// Private chats with the bridge: a phone opens one in Bitchat as with any person nearby, and asks there what it
// would ask in the public chat. The answers stay in that chat. Nothing of it goes to LoRa but what a command
// itself sends there (an SOS, a range test).
//
// Handshake packets are unsigned, so anyone can claim a phone's ID: nothing unauthenticated ends a session or a
// handshake under way, a newcomer never pushes out a chat in use, and handshakes are limited per phone and overall.
#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_system.h>

using namespace bitchat;

static constexpr uint8_t TYPE_HANDSHAKE = 0x10;
static constexpr uint8_t TYPE_ENCRYPTED = 0x11;
static constexpr uint8_t KIND_MESSAGE = 0x01;
static constexpr uint8_t KIND_DELIVERED = 0x03;
// The largest private message read: an ID and up to 255 bytes of text, with room to spare.
static constexpr size_t PRIVATE_LIMIT = 400;
static constexpr uint32_t SESSION_IDLE_MS = 30UL * 60 * 1000;
// A chat used this recently is not given up for a newcomer.
static constexpr uint32_t SESSION_KEEP_MS = 2UL * 60 * 1000;
static constexpr uint32_t HANDSHAKE_WAIT_MS = 20000;
// After the first try, the bridge starts a handshake with the same phone at most once a minute.
static constexpr uint32_t INITIATE_EVERY_MS = 60000;
// Messages that do not open count against a session only when none has opened for this long.
static constexpr uint32_t QUIET_BEFORE_RESET_MS = 10000;

static uint32_t idHash(Bytes id)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < id.size; ++i)
        hash = (hash ^ id.data[i]) * 16777619u;
    return hash ? hash : 1;
}

// Random bytes, or false when the generator gave nothing (the nRF52 one fills zeros after a timeout).
static bool randomBytes(uint8_t *out, size_t size)
{
    esp_fill_random(out, size);
    uint8_t any = 0;
    for (size_t i = 0; i < size; ++i)
        any |= out[i];
    return any != 0;
}

// An ID like the apps' own: a random UUID in capitals.
static bool newMessageId(char out[37])
{
    uint8_t bytes[16];
    if (!randomBytes(bytes, sizeof(bytes)))
        return false;
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    static constexpr char DIGITS[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[o++] = '-';
        out[o++] = DIGITS[bytes[i] >> 4];
        out[o++] = DIGITS[bytes[i] & 15];
    }
    out[o] = 0;
    return true;
}

static bool sameWord(Bytes text, const char *word)
{
    const size_t size = std::strlen(word);
    if (text.size != size)
        return false;
    for (size_t i = 0; i < size; ++i) {
        const char c = char(text.data[i]);
        if ((c >= 'A' && c <= 'Z' ? char(c + 32) : c) != word[i])
            return false;
    }
    return true;
}

static void clearSession(PrivateSession &session)
{
    session.handshake.wipe();
    noiseWipe(&session.send, sizeof(session.send));
    noiseWipe(&session.receive, sizeof(session.receive));
    session = {};
}

void BitchatBridge::expireHandshake(PrivateSession &session, uint32_t now)
{
    if (session.handshaking && !Throttle::isWithinTimespanMsAt(session.handshakeAt, HANDSHAKE_WAIT_MS, now)) {
        session.handshaking = false;
        session.handshake.wipe();
    }
}

PrivateSession *BitchatBridge::privateSession(const PeerId &peer, uint32_t now, bool create)
{
    if (!runtime->privates && (!create || !allocateBuffer(runtime->privates)))
        return nullptr;
    PrivateSession *found = nullptr, *free = nullptr, *oldest = nullptr;
    for (auto &session : runtime->privates->slots) {
        if (session.used) {
            expireHandshake(session, now);
            if (!session.handshaking && !Throttle::isWithinTimespanMsAt(session.usedAt, SESSION_IDLE_MS, now))
                clearSession(session);
        }
        if (session.used && session.peer == peer)
            found = &session;
        else if (!session.used) {
            if (!free)
                free = &session;
        } else if (!session.handshaking && !Throttle::isWithinTimespanMsAt(session.usedAt, SESSION_KEEP_MS, now) &&
                   (!oldest || int32_t(session.usedAt - oldest->usedAt) < 0)) {
            oldest = &session;
        }
    }
    if (found || !create)
        return found;
    PrivateSession *slot = free ? free : oldest;
    if (!slot)
        return nullptr;
    clearSession(*slot);
    slot->peer = peer;
    slot->used = true;
    slot->usedAt = now;
    return slot;
}

void BitchatBridge::privateFrame(const Packet &packet, const Peer &peer, uint16_t source, uint32_t now)
{
    // Bitchat never compresses ciphertext in practice; a compressed one is not read.
    if (packet.compressed() || !utc())
        return;
    // Only a first handshake message, or ciphertext from a phone this bridge lost track of, may take a slot.
    const bool opener =
        packet.type == TYPE_HANDSHAKE ? packet.payload.size == NOISE_MESSAGE1 : packet.payload.size > NOISE_TRANSPORT_OVERHEAD;
    PrivateSession *session = privateSession(packet.sender, now, opener);
    if (!session)
        return;
    expireHandshake(*session, now);
    // Answers go straight to the phone when it is connected to this radio, and through the others when not.
    session->link = source != NO_LINK && packet.ttl == 7 ? source : NO_LINK;
    if (packet.type == TYPE_HANDSHAKE)
        privateHandshake(*session, packet, now);
    else if (packet.type == TYPE_ENCRYPTED)
        privateTransport(*session, packet, peer, session->link != NO_LINK, now);
}

static void established(PrivateSession &session, NoiseHandshake &trial, uint32_t now)
{
    trial.split(session.send, session.receive);
    trial.wipe();
    session.handshake.wipe();
    session.handshaking = false;
    session.ready = true;
    session.failures = 0;
    session.usedAt = now;
    session.openedAt = now ? now : 1;
}

void BitchatBridge::privateHandshake(PrivateSession &session, const Packet &packet, uint32_t now)
{
    const Bytes in = packet.payload;
    auto &handshake = session.handshake;
    const uint8_t *staticPrivate = identity.data() + 32;
    PeerId proved{};
    if (session.handshaking && handshake.initiator()) {
        if (in.size == NOISE_MESSAGE2) {
            // The phone answered this bridge's handshake: it must prove the key its ID stands for. Read on a
            // copy, so bytes that do not authenticate leave the handshake waiting for the real answer.
            NoiseHandshake trial = handshake;
            uint8_t out[NOISE_MESSAGE3];
            bool ok = trial.readMessage2(in);
            if (ok) {
                noisePeerId(trial.remoteStatic(), proved);
                ok = proved == packet.sender && trial.writeMessage3(staticPrivate, noisePublic.data(), out) &&
                     sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)}, session.link);
            }
            if (ok) {
                established(session, trial, now);
                deliverMail(session, now);
            } else {
                trial.wipe();
            }
            return;
        }
        // Both sides started at once: the lower ID stays the initiator, as in both apps.
        if (in.size != NOISE_MESSAGE1 ||
            std::lexicographical_compare(ownId.begin(), ownId.end(), packet.sender.begin(), packet.sender.end()))
            return;
    } else if (session.handshaking && in.size == NOISE_MESSAGE3) {
        NoiseHandshake trial = handshake;
        bool ok = trial.readMessage3(in);
        if (ok) {
            noisePeerId(trial.remoteStatic(), proved);
            ok = proved == packet.sender;
        }
        // The new session replaces the old one only now; anything else keeps waiting.
        if (ok) {
            established(session, trial, now);
            deliverMail(session, now);
        } else {
            trial.wipe();
        }
        return;
    }
    // Only a first message is answered: with no handshake under way the apps take anything as one, so a
    // stray second or third message is never sent back.
    if (in.size != NOISE_MESSAGE1)
        return;
    // Each answer costs three curve operations: three per phone, then one every 20 s; for all phones together,
    // six, then one every five seconds.
    if (!session.handshakes.admit(now, 3, 20000) || !runtime->handshakes.admit(now, 6, 5000))
        return;
    uint8_t ephemeral[NOISE_KEY];
    if (!randomBytes(ephemeral, sizeof(ephemeral)))
        return;
    NoiseHandshake fresh;
    fresh.start(false, ephemeral);
    noiseWipe(ephemeral, sizeof(ephemeral));
    uint8_t out[NOISE_MESSAGE2];
    if (!fresh.readMessage1(in) || !fresh.writeMessage2(staticPrivate, noisePublic.data(), out) ||
        !sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)}, session.link)) {
        fresh.wipe();
        return;
    }
    // The answer went out: this handshake replaces any earlier one (the phone started again).
    handshake = fresh;
    fresh.wipe();
    session.handshaking = true;
    session.handshakeAt = now;
}

bool BitchatBridge::initiatePrivate(PrivateSession &session, uint32_t now)
{
    expireHandshake(session, now);
    if (session.handshaking ||
        (session.initiatedAt && Throttle::isWithinTimespanMsAt(session.initiatedAt, INITIATE_EVERY_MS, now)) ||
        !session.handshakes.admit(now, 3, 20000) || !runtime->handshakes.admit(now, 6, 5000))
        return false;
    uint8_t ephemeral[NOISE_KEY];
    if (!randomBytes(ephemeral, sizeof(ephemeral)))
        return false;
    session.initiatedAt = now ? now : 1;
    session.handshake.start(true, ephemeral);
    noiseWipe(ephemeral, sizeof(ephemeral));
    uint8_t out[NOISE_MESSAGE1];
    if (!session.handshake.writeMessage1(out) || !sendNoise(TYPE_HANDSHAKE, session.peer, {out, sizeof(out)}, session.link)) {
        session.handshake.wipe();
        return false;
    }
    session.handshaking = true;
    session.handshakeAt = now;
    return true;
}

void BitchatBridge::privateTransport(PrivateSession &session, const Packet &packet, const Peer &peer, bool direct, uint32_t now)
{
    const Bytes in = packet.payload;
    if (!session.ready) {
        // A phone that still holds a session this bridge lost (a restart): a new handshake, as the apps do.
        initiatePrivate(session, now);
        return;
    }
    if (in.size <= NOISE_TRANSPORT_OVERHEAD || in.size > PRIVATE_LIMIT + NOISE_TRANSPORT_OVERHEAD)
        return;
    uint8_t plain[PRIVATE_LIMIT];
    if (!noiseDecrypt(session.receive, in, plain)) {
        // Three in a row that do not open, while nothing opened for a while: the phone's keys are not this
        // session's. Junk under the phone's ID while it is talking changes nothing.
        if (!Throttle::isWithinTimespanMsAt(session.usedAt, QUIET_BEFORE_RESET_MS, now) && ++session.failures >= 3) {
            session.failures = 0;
            initiatePrivate(session, now);
        }
        return;
    }
    const size_t size = in.size - NOISE_TRANSPORT_OVERHEAD;
    session.failures = 0;
    session.usedAt = now;
    ++privateIn;
    // Delivery and read receipts, verification, files: nothing for the bridge to do.
    if (plain[0] != KIND_MESSAGE) {
        noiseWipe(plain, size);
        return;
    }
    Bytes id{}, content{};
    bool valid = true;
    for (size_t offset = 1; offset < size && valid;) {
        if (offset + 2 > size || offset + 2 + plain[offset + 1] > size) {
            valid = false;
            break;
        }
        const Bytes value{plain + offset + 2, plain[offset + 1]};
        if (plain[offset] == 0)
            id = value;
        else if (plain[offset] == 1)
            content = value;
        else
            valid = false; // both apps refuse fields they do not know
        offset += 2 + value.size;
    }
    if (valid && id.size) {
        // Delivered, every time: without it the app keeps the message and sends it again. A message is
        // remembered only when the delivery went out, so one that could not be acknowledged is answered when
        // the app sends it again.
        if (sendPrivate(session, KIND_DELIVERED, id)) {
            const uint32_t hash = idHash(id);
            const bool repeat = std::find(session.recent.begin(), session.recent.end(), hash) != session.recent.end();
            if (!repeat) {
                session.recent[session.recentNext] = hash;
                session.recentNext = uint8_t((session.recentNext + 1) % session.recent.size());
                // Favourite marks are the app's own control messages, not chat.
                if (content.size && validUtf8(content) && !startsWith(content, "[FAVORITED]") &&
                    !startsWith(content, "[UNFAVORITED]"))
                    privateRequest(session, peer, content, direct, now);
            }
        }
    }
    noiseWipe(plain, size);
}

void BitchatBridge::privateRequest(PrivateSession &session, const Peer &peer, Bytes text, bool direct, uint32_t now)
{
    // "@name text": a direct message to a LoRa contact.
    if (text.size && text.data[0] == '@') {
        privateReply = &session;
        privateDirect(session, peer, text, now);
        privateReply = nullptr;
        return;
    }
    BridgeRequest asked;
    // In the private chat the command words are enough: "bateria" as "!ponte bateria".
    char line[COMMAND_TEXT_LIMIT + 1];
    if (!parseBridgeCommand(text, asked)) {
        if (sameWord(text, "ponte") || sameWord(text, "estado")) {
            asked.command = Command::Status;
            asked.portuguese = true;
        } else if (sameWord(text, "bridge") || sameWord(text, "status")) {
            asked.command = Command::Status;
        } else {
            for (const char *prefix : {"!bridge ", "!ponte "}) {
                const size_t length = std::strlen(prefix);
                if (text.size + length >= sizeof(line))
                    continue;
                std::memcpy(line, prefix, length);
                std::memcpy(line + length, text.data, text.size);
                if (parseBridgeCommand({reinterpret_cast<const uint8_t *>(line), length + text.size}, asked))
                    break;
            }
        }
    }
    privateReply = &session;
    if (asked.command == Command::None || asked.command == Command::Help) {
        static constexpr char HELP[] =
            "Ponte LoRa. Escreva: ponte (estado), bateria, quem, guia, teste, recado <texto>, sos <texto>, ou "
            "@contato texto para mandar pelo LoRa. In English: bridge, battery, who, guide, test, note, sos, @contact.";
        replyPrivately(HELP, sizeof(HELP) - 1);
    } else {
        answerAsked(asked, peer, direct, now);
    }
    privateReply = nullptr;
}

bool BitchatBridge::replyPrivately(const char *text, size_t size)
{
    if (!privateReply)
        return false;
    const size_t prefix = sizeof(REPLY_PREFIX) - 1;
    if (size >= prefix && std::memcmp(text, REPLY_PREFIX, prefix) == 0) {
        text += prefix;
        size -= prefix;
    }
    return sendPrivateText(*privateReply, text, size);
}

bool BitchatBridge::sendPrivateText(PrivateSession &session, const char *text, size_t size)
{
    if (!session.ready)
        return false;
    // At most 255 bytes, cut on a character boundary.
    if (size > 255) {
        size = 255;
        while (size && (uint8_t(text[size]) & 0xc0) == 0x80)
            --size;
    }
    char id[37];
    if (!newMessageId(id))
        return false;
    uint8_t body[2 + 36 + 2 + 255];
    size_t offset = 0;
    body[offset++] = 0;
    body[offset++] = 36;
    std::memcpy(body + offset, id, 36);
    offset += 36;
    body[offset++] = 1;
    body[offset++] = uint8_t(size);
    std::memcpy(body + offset, text, size);
    offset += size;
    const bool sent = sendPrivate(session, KIND_MESSAGE, {body, offset});
    noiseWipe(body, offset);
    return sent;
}

bool BitchatBridge::sendPrivate(PrivateSession &session, uint8_t kind, Bytes body)
{
    if (!session.ready || body.size + 1 > PRIVATE_LIMIT || !session.outgoing.admit(millis(), 10, 1000))
        return false;
    uint8_t plain[PRIVATE_LIMIT];
    uint8_t sealed[PRIVATE_LIMIT + NOISE_TRANSPORT_OVERHEAD];
    plain[0] = kind;
    std::memcpy(plain + 1, body.data, body.size);
    const bool ok = noiseEncrypt(session.send, {plain, body.size + 1}, sealed) &&
                    sendNoise(TYPE_ENCRYPTED, session.peer, {sealed, body.size + 1 + NOISE_TRANSPORT_OVERHEAD}, session.link);
    noiseWipe(plain, body.size + 1);
    if (ok)
        ++privateOut;
    return ok;
}

// Unsigned and addressed to one phone, as iOS sends them. Padded as the apps pad these two types: the apps'
// stream reader takes their length as the padded one. Straight to the phone when it is connected here; else to
// every phone, which pass it on. Not put in the shared list of recent packets: the copies the phones relay
// back carry this bridge's own ID and are dropped anyway.
bool BitchatBridge::sendNoise(uint8_t type, const PeerId &to, Bytes payload, uint16_t link)
{
    if (!identityReady || !utc())
        return false;
    const uint64_t timestamp = nextTimestamp();
    if (encode(type, ownId, &to, timestamp, 7, payload, {}, runtime->working, true) != Result::Ok)
        return false;
    const bool sent = (link != NO_LINK && bleSendTo(runtime->working.bytes(), link)) || bleBroadcast(runtime->working.bytes());
    if (!sent) {
        reject(Result::Full);
        return false;
    }
    ++originatedBle;
    return true;
}
#endif

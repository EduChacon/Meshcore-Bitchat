// Private chats with the bridge: a phone opens one in Bitchat as with any person nearby, and asks there what it
// would ask in the public chat. The answers stay in that chat. Nothing of it goes to LoRa but what a command
// itself sends there (an SOS, a range test).
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
static constexpr uint32_t HANDSHAKE_WAIT_MS = 20000;
static constexpr uint32_t INITIATE_EVERY_MS = 10000;
static constexpr uint32_t PRIVATE_DEDUP_MS = 125000;

static uint32_t idHash(Bytes id)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < id.size; ++i)
        hash = (hash ^ id.data[i]) * 16777619u;
    return hash ? hash : 1;
}

// An ID like the apps' own: a random UUID in capitals.
static void newMessageId(char out[37])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
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

PrivateSession *BitchatBridge::privateSession(const PeerId &peer, uint32_t now)
{
    if (!allocateBuffer(runtime->privates))
        return nullptr;
    PrivateSession *found = nullptr, *free = nullptr, *oldest = nullptr;
    for (auto &session : runtime->privates->slots) {
        if (session.used && !session.handshaking && !Throttle::isWithinTimespanMsAt(session.usedAt, SESSION_IDLE_MS, now)) {
            session.handshake.wipe();
            session = {};
        }
        if (session.used && session.peer == peer)
            found = &session;
        else if (!session.used && !free)
            free = &session;
        else if (session.used && (!oldest || int32_t(session.usedAt - oldest->usedAt) < 0))
            oldest = &session;
    }
    if (found)
        return found;
    PrivateSession *slot = free ? free : oldest;
    if (!slot)
        return nullptr;
    slot->handshake.wipe();
    *slot = {};
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
    PrivateSession *session = privateSession(packet.sender, now);
    if (!session)
        return;
    if (packet.type == TYPE_HANDSHAKE)
        privateHandshake(*session, packet, now);
    else if (packet.type == TYPE_ENCRYPTED)
        privateTransport(*session, packet, peer, source != NO_LINK && packet.ttl == 7, now);
}

void BitchatBridge::privateHandshake(PrivateSession &session, const Packet &packet, uint32_t now)
{
    const Bytes in = packet.payload;
    auto &handshake = session.handshake;
    const uint8_t *staticPrivate = identity.data() + 32;
    PeerId proved{};
    if (session.handshaking && !Throttle::isWithinTimespanMsAt(session.handshakeAt, HANDSHAKE_WAIT_MS, now)) {
        session.handshaking = false;
        handshake.wipe();
    }
    if (session.handshaking && handshake.initiator()) {
        if (in.size == NOISE_MESSAGE2) {
            // The phone answered this bridge's handshake: it must prove the key its ID stands for.
            uint8_t out[NOISE_MESSAGE3];
            session.handshaking = false;
            bool ok = handshake.readMessage2(in);
            if (ok) {
                noisePeerId(handshake.remoteStatic(), proved);
                ok = proved == packet.sender && handshake.writeMessage3(staticPrivate, noisePublic.data(), out) &&
                     sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)});
            }
            if (ok) {
                handshake.split(session.send, session.receive);
                session.ready = true;
                session.failures = 0;
                session.usedAt = now;
            }
            handshake.wipe();
            return;
        }
        // Both sides started at once: the lower ID stays the initiator, as in both apps.
        if (in.size != NOISE_MESSAGE1 ||
            std::lexicographical_compare(ownId.begin(), ownId.end(), packet.sender.begin(), packet.sender.end()))
            return;
    } else if (session.handshaking && in.size == NOISE_MESSAGE3) {
        session.handshaking = false;
        bool ok = handshake.readMessage3(in);
        if (ok) {
            noisePeerId(handshake.remoteStatic(), proved);
            ok = proved == packet.sender;
        }
        if (ok) {
            // The new session replaces the old one only now.
            handshake.split(session.send, session.receive);
            session.ready = true;
            session.failures = 0;
            session.usedAt = now;
        }
        handshake.wipe();
        return;
    }
    // Only a first message is answered: with no handshake under way the apps take anything as one, so a
    // stray second or third message is never sent back.
    if (in.size != NOISE_MESSAGE1)
        return;
    // Each answer costs three curve operations: a burst, then one every five seconds, for all phones together.
    if (!runtime->handshakes.admit(now, 6, 5000))
        return;
    uint8_t ephemeral[NOISE_KEY];
    esp_fill_random(ephemeral, sizeof(ephemeral));
    handshake.start(false, ephemeral);
    std::fill(std::begin(ephemeral), std::end(ephemeral), uint8_t(0));
    uint8_t out[NOISE_MESSAGE2];
    session.handshaking = false;
    if (!handshake.readMessage1(in) || !handshake.writeMessage2(staticPrivate, noisePublic.data(), out) ||
        !sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)})) {
        handshake.wipe();
        return;
    }
    session.handshaking = true;
    session.handshakeAt = now;
}

bool BitchatBridge::initiatePrivate(PrivateSession &session, uint32_t now)
{
    if (session.handshaking ||
        (session.initiatedAt && Throttle::isWithinTimespanMsAt(session.initiatedAt, INITIATE_EVERY_MS, now)) ||
        !runtime->handshakes.admit(now, 6, 5000))
        return false;
    session.initiatedAt = now ? now : 1;
    uint8_t ephemeral[NOISE_KEY];
    esp_fill_random(ephemeral, sizeof(ephemeral));
    session.handshake.start(true, ephemeral);
    std::fill(std::begin(ephemeral), std::end(ephemeral), uint8_t(0));
    uint8_t out[NOISE_MESSAGE1];
    if (!session.handshake.writeMessage1(out) || !sendNoise(TYPE_HANDSHAKE, session.peer, {out, sizeof(out)})) {
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
        // Three in a row that do not open: the phone's keys are not this session's.
        if (++session.failures >= 3) {
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
        std::fill(plain, plain + size, uint8_t(0));
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
        // Delivered, every time: without it the app keeps the message and sends it again.
        sendPrivate(session, KIND_DELIVERED, id);
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
    std::fill(plain, plain + size, uint8_t(0));
}

void BitchatBridge::privateRequest(PrivateSession &session, const Peer &peer, Bytes text, bool direct, uint32_t now)
{
    // One request a second per chat; the public chat's spacing does not apply here.
    if (session.commandAt && Throttle::isWithinTimespanMsAt(session.commandAt, 1000, now))
        return;
    session.commandAt = now ? now : 1;
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
                    break;
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
            "Ponte LoRa. Escreva: ponte (estado), bateria, quem, guia, teste, recado <texto>, sos <texto>. "
            "In English: bridge, battery, who, guide, test, note <text>, sos <text>.";
        replyPrivately(HELP, sizeof(HELP) - 1);
    } else {
        answerAsked(asked, peer, direct, now);
    }
    privateReply = nullptr;
}

bool BitchatBridge::replyPrivately(const char *text, size_t size)
{
    if (!privateReply || !privateReply->ready)
        return false;
    const size_t prefix = sizeof(REPLY_PREFIX) - 1;
    if (size >= prefix && std::memcmp(text, REPLY_PREFIX, prefix) == 0) {
        text += prefix;
        size -= prefix;
    }
    size = std::min<size_t>(size, 255);
    char id[37];
    newMessageId(id);
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
    return sendPrivate(*privateReply, KIND_MESSAGE, {body, offset});
}

bool BitchatBridge::sendPrivate(PrivateSession &session, uint8_t kind, Bytes body)
{
    if (!session.ready || body.size + 1 > PRIVATE_LIMIT)
        return false;
    uint8_t plain[PRIVATE_LIMIT];
    uint8_t sealed[PRIVATE_LIMIT + NOISE_TRANSPORT_OVERHEAD];
    plain[0] = kind;
    std::memcpy(plain + 1, body.data, body.size);
    const bool ok = noiseEncrypt(session.send, {plain, body.size + 1}, sealed) &&
                    sendNoise(TYPE_ENCRYPTED, session.peer, {sealed, body.size + 1 + NOISE_TRANSPORT_OVERHEAD});
    std::fill(std::begin(plain), std::end(plain), uint8_t(0));
    if (ok)
        ++privateOut;
    return ok;
}

// Unsigned and addressed to one phone, as iOS sends them; the phones in between pass it on. Padded as the apps
// pad these two types: the apps' stream reader takes their length as the padded one.
bool BitchatBridge::sendNoise(uint8_t type, const PeerId &to, Bytes payload)
{
    if (!identityReady || !utc())
        return false;
    const uint64_t timestamp = nextTimestamp();
    if (encode(type, ownId, &to, timestamp, 7, payload, {}, runtime->working, true) != Result::Ok)
        return false;
    Digest id;
    if (packetDigest(crypto, runtime->working.bytes(), id) == Result::Ok)
        runtime->dedup->insert(id, millis(), PRIVATE_DEDUP_MS);
    if (!bleBroadcast(runtime->working.bytes())) {
        reject(Result::Full);
        return false;
    }
    ++originatedBle;
    return true;
}
#endif

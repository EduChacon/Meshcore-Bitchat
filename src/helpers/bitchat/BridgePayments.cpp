#ifdef WITH_BITCHAT_BRIDGE
// Cashu payments over LoRa. A phone near this radio posts a token in Bitchat's public chat; the bridge
// carries the payer's own frames, whole, to the bridges on the same MeshCore channel, which give them to
// the phones near them. The bridge never holds, opens or spends a token: it only decides whether the
// token may travel, and tells the payer how it went. See docs/bitchat/CASHU.md.
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include "BridgeInflate.h"
#include "BridgePlatform.h"
#include <Arduino.h>
#include <SHA256.h>
#include <cstring>

using namespace bitchat;

namespace
{
// Text and token of one message, inflated: on the heap while it is read, as the stack is short.
struct CashuWork {
    uint8_t text[MAX_PAYLOAD];
    uint8_t scratch[CASHU_SCRATCH];
};

// A phone takes an announce up to two minutes old by its own clock, and the trip takes some of that: an
// older one is not sent, the bridge waits for the payer's next (phones announce every 15 to 40 s).
constexpr uint32_t ANNOUNCE_FRESH_MS = 30000;
constexpr uint32_t ANNOUNCE_WAIT_MS = 45000;
// After a refusal from a bridge, another one may still take the payment for this long.
constexpr uint32_t REFUSAL_GRACE_MS = 15000;

// At most `limit` bytes of UTF-8, cut between characters.
size_t clip(const char *text, size_t limit, char *out, size_t capacity)
{
    size_t n = std::min(strlen(text), std::min(limit, capacity - 1));
    while (n && (uint8_t(text[n]) & 0xc0) == 0x80)
        --n;
    memcpy(out, text, n);
    out[n] = 0;
    return n;
}

// "21 sat"
void amountText(uint64_t amount, const char *unit, char *out, size_t capacity)
{
    char digits[21];
    size_t n = 0;
    do {
        digits[n++] = char('0' + amount % 10);
        amount /= 10;
    } while (amount && n < sizeof(digits));
    size_t at = 0;
    while (n && at + 1 < capacity)
        out[at++] = digits[--n];
    snprintf(out + at, capacity - at, " %s", unit[0] ? unit : "sat");
}

// A frame without the padding the sender may have added: phones read both, and LoRa airtime is dear.
Bytes natural(Bytes frame, const Packet &packet)
{
    return {frame.data, std::min(frame.size, packet.naturalSize)};
}
} // namespace

// A line to every phone, "[bridge] " in front, cut between characters to fit.
void BitchatBridge::say(const char *text)
{
    char clipped[BITCHAT_TEXT_LIMIT];
    tellBitchat(clipped, clip(text, BITCHAT_TEXT_LIMIT - sizeof(REPLY_PREFIX), clipped, sizeof(clipped)));
}

// Whether text on the bridge's LoRa channel stays among those given its key. A hashtag channel's key
// follows from its name, so anyone may listen there.
bool BitchatBridge::channelPrivate() const
{
    char name[32];
    uint8_t secret[32];
    if (!host || settings.channel == NO_CHANNEL || !host->readChannel(settings.channel, name, secret))
        return false;
    name[31] = 0;
    // MeshCore's well-known Public key.
    static const uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                           0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
    if (std::equal(PUBLIC_KEY, PUBLIC_KEY + 16, secret))
        return false;
    // A hashtag key, whatever name it was saved under: "#name" as written and in lower case, with or
    // without the "#" in the stored name.
    const char *bare = name[0] == '#' ? name + 1 : name;
    for (int lowered = 0; lowered < 2; ++lowered) {
        char tag[34] = "#";
        size_t n = 1;
        for (const char *c = bare; *c && n + 1 < sizeof(tag); ++c)
            tag[n++] = lowered && *c >= 'A' && *c <= 'Z' ? char(*c - 'A' + 'a') : *c;
        tag[n] = 0;
        Digest hash;
        SHA256 sha;
        sha.update(tag, n);
        sha.finalize(hash.data(), hash.size());
        if (std::equal(hash.begin(), hash.begin() + 16, secret))
            return false;
    }
    return true;
}

void BitchatBridge::rememberAnnounce(const Packet &packet, Bytes frame)
{
    const Bytes kept = natural(frame, packet);
    if (kept.size > runtime->announced[0].frame.size())
        return;
    auto *slot = &runtime->announced[0];
    for (auto &entry : runtime->announced) {
        if (entry.size && entry.peer == packet.sender) {
            slot = &entry;
            break;
        }
        if (millis() - entry.at > millis() - slot->at || !entry.size)
            slot = &entry;
    }
    slot->peer = packet.sender;
    slot->size = uint16_t(kept.size);
    slot->at = millis();
    std::copy_n(kept.data, kept.size, slot->frame.begin());
    if (payment.holding && payment.sender == packet.sender && runtime->held && runtime->tunnelOut)
        startPayment(kept, runtime->held->bytes());
}

bool BitchatBridge::carryCashu(const Packet &packet, Bytes frame, const Peer &peer, const Digest &id)
{
    if (!packet.compressed() && !mentionsCashu(packet.payload))
        return false;
    std::unique_ptr<CashuWork> work;
    if (!allocateBuffer(work)) {
        // Unread, a token could go out as pieces of text: an uncompressed message that names one stays here.
        reject(Result::Full);
        return !packet.compressed();
    }
    Bytes text;
    if (!messageText(packet, work->text, sizeof(work->text), text))
        return false;
    CashuToken token;
    const CashuFound found = findCashu(text, work->scratch, token);
    work.reset();
    if (found == CashuFound::None)
        return false;
    // Without a LoRa channel the payment simply stays in this room.
    if (!textReady || textConflict || seen(id))
        return true;
    // Refusals are spaced out: a phone repeating a bad token gets one answer, not a flood of them.
    const auto tell = [&](const char *message) {
        if (paymentReplies.admit(millis(), 3000))
            say(message);
    };
    if (found == CashuFound::Malformed) {
        tell("Token Cashu ilegível: não foi levado pela LoRa.");
        return true;
    }
    if (found == CashuFound::Several) {
        tell("Um token por mensagem: nada foi levado pela LoRa.");
        return true;
    }
    // The phones near the other bridge read Bitchat's public chat too: there a token anyone may spend is
    // theirs for the taking. Only a key-locked token goes over a channel whose key is public.
    if (token.unlocked() && !channelPrivate()) {
        tell("Token sem trava P2PK: num canal aberto outros poderiam gastá-lo. Não foi pela LoRa.");
        return true;
    }
    if ((runtime->tunnelOut && runtime->tunnelOut->active()) || payment.waiting || payment.holding) {
        tell("A ponte já leva outro pagamento: reenvie em 2 min.");
        return true;
    }
    if (!allocateBuffer(runtime->tunnelOut)) {
        tell("Sem memória para o pagamento agora: reenvie em 1 min.");
        return true;
    }
    const Bytes message = natural(frame, packet);
    if (4 + message.size > TUNNEL_BUNDLE - 120) {
        tell("Token grande demais para a LoRa: junte as provas na carteira e reenvie.");
        return true;
    }
    payment = {};
    payment.id = read32(id.data());
    payment.amount = token.amount;
    snprintf(payment.unit, sizeof(payment.unit), "%s", token.unit);
    char label[LABEL_LIMIT + 8];
    peerLabel(peer, label, sizeof(label));
    clip(label, 14, payment.payer, sizeof(payment.payer));
    payment.sender = packet.sender;
    payment.messageId = id;
    // Every limit counts from when the payer's phone signed the message: phones check that time.
    const uint64_t now = utc();
    payment.signedAt = millis() - uint32_t(packet.timestamp < now ? std::min<uint64_t>(now - packet.timestamp, 120000) : 0);
    for (const auto &entry : runtime->announced)
        if (entry.size && entry.peer == packet.sender && millis() - entry.at < ANNOUNCE_FRESH_MS) {
            startPayment({entry.frame.data(), entry.size}, message);
            return true;
        }
    // Held until the payer announces again.
    if (!allocateBuffer(runtime->held)) {
        tell("Sem memória para o pagamento agora: reenvie em 1 min.");
        return true;
    }
    std::copy_n(message.data, message.size, runtime->held->data.begin());
    runtime->held->size = message.size;
    payment.holding = true;
    payment.heldAt = millis();
    interval = 0;
    return true;
}

void BitchatBridge::startPayment(Bytes announce, Bytes message)
{
    payment.holding = false;
    // Too little of the two minutes left for the trip and a resend: better to say so now.
    if (!Throttle::isWithinTimespanMsAt(payment.signedAt, 70000, millis())) {
        say("Seu celular demorou a se anunciar: nada foi levado. Reenvie o token.");
        runtime->held.reset();
        return;
    }
    if (!runtime->tunnelOut->start(payment.id, announce, message, payment.signedAt)) {
        say("Token grande demais para a LoRa: junte as provas na carteira e reenvie.");
        runtime->held.reset();
        return;
    }
    // `message` may be the held copy: read before it is let go.
    Digest hash;
    crypto.hash(message, hash);
    std::copy_n(hash.begin(), payment.messageHash.size(), payment.messageHash.begin());
    runtime->held.reset();
    // Remembered across a restart, so the same message is never carried twice.
    record(payment.messageId);
    payment.at = payment.signedAt;
    payment.channel = settings.channel;
    payment.binding = settings.binding;
    payment.waiting = true;
    char amount[32], line[160];
    amountText(payment.amount, payment.unit, amount, sizeof(amount));
    snprintf(line, sizeof(line), "Levando %s de %s pela LoRa (%u partes)…", amount, payment.payer,
             unsigned(runtime->tunnelOut->count()));
    say(line);
    interval = 0;
}

// The payer is told in one line, the outcome first: Bitchat text from the bridge stays under 96 bytes.
// A bridge is named only when its radio is a contact here and signed the answer; otherwise it is the
// number it claims, marked as not a contact.
void BitchatBridge::reportPayment()
{
    char amount[32], who[24], line[160];
    amountText(payment.amount, payment.unit, amount, sizeof(amount));
    if (payment.trusted)
        snprintf(who, sizeof(who), "%s", payment.bridge);
    else
        snprintf(who, sizeof(who), "%04x", unsigned(payment.node >> 16));
    if (!payment.answered)
        snprintf(line, sizeof(line), "✗ %s sem confirmação. Se não chegou, o token segue com você.", amount);
    else if (payment.outcome == TunnelOutcome::Delivered && payment.trusted)
        snprintf(line, sizeof(line), "✓ %s entregue à ponte %s (%u cel.). Confirme com quem recebe.", amount, who,
                 unsigned(payment.phones));
    else if (payment.outcome == TunnelOutcome::Delivered)
        snprintf(line, sizeof(line), "✓? %s entregue, diz a ponte %s (não é contato). Confirme com quem recebe.", amount, who);
    else if (payment.outcome == TunnelOutcome::NoPhones)
        snprintf(line, sizeof(line), "✗ %s: ponte %s sem celular perto. O token segue com você.", amount, who);
    else if (payment.outcome == TunnelOutcome::Late)
        snprintf(line, sizeof(line), "✗ %s chegou tarde à ponte %s. O token segue com você.", amount, who);
    else
        snprintf(line, sizeof(line), "✗ A ponte %s recusou %s. O token segue com você.", who, amount);
    say(line);
    payment.waiting = false;
    if (runtime && runtime->tunnelOut)
        runtime->tunnelOut->finish();
}

void BitchatBridge::paymentAnswered(const TunnelHeard &heard, uint32_t now)
{
    if (!payment.waiting || heard.id != payment.id)
        return;
    if (heard.kind == TunnelKind::Want) {
        if (runtime->tunnelOut)
            runtime->tunnelOut->resend(heard.missing, now);
        return;
    }
    char name[32] = "";
    bool trusted = false;
    if (heard.signedAck) {
        uint8_t signedBytes[TUNNEL_ACK_SIGNED];
        tunnelAckSigned(heard.id, heard.outcome, heard.phones, heard.key.data(), payment.messageHash.data(), signedBytes);
        const auto check = host->verifyNode(heard.key.data(), signedBytes, sizeof(signedBytes), heard.signature.data(), name);
        // A known radio's key that does not sign this answer: someone else wrote it.
        if (check == MeshHost::NodeCheck::Invalid) {
            reject(Result::Unauthenticated);
            return;
        }
        trusted = check == MeshHost::NodeCheck::Valid;
    }
    // A checked answer beats an unchecked one, which anyone on the channel could write; then delivered
    // beats refused.
    const bool delivered = heard.outcome == TunnelOutcome::Delivered;
    const uint8_t rank = uint8_t((trusted ? 2 : 0) + (delivered ? 1 : 0) + 1);
    if (rank > payment.rank) {
        payment.rank = rank;
        payment.outcome = heard.outcome;
        payment.phones = heard.phones;
        payment.trusted = trusted;
        payment.node = heard.node;
        name[31] = 0;
        clip(name, 14, payment.bridge, sizeof(payment.bridge));
    }
    if (!payment.answered) {
        payment.answered = true;
        payment.answeredAt = now;
    }
    // Only a delivery signed by a known radio ends the wait; another bridge may still have done better.
    if (delivered && trusted)
        reportPayment();
}

bool BitchatBridge::tunnelDue() const
{
    return runtime->tunnelControl.size() || (runtime->tunnelOut && runtime->tunnelOut->hasPending());
}

void BitchatBridge::watchPayment(uint32_t now)
{
    if (runtime->tunnelIn) {
        TunnelDatagram want;
        if (runtime->tunnelControl.size() < 3 && runtime->tunnelIn->want(now, want))
            runtime->tunnelControl.push(want);
        runtime->tunnelIn->expire(now);
        if (runtime->tunnelIn->empty())
            runtime->tunnelIn.reset();
    }
    if (payment.holding && !Throttle::isWithinTimespanMsAt(payment.heldAt, ANNOUNCE_WAIT_MS, now)) {
        payment.holding = false;
        runtime->held.reset();
        say("Seu celular não se anunciou a tempo: nada foi levado. Reenvie o token.");
    }
    if (runtime->tunnelOut && !runtime->tunnelOut->active() && !payment.holding)
        runtime->tunnelOut.reset();
    if (!payment.waiting)
        return;
    if (!runtime || !runtime->tunnelOut || !runtime->tunnelOut->active()) {
        payment.waiting = false;
        return;
    }
    // The bridge was moved to another channel, perhaps one anyone may read: what is left stays here.
    if (runtime->tunnelOut->hasPending() &&
        (settings.channel != payment.channel || settings.binding != payment.binding || !textReady)) {
        runtime->tunnelOut->finish();
        payment.waiting = false;
        say("✗ O canal da ponte mudou: o pagamento não foi levado inteiro. O token segue com você.");
        return;
    }
    if ((payment.answered && !Throttle::isWithinTimespanMsAt(payment.answeredAt, REFUSAL_GRACE_MS, now)) ||
        !Throttle::isWithinTimespanMsAt(payment.at, TUNNEL_WAIT_MS, now))
        reportPayment();
}

// Datagrams wait behind the bridge's text, share its airtime and are spaced as it is.
bool BitchatBridge::sendTunnel()
{
    TunnelDatagram datagram;
    bool control = false;
    if (auto *queued = runtime->tunnelControl.front()) {
        datagram = *queued;
        control = true;
    } else if (!runtime->tunnelOut || !runtime->tunnelOut->next(datagram)) {
        return false;
    }
    const uint32_t cost = host->airtimeMs(datagram.size);
    if (cost > tokens || !host->sendChannelData(settings.channel, TUNNEL_DATA_TYPE, datagram.data.data(), datagram.size)) {
        // Not sent: a part goes back to the ones still to send.
        if (!control && runtime->tunnelOut)
            runtime->tunnelOut->resend(uint16_t(1u << datagram.data[7]), runtime->tunnelOut->startedAt());
        return false;
    }
    if (control)
        runtime->tunnelControl.pop();
    tokens -= cost;
    return true;
}

bool BitchatBridge::onChannelData(uint8_t channel, uint16_t dataType, const uint8_t *data, size_t size)
{
    if (dataType != TUNNEL_DATA_TYPE || !data || !tunnelDatagram({data, size}))
        return false;
    struct Call {
        BitchatBridge *self;
        uint8_t channel;
        Bytes data;
    } call{this, channel, {data, size}};
    onLargeStack(
        [](void *argument) {
            auto *c = static_cast<Call *>(argument);
            c->self->channelData(c->channel, c->data);
        },
        &call);
    return true;
}

void BitchatBridge::channelData(uint8_t channel, Bytes data)
{
    if (!host || !settings.enabled || !runtime || !wasActive || !identityReady || !utc())
        return;
    refreshChannel();
    if (!textReady || textConflict || channel != settings.channel)
        return;
    const uint32_t now = millis();
    TunnelHeard heard;
    // A frame between phones near different bridges (BridgeRemote.cpp).
    if ((data.data[2] & 0x0f) == uint8_t(TunnelKind::Frame)) {
        tunnelIn(data, now);
        return;
    }
    if (data.data[2] & 0x0f) {
        // An answer about a payment: only the bridge that sent it cares.
        if (readTunnelAnswer(data, heard))
            paymentAnswered(heard, now);
        return;
    }
    if (!allocateBuffer(runtime->tunnelIn)) {
        reject(Result::Full);
        return;
    }
    if (runtime->tunnelIn->accept(data, now, heard) && heard.complete)
        deliverPayment(heard);
    if (runtime->tunnelIn->empty())
        runtime->tunnelIn.reset();
    else
        interval = 0;
}

TunnelOutcome BitchatBridge::checkPayment(const TunnelHeard &heard, uint64_t &amount, char *unit, size_t unitSize, char *payer,
                                          size_t payerSize)
{
    const uint32_t now = millis();
    Packet announced, message;
    if (!heard.announce.size || decode(heard.announce, announced) != Result::Ok || announced.type != 1 ||
        decode(heard.message, message) != Result::Ok || message.type != 2 || message.directed() ||
        message.sender != announced.sender)
        return TunnelOutcome::Invalid;
    Peer *peer = nullptr;
    Result result = runtime->peers->authenticate(crypto, heard.announce, utc(), now, peer);
    if (result == Result::Ok && peer && !peer->announcement.gateway)
        result = runtime->peers->authenticate(crypto, heard.message, utc(), now, peer);
    else if (result == Result::Ok)
        result = Result::Policy;
    if (result == Result::Expired)
        return TunnelOutcome::Late;
    if (result != Result::Ok || !peer)
        return TunnelOutcome::Invalid;
    // The same rule as where it came from: only a token, and only a locked one on a channel anyone may read.
    std::unique_ptr<CashuWork> work;
    Bytes text;
    CashuToken token;
    if (!allocateBuffer(work) || !messageText(message, work->text, sizeof(work->text), text) ||
        findCashu(text, work->scratch, token) != CashuFound::Token || (token.unlocked() && !channelPrivate()))
        return TunnelOutcome::Invalid;
    amount = token.amount;
    snprintf(unit, unitSize, "%s", token.unit);
    char label[LABEL_LIMIT + 8];
    peerLabel(*peer, label, sizeof(label));
    clip(label, 14, payer, payerSize);
    return TunnelOutcome::Delivered;
}

void BitchatBridge::deliverPayment(const TunnelHeard &heard)
{
    const uint32_t now = millis();
    uint64_t amount = 0;
    char unit[13] = "sat", payer[LABEL_LIMIT + 1] = "";
    TunnelOutcome outcome = checkPayment(heard, amount, unit, sizeof(unit), payer, sizeof(payer));
    Digest announceId, messageId;
    if (outcome == TunnelOutcome::Delivered && (packetDigest(crypto, heard.announce, announceId) != Result::Ok ||
                                                packetDigest(crypto, heard.message, messageId) != Result::Ok))
        outcome = TunnelOutcome::Invalid;
    const uint8_t phones = uint8_t(std::min<size_t>(bleListeners(), 255));
    if (outcome == TunnelOutcome::Delivered && !phones)
        outcome = TunnelOutcome::NoPhones;
    if (outcome == TunnelOutcome::Delivered && !runtime->dedup->contains(messageId, now, 125000)) {
        // The announce first: a phone shows a message only from someone it knows.
        for (const Bytes frame : {heard.announce, heard.message}) {
            const Bytes out = relay(frame, runtime->working) == Result::Ok ? runtime->working.bytes() : frame;
            if (!bleBroadcast(out))
                outcome = TunnelOutcome::NoPhones;
        }
        // Phones relay what they hear, so the copies coming back are dropped as any other.
        if (outcome == TunnelOutcome::Delivered) {
            runtime->dedup->insert(announceId, now, 125000);
            runtime->dedup->insert(messageId, now, 125000);
        }
        if (outcome == TunnelOutcome::Delivered) {
            char text[32], line[160];
            amountText(amount, unit, text, sizeof(text));
            snprintf(line, sizeof(line), "%s de %s chegaram pela LoRa. Toque no token para resgatar.", text, payer);
            say(line);
        }
    }
    // Signed with the radio's own MeshCore identity: the payer's radio knows that key from its adverts.
    Digest hash;
    crypto.hash(heard.message, hash);
    // Parts that did not hold together may be someone else's: the true ones can still complete it.
    if (outcome == TunnelOutcome::Invalid && runtime->tunnelIn)
        runtime->tunnelIn->forget(heard.id);
    uint8_t key[8], signature[64], signedBytes[TUNNEL_ACK_SIGNED];
    write32(key, host->nodeNumber());
    std::fill(key + 4, key + 8, 0);
    bool signedAck = host->nodeKeyPrefix(key);
    if (signedAck) {
        tunnelAckSigned(heard.id, outcome, phones, key, hash.data(), signedBytes);
        signedAck = host->signAsNode(signedBytes, sizeof(signedBytes), signature);
    }
    TunnelDatagram ack;
    makeTunnelAck(heard.id, outcome, phones, key, signedAck ? signature : nullptr, ack);
    if (!runtime->tunnelControl.push(ack))
        reject(Result::Full);
    interval = 0;
}

#endif

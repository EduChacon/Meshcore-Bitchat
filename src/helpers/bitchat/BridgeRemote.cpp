// Private chats between phones near different bridges. With the tunnel on (the operator's choice, off by default),
// the bridge takes the announce of each phone that talks to it directly over the LoRa channel, every three minutes
// at most, so the phones near the other bridges list it; and it takes over LoRa the Noise packets a phone here
// addresses to a phone known that way. Coming back, those packets are given to the phones here. The bridges never
// read them: they are the apps' own end-to-end encrypted packets. Each part costs airtime on the shared channel.
#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_system.h>

using namespace bitchat;

static constexpr uint32_t PRESENCE_MS = 3UL * 60 * 1000;
static constexpr uint32_t LOCAL_MS = 3UL * 60 * 1000;
static constexpr uint32_t REMOTE_DEDUP_MS = 125000;

void BitchatBridge::tunnelOut(const Packet &packet, Peer &peer, Bytes relayed, uint32_t now)
{
    if (!tunnelOn || !textReady || textConflict || !runtime || peer.announcement.gateway || peer.remote)
        return;
    if (packet.type == 1) {
        // A phone here, announcing itself to this radio: its presence on the other side.
        if (packet.ttl != 7 || (peer.tunneledAt && Throttle::isWithinTimespanMsAt(peer.tunneledAt, PRESENCE_MS, now)))
            return;
        if (queueFrame(relayed, now))
            peer.tunneledAt = now ? now : 1;
        return;
    }
    if ((packet.type != 0x10 && packet.type != 0x11) || !packet.directed() || packet.recipient == ownId)
        return;
    const Peer *to = runtime->peers->find(packet.recipient);
    if (!to || !to->remote)
        return;
    if (!peer.frames.admit(now, 6, 2000)) {
        reject(Result::Full);
        return;
    }
    // The other side drops what comes from a phone it does not know: the announce goes first when it is due.
    if (!peer.tunneledAt || !Throttle::isWithinTimespanMsAt(peer.tunneledAt, PRESENCE_MS, now)) {
        for (const auto &kept : runtime->announced)
            if (kept.size && kept.peer == peer.id) {
                Frame announce;
                if (relay({kept.frame.data(), kept.size}, announce) == Result::Ok && queueFrame(announce.bytes(), now))
                    peer.tunneledAt = now ? now : 1;
                break;
            }
    }
    queueFrame(relayed, now);
}

bool BitchatBridge::queueFrame(Bytes frame, uint32_t now)
{
    Packet packet;
    if (decode(frame, packet) != Result::Ok)
        return false;
    // Without its padding: the other bridge pads it again.
    const Bytes natural{frame.data, packet.naturalSize};
    const size_t count = tunnelFrameParts(natural.size);
    if (!natural.size || natural.size > TUNNEL_FRAME) {
        reject(Result::Oversize);
        return false;
    }
    if (!allocateBuffer(runtime->frames))
        return false;
    auto &frames = *runtime->frames;
    if (frames.out.size() + count > decltype(frames.out)::capacity()) {
        reject(Result::Full);
        return false;
    }
    uint32_t id = 0;
    esp_fill_random(&id, sizeof(id));
    id ^= ++frames.nextId;
    for (size_t i = 0; i < count; ++i) {
        Runtime::Frames::Part part;
        if (!makeTunnelFrame(id, natural, uint8_t(i), part.datagram))
            return false;
        part.queuedAt = now;
        frames.out.push(part);
    }
    interval = 0;
    return true;
}

bool BitchatBridge::sendFrame()
{
    auto *part = runtime->frames->out.front();
    if (!part)
        return false;
    // Stale parts are of no use: the apps gave up the handshake or sent again.
    if (!Throttle::isWithinTimespanMsAt(part->queuedAt, TUNNEL_FRAME_WAIT_MS, millis()) || !textReady || textConflict) {
        runtime->frames->out.pop();
        reject(Result::Expired);
        ++lost;
        return false;
    }
    const uint32_t cost = host->airtimeMs(part->datagram.size);
    if (cost > tokens)
        return false;
    if (!host->sendChannelData(settings.channel, TUNNEL_DATA_TYPE, part->datagram.data.data(), part->datagram.size)) {
        reject(Result::Full);
        return false;
    }
    tokens -= cost;
    runtime->frames->out.pop();
    ++submittedRadio;
    return true;
}

void BitchatBridge::tunnelIn(Bytes data, uint32_t now)
{
    if (!tunnelOn || !allocateBuffer(runtime->frames))
        return;
    Bytes natural;
    if (!runtime->frames->in.accept(data, now, natural))
        return;
    Packet packet;
    if (decode(natural, packet) != Result::Ok || packet.sender == ownId || packet.replay() ||
        (packet.type != 1 && packet.type != 0x10 && packet.type != 0x11)) {
        reject(Result::Policy);
        return;
    }
    // The Noise types padded again, as the apps' stream reader expects them.
    Frame &full = runtime->assembled;
    if (natural.size > full.data.size())
        return;
    std::memmove(full.data.data(), natural.data, natural.size);
    full.size = natural.size;
    if (packet.type != 1) {
        const size_t padded = paddedSize(natural.size);
        if (padded > full.data.size())
            return;
        std::fill(full.data.begin() + natural.size, full.data.begin() + padded, uint8_t(padded - natural.size));
        full.size = padded;
    }
    Digest id;
    Result result = packetDigest(crypto, full.bytes(), id);
    if (result == Result::Ok && runtime->dedup->contains(id, now, REMOTE_DEDUP_MS))
        result = Result::Duplicate;
    Peer *peer = nullptr;
    if (result == Result::Ok)
        result = runtime->peers->authenticate(crypto, full.bytes(), utc(), now, peer);
    if (result == Result::Ok && !peer)
        result = Result::Unauthenticated;
    if (result == Result::Ok)
        result = runtime->dedup->insert(id, now, REMOTE_DEDUP_MS);
    if (result != Result::Ok) {
        reject(result);
        return;
    }
    if (packet.type == 1) {
        // Another bridge's announce is never tunneled; a phone heard here directly is not remote.
        if (peer->announcement.gateway)
            return;
        if (!peer->localAt || !Throttle::isWithinTimespanMsAt(peer->localAt, LOCAL_MS, now))
            peer->remote = true;
    } else {
        // Only for a phone here, and never for this bridge: its own private chat is with phones nearby.
        const Peer *to = packet.directed() ? runtime->peers->find(packet.recipient) : nullptr;
        if (!to || to->remote || packet.recipient == ownId)
            return;
    }
    if (bleBroadcast(full.bytes()))
        ++forwardedBle;
}
#endif

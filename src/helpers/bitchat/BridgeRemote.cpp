// Private chats between phones near different bridges. With the tunnel on (the operator's choice, off by default),
// the bridge takes over the LoRa channel the announce of each phone that talks to it directly, so the phones near the
// other bridges list it, and the Noise packets such a phone addresses to a phone known that way. Coming back, those
// packets are given to the phones here. The bridges never read them: they are the apps' own end-to-end encrypted
// packets. What does go over the channel, readable by anyone with its key, is each phone's announce: nickname and
// public keys. Every part costs airtime on the shared channel.
#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_system.h>

using namespace bitchat;

// A phone's presence on the other side: every 3 minutes while another tunnel bridge was heard in the last half hour,
// else every 30 minutes, so two bridges find each other without the channel paying for it all day.
static constexpr uint32_t PRESENCE_MS = 3UL * 60 * 1000;
static constexpr uint32_t PRESENCE_ALONE_MS = 30UL * 60 * 1000;
static constexpr uint32_t PARTNER_MS = 30UL * 60 * 1000;
static constexpr uint32_t LOCAL_MS = 3UL * 60 * 1000;
// A phone's announce goes again before its private packets only when the other side may have dropped it (its
// table forgets a phone after five minutes): an extra part costs seconds the app's handshake does not have.
static constexpr uint32_t ANNOUNCE_AGAIN_MS = 4UL * 60 * 1000;
static constexpr uint32_t REMOTE_DEDUP_MS = 125000;
// Phones known through the tunnel take at most this many places in the table of phones.
static constexpr size_t REMOTE_PEERS = 6;

void BitchatBridge::tunnelOut(const Packet &packet, Peer &peer, Bytes relayed, uint16_t source, uint32_t now)
{
    // Not held back by a standby for the public text: these frames are each phone's own, and a chat needs both
    // bridges to carry its side.
    if (!tunnelOn || !textReady || !runtime || peer.announcement.gateway || peer.remote)
        return;
    const bool partner =
        runtime->frames && runtime->frames->heardAt && Throttle::isWithinTimespanMsAt(runtime->frames->heardAt, PARTNER_MS, now);
    if (packet.type == 1) {
        // A phone here, announcing itself to this radio: its presence on the other side.
        if (packet.ttl != 7 ||
            (peer.tunneledAt && Throttle::isWithinTimespanMsAt(peer.tunneledAt, partner ? PRESENCE_MS : PRESENCE_ALONE_MS, now)))
            return;
        if (queueFrame(relayed, now))
            peer.tunneledAt = now ? now : 1;
        return;
    }
    if ((packet.type != 0x10 && packet.type != 0x11) || !packet.directed() || packet.recipient == ownId)
        return;
    // Only the phone's own packets, from the link it announced itself on: anyone may write any sender ID.
    if (packet.ttl != 7 || source == NO_LINK || source != peer.link)
        return;
    const Peer *to = runtime->peers->find(packet.recipient);
    if (!to || !to->remote)
        return;
    if (!allocateBuffer(runtime->frames))
        return;
    RateBucket probe = runtime->frames->budget;
    if (!peer.frames.admit(now, 6, 2000) || !probe.admit(now, 12, 3000)) {
        reject(Result::Full);
        return;
    }
    runtime->frames->budget.admit(now, 12, 3000);
    // The other side drops what comes from a phone it does not know: the announce goes first when it is due.
    if (!peer.tunneledAt || !Throttle::isWithinTimespanMsAt(peer.tunneledAt, ANNOUNCE_AGAIN_MS, now)) {
        for (const auto &kept : runtime->announced)
            if (kept.size && kept.size <= kept.frame.size() && kept.peer == peer.id) {
                std::array<uint8_t, sizeof(kept.frame)> announce;
                std::copy_n(kept.frame.begin(), kept.size, announce.begin());
                // a hop older, as relayed
                if (announce[2] > 1) {
                    --announce[2];
                    if (queueFrame({announce.data(), kept.size}, now, true))
                        peer.tunneledAt = now ? now : 1;
                }
                break;
            }
    }
    queueFrame(relayed, now, packet.type == 0x10);
}

bool BitchatBridge::queueFrame(Bytes frame, uint32_t now, bool urgent)
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
    auto &queue = urgent ? frames.urgent : frames.out;
    if (queue.size() + count > queue.capacity()) {
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
        queue.push(part);
    }
    interval = 0;
    return true;
}

bool BitchatBridge::sendFrame()
{
    auto &frames = *runtime->frames;
    const uint32_t now = millis();
    // Stale parts are of no use: the apps gave up the handshake or sent again. All of them go at once.
    for (auto *queue : {&frames.urgent, &frames.out})
        while (auto *part = queue->front()) {
            if (Throttle::isWithinTimespanMsAt(part->queuedAt, TUNNEL_FRAME_WAIT_MS, now) && textReady)
                break;
            queue->pop();
            reject(Result::Expired);
            ++lost;
        }
    auto &queue = frames.urgent.size() ? frames.urgent : frames.out;
    auto *part = queue.front();
    if (!part)
        return false;
    const uint32_t cost = host->airtimeMs(part->datagram.size);
    if (cost > tokens)
        return false;
    if (!host->sendChannelData(settings.channel, TUNNEL_DATA_TYPE, part->datagram.data.data(), part->datagram.size)) {
        reject(Result::Full);
        return false;
    }
    tokens -= cost;
    queue.pop();
    ++submittedRadio;
    ++tunnelSent;
    return true;
}

void BitchatBridge::tunnelIn(Bytes data, uint32_t now)
{
    if (!tunnelOn || !allocateBuffer(runtime->frames))
        return;
    Bytes natural;
    uint32_t frameId = 0;
    if (!runtime->frames->in.accept(data, now, natural, &frameId))
        return;
    Packet packet;
    if (decode(natural, packet) != Result::Ok || packet.sender == ownId || packet.replay() ||
        (packet.type != 1 && packet.type != 0x10 && packet.type != 0x11)) {
        runtime->frames->in.forget(frameId);
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
    // A phone not known here takes one of the few places for remote phones: the oldest of those gives way, never a
    // phone here.
    if (result == Result::Ok && packet.type == 1 && !runtime->peers->find(packet.sender) &&
        runtime->peers->remoteCount() >= REMOTE_PEERS)
        runtime->peers->dropOldestRemote();
    Peer *peer = nullptr;
    if (result == Result::Ok)
        result = runtime->peers->authenticate(crypto, full.bytes(), utc(), now, peer);
    if (result == Result::Ok && !peer)
        result = Result::Unauthenticated;
    if (result == Result::Ok)
        result = runtime->dedup->insert(id, now, REMOTE_DEDUP_MS);
    if (result != Result::Ok) {
        if (result != Result::Duplicate)
            runtime->frames->in.forget(frameId);
        reject(result);
        return;
    }
    // Another tunnel bridge is out there: presence goes at the shorter interval.
    runtime->frames->heardAt = now ? now : 1;
    if (packet.type == 1) {
        // Another bridge's announce is never tunneled; a phone heard here directly is not remote.
        if (peer->announcement.gateway)
            return;
        if (!peer->localAt || !Throttle::isWithinTimespanMsAt(peer->localAt, LOCAL_MS, now))
            peer->remote = true;
        if (!peer->remote)
            return;
    } else {
        // Only from a phone known as remote, only for a phone here, and never for this bridge: its own private
        // chat is with phones nearby.
        const Peer *to = packet.directed() ? runtime->peers->find(packet.recipient) : nullptr;
        if (!peer->remote || !to || to->remote || packet.recipient == ownId)
            return;
    }
    ++tunnelReceived;
    if (bleBroadcast(full.bytes()))
        ++forwardedBle;
}
#endif

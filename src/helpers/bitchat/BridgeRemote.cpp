// Private chats between phones near different bridges. With the tunnel on (the default; the owner may turn it
// off), the bridge takes over the LoRa channel the announce of each phone that talks to it directly, so the phones
// near the other bridges list it, and the Noise packets such a phone addresses to a phone known that way. Coming
// back, those packets are given to the phones here. The bridges never read them: they are the apps' own end-to-end
// encrypted packets. What does go over the channel, readable by anyone with its key, is each phone's announce:
// nickname and public keys. Every part costs airtime on the shared channel.
#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_system.h>

using namespace bitchat;

// A phone's presence on the other side. The Bitchat app drops a relayed phone from its list 60 s after its last
// announce, and sends nothing to a phone it does not list. While phones are near both bridges (the other one sent
// something in the last 3 minutes), the bridge itself sends the latest announce of its first phones every 40 s,
// those in a chat first, as many as the airtime allows (one at the Brazilian settings, two at most, none at the
// slowest). Otherwise each phone's announce goes when it
// announces itself: every 3 minutes while another tunnel bridge was heard in the last half hour, else every 30
// minutes, so two bridges find each other without the channel paying for it all day. With no phone near, nothing.
static constexpr uint32_t FAST_PRESENCE_MS = 40000;
static constexpr uint32_t ACTIVE_PARTNER_MS = 3UL * 60 * 1000;
static constexpr size_t FAST_PHONES = 2;
// A phone that stopped announcing (it announces every 15 to 40 s) gives its fast place to the next one.
static constexpr uint32_t FAST_LOCAL_MS = 90000;
// A phone whose private packets crossed in the last minutes goes before the others at the fast rhythm.
static constexpr uint32_t CHATTING_MS = 5UL * 60 * 1000;
// A kept announce older than this is not sent: the other bridge refuses one over two minutes old.
static constexpr uint32_t FRESH_ANNOUNCE_MS = 60000;
// A routine presence leaves this much of the airtime budget to the chats themselves, their handshakes first.
static constexpr uint32_t PRESENCE_RESERVE_MS = 4000;
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

// A remembered announce, a hop older as relayed, queued for the other side.
bool BitchatBridge::relayedCopy(const Runtime::Announced &kept, std::array<uint8_t, sizeof(Runtime::Announced::frame)> &out)
{
    if (!kept.size || kept.size > kept.frame.size() || kept.frame[2] <= 1)
        return false;
    std::copy_n(kept.frame.begin(), kept.size, out.begin());
    --out[2];
    return true;
}

bool BitchatBridge::partnerActive(uint32_t now) const
{
    const uint32_t heardAt = runtime && runtime->frames ? runtime->frames->heardAt : 0;
    return heardAt && Throttle::isWithinTimespanMsAt(heardAt, ACTIVE_PARTNER_MS, now);
}

void BitchatBridge::tunnelOut(const Packet &packet, Peer &peer, Bytes relayed, uint16_t source, uint32_t now)
{
    // Not held back by a standby for the public text: these frames are each phone's own, and a chat needs both
    // bridges to carry its side.
    if (!tunnelOn || !textReady || !runtime || peer.announcement.gateway || peer.remote)
        return;
    const uint32_t heardAt = runtime->frames ? runtime->frames->heardAt : 0;
    const bool partner = heardAt && Throttle::isWithinTimespanMsAt(heardAt, PARTNER_MS, now);
    if (packet.type == 1) {
        // A phone here, announcing itself to this radio: its presence on the other side, unless one is on its way
        // or the fast rhythm (tunnelPresence) takes care of it.
        if (packet.ttl != 7)
            return;
        const bool fast = partnerActive(now) && fastPresence(peer, now);
        if (fast)
            keepLatest(peer, relayed, now);
        if (peer.presenceQueued || peer.announceQueued)
            return;
        const uint32_t every = fast ? FAST_PRESENCE_MS : partner ? PRESENCE_MS : PRESENCE_ALONE_MS;
        if (peer.tunneledAt && Throttle::isWithinTimespanMsAt(peer.tunneledAt, every, now))
            return;
        if (queueFrame(relayed, now, false, true, fast))
            peer.presenceQueued = true;
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
    // The other side drops what comes from a phone it does not know: the announce goes first, ahead of everything,
    // when none went lately (a presence still waiting in its queue does not count).
    if (!peer.announceQueued && (!peer.tunneledAt || !Throttle::isWithinTimespanMsAt(peer.tunneledAt, ANNOUNCE_AGAIN_MS, now))) {
        // Only a recent one: the other side refuses an announce over two minutes old.
        bool queued = false, tried = false;
        for (const auto &kept : runtime->announced)
            if (kept.size && kept.peer == peer.id && Throttle::isWithinTimespanMsAt(kept.at, FRESH_ANNOUNCE_MS, now)) {
                tried = true;
                std::array<uint8_t, sizeof(kept.frame)> announce;
                queued = relayedCopy(kept, announce) && queueFrame({announce.data(), kept.size}, now, true);
                break;
            }
        if (!tried)
            for (const auto &latest : runtime->frames->latest)
                if (latest.size && latest.peer == peer.id && Throttle::isWithinTimespanMsAt(latest.at, FRESH_ANNOUNCE_MS, now)) {
                    queued = queueFrame({latest.frame.data(), latest.size}, now, true);
                    break;
                }
        if (queued)
            peer.announceQueued = true;
    }
    if (queueFrame(relayed, now, packet.type == 0x10))
        peer.chattedAt = now ? now : 1;
}

// The fast rhythm: the latest announce of the first phones here goes every 40 s while phones are near both bridges,
// whenever the phone announced itself since the last one went (the app ignores the same announce heard again).
void BitchatBridge::tunnelPresence(uint32_t now)
{
    if (!tunnelOn || !textReady || !runtime || !runtime->frames || !partnerActive(now))
        return;
    for (const auto &latest : runtime->frames->latest) {
        if (!latest.size)
            continue;
        Peer *peer = runtime->peers->find(latest.peer);
        if (!peer || peer->remote || peer->announcement.gateway || peer->presenceQueued || peer->announceQueued ||
            !fastPresence(*peer, now))
            continue;
        // Ages, not times, so the comparison holds when the clock wraps.
        if (!Throttle::isWithinTimespanMsAt(latest.at, FRESH_ANNOUNCE_MS, now) ||
            (peer->tunneledAt && (Throttle::isWithinTimespanMsAt(peer->tunneledAt, FAST_PRESENCE_MS, now) ||
                                  now - latest.at >= now - peer->tunneledAt)))
            continue;
        if (queueFrame({latest.frame.data(), latest.size}, now, false, true, true))
            peer->presenceQueued = true;
    }
}

// The latest announce of a phone at the fast rhythm, a hop older: in its own place, or in the place of a phone no
// longer at that rhythm, or of the one kept longest ago.
void BitchatBridge::keepLatest(const Peer &peer, Bytes relayed, uint32_t now)
{
    Packet packet;
    if (!allocateBuffer(runtime->frames) || decode(relayed, packet) != Result::Ok || packet.naturalSize > TUNNEL_FRAME)
        return;
    auto &latest = runtime->frames->latest;
    Runtime::Frames::Latest *own = nullptr, *free = nullptr, *oldest = nullptr;
    for (auto &entry : latest) {
        if (entry.size && entry.peer == peer.id)
            own = &entry;
        const Peer *other = entry.size ? runtime->peers->find(entry.peer) : nullptr;
        if (!other || !fastPresence(*other, now))
            free = free ? free : &entry;
        else if (!oldest || now - entry.at > now - oldest->at)
            oldest = &entry;
    }
    auto *slot = own ? own : free ? free : oldest;
    slot->peer = peer.id;
    slot->at = now;
    slot->size = uint16_t(packet.naturalSize);
    std::copy_n(relayed.data, packet.naturalSize, slot->frame.begin());
}

// How many phones the fast rhythm can carry: a presence (two parts) every 40 s may take about 70% of the budget the
// airtime limit gives back in that time, two phones at most; one when a presence fits in what comes back (the
// Brazilian settings), and none at slower settings, where it could not keep the rhythm anyway.
size_t BitchatBridge::fastSlots() const
{
    const uint32_t presence = 2 * host->airtimeMs(TUNNEL_DATAGRAM);
    const uint32_t refill = FAST_PRESENCE_MS / 10;
    if (!presence)
        return FAST_PHONES;
    const size_t slots = std::min<size_t>(refill * 7 / 10 / presence, FAST_PHONES);
    return slots ? slots : presence <= refill ? 1 : 0;
}

// One of the first phones here among those connected to this bridge and announcing now, those in a chat over the
// tunnel first, then by ID: the few whose presence goes at the fast rhythm, so a full house does not take the
// channel.
bool BitchatBridge::fastPresence(const Peer &peer, uint32_t now) const
{
    const auto local = [now](const Peer &p) {
        return p.used && !p.remote && !p.announcement.gateway && p.link != NO_LINK && p.localAt &&
               Throttle::isWithinTimespanMsAt(p.localAt, FAST_LOCAL_MS, now);
    };
    const auto chatting = [now](const Peer &p) {
        return p.chattedAt && Throttle::isWithinTimespanMsAt(p.chattedAt, CHATTING_MS, now);
    };
    const size_t slots = fastSlots();
    if (!slots || !local(peer))
        return false;
    const bool talks = chatting(peer);
    size_t before = 0;
    for (size_t i = 0; i < PeerTable::CAPACITY; ++i) {
        const Peer &other = runtime->peers->slot(i);
        if (&other == &peer || !local(other))
            continue;
        const bool otherTalks = chatting(other);
        if ((otherTalks && !talks) || (otherTalks == talks && std::lexicographical_compare(other.id.begin(), other.id.end(),
                                                                                           peer.id.begin(), peer.id.end())))
            ++before;
    }
    return before < slots;
}

// An announce of this phone went whole over LoRa: nothing of it waits any more. Or one of its parts was dropped:
// that one, a routine presence or the urgent copy, no longer waits.
void BitchatBridge::announceDone(const PeerId &id, bool sent, bool presence, uint32_t now)
{
    Peer *peer = runtime->peers->find(id);
    if (!peer)
        return;
    if (sent) {
        peer->presenceQueued = peer->announceQueued = false;
        peer->tunneledAt = now ? now : 1;
    } else if (presence) {
        peer->presenceQueued = false;
    } else {
        peer->announceQueued = false;
    }
}

void BitchatBridge::clearAnnounceMarks()
{
    if (!runtime || !runtime->peers)
        return;
    for (size_t i = 0; i < PeerTable::CAPACITY; ++i) {
        Peer &peer = runtime->peers->slot(i);
        peer.presenceQueued = peer.announceQueued = false;
    }
}

bool BitchatBridge::queueFrame(Bytes frame, uint32_t now, bool urgent, bool presence, bool fast)
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
    auto &queue = urgent ? frames.urgent : !presence ? frames.out : fast ? frames.fast : frames.presence;
    if (queue.size() + count > queue.capacity()) {
        // A presence finds room on a later announce: no failure counted for it.
        if (!presence)
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
        part.presence = presence;
        part.announce = packet.type == 1;
        part.peer = packet.sender;
        part.index = uint8_t(i);
        part.count = uint8_t(count);
        queue.push(part);
    }
    interval = 0;
    return true;
}

// Whether a tunnel part can go now. Stale parts are dropped first (the apps gave up the handshake or sent again),
// and so are the parts of an announce of a phone whose announce went since it was queued.
bool BitchatBridge::frameReady(uint32_t now)
{
    if (!runtime || !runtime->frames)
        return false;
    auto &frames = *runtime->frames;
    for (auto *queue : {&frames.urgent, &frames.out, &frames.fast, &frames.presence})
        while (auto *part = queue->front()) {
            const bool stale = !Throttle::isWithinTimespanMsAt(part->queuedAt, TUNNEL_FRAME_WAIT_MS, now) || !textReady;
            const Peer *peer = part->announce ? runtime->peers->find(part->peer) : nullptr;
            // Its announce went since this part was queued (ages, not times, so the clock may wrap).
            const bool superseded = peer && peer->tunneledAt && now - peer->tunneledAt <= now - part->queuedAt;
            if (!stale && !superseded)
                break;
            const auto gone = *part;
            queue->pop();
            // A routine presence that waited for room is not a lost message.
            if (stale && !gone.presence) {
                reject(Result::Expired);
                ++lost;
            }
            if (gone.announce)
                announceDone(gone.peer, false, gone.presence, now);
        }
    auto &queue = frames.urgent.size() ? frames.urgent
                  : frames.out.size()  ? frames.out
                  : frames.fast.size() ? frames.fast
                                       : frames.presence;
    const auto *part = queue.front();
    if (!part)
        return false;
    const uint32_t cost = host->airtimeMs(part->datagram.size);
    // A routine presence starts only with room for all its parts and the reserve (at slow settings, where that is
    // more than the whole budget, with the budget full); once started, it goes on.
    const uint32_t start = std::min(cost * part->count + PRESENCE_RESERVE_MS, BRIDGE_BUDGET_MS);
    return cost <= tokens && !(part->presence && part->index == 0 && start > tokens);
}

bool BitchatBridge::sendFrame()
{
    const uint32_t now = millis();
    if (!frameReady(now))
        return false;
    auto &frames = *runtime->frames;
    auto &queue = frames.urgent.size() ? frames.urgent
                  : frames.out.size()  ? frames.out
                  : frames.fast.size() ? frames.fast
                                       : frames.presence;
    const auto part = *queue.front();
    if (!host->sendChannelData(settings.channel, TUNNEL_DATA_TYPE, part.datagram.data.data(), part.datagram.size)) {
        reject(Result::Full);
        return false;
    }
    tokens -= host->airtimeMs(part.datagram.size);
    queue.pop();
    ++submittedRadio;
    ++tunnelSent;
    if (part.announce && part.index + 1 == part.count)
        announceDone(part.peer, true, part.presence, now);
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
        Peer *to = packet.directed() ? runtime->peers->find(packet.recipient) : nullptr;
        if (!peer->remote || !to || to->remote || packet.recipient == ownId)
            return;
        to->chattedAt = now ? now : 1;
    }
    // Another tunnel bridge with phones near it is out there (a copy of a phone heard here too does not count), even
    // when no phone here is connected right now.
    runtime->frames->heardAt = now ? now : 1;
    ++tunnelReceived;
    if (bleBroadcast(full.bytes()))
        ++forwardedBle;
}
#endif

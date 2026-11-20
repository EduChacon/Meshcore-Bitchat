#include "BridgeTunnel.h"

#include "BridgeCompat.h"
#include <algorithm>
#include <cstring>

namespace bitchat
{

static void header(TunnelKind kind, uint32_t id, TunnelDatagram &out)
{
    out.data[0] = 'B';
    out.data[1] = 'C';
    out.data[2] = uint8_t(TUNNEL_VERSION << 4 | uint8_t(kind));
    write32(out.data.data() + 3, id);
}

bool tunnelDatagram(Bytes data)
{
    return data.size >= 7 && data.size <= TUNNEL_DATAGRAM && data.data[0] == 'B' && data.data[1] == 'C' &&
           (data.data[2] >> 4) == TUNNEL_VERSION;
}

size_t tunnelParts(size_t bundleSize)
{
    return (bundleSize + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
}

bool makeTunnelAck(uint32_t id, TunnelOutcome outcome, uint8_t phones, uint32_t node, TunnelDatagram &out)
{
    header(TunnelKind::Ack, id, out);
    out.data[7] = uint8_t(outcome);
    out.data[8] = phones;
    write32(out.data.data() + 9, node);
    out.size = 13;
    return true;
}

bool makeTunnelWant(uint32_t id, uint16_t missing, TunnelDatagram &out)
{
    header(TunnelKind::Want, id, out);
    write16(out.data.data() + 7, missing);
    out.size = 9;
    return true;
}

bool TunnelSender::start(uint32_t id, Bytes announce, Bytes message, uint32_t now)
{
    const size_t total = 4 + announce.size + message.size;
    if (used || !announce.size || !message.size || announce.size > 0xffff || message.size > 0xffff || total > TUNNEL_BUNDLE)
        return false;
    write16(bundle.data(), uint16_t(announce.size));
    memcpy(bundle.data() + 2, announce.data, announce.size);
    write16(bundle.data() + 2 + announce.size, uint16_t(message.size));
    memcpy(bundle.data() + 4 + announce.size, message.data, message.size);
    size = total;
    parts = uint8_t(tunnelParts(total));
    pending = uint16_t((1u << parts) - 1);
    bundleId = id;
    started = now;
    used = true;
    return true;
}

bool TunnelSender::next(TunnelDatagram &out)
{
    if (!used || !pending)
        return false;
    uint8_t index = 0;
    while (!(pending & (1u << index)))
        ++index;
    pending &= uint16_t(~(1u << index));
    const size_t offset = index * TUNNEL_CHUNK;
    const size_t length = std::min(TUNNEL_CHUNK, size - offset);
    header(TunnelKind::Data, bundleId, out);
    out.data[7] = index;
    out.data[8] = parts;
    memcpy(out.data.data() + TUNNEL_HEADER, bundle.data() + offset, length);
    out.size = TUNNEL_HEADER + length;
    return true;
}

void TunnelSender::resend(uint16_t missing, uint32_t now)
{
    // Parts that would arrive after the phones stop taking the frame are not worth the airtime.
    if (used && Throttle::isWithinTimespanMsAt(started, TUNNEL_WAIT_MS - 20000, now))
        pending |= uint16_t(missing & ((1u << parts) - 1));
}

bool readTunnelAnswer(Bytes data, TunnelHeard &out)
{
    out = {};
    if (!tunnelDatagram(data))
        return false;
    out.kind = TunnelKind(data.data[2] & 0x0f);
    out.id = read32(data.data + 3);
    if (out.kind == TunnelKind::Ack) {
        if (data.size < 13 || data.data[7] > uint8_t(TunnelOutcome::Invalid))
            return false;
        out.outcome = TunnelOutcome(data.data[7]);
        out.phones = data.data[8];
        out.node = read32(data.data + 9);
        return true;
    }
    if (out.kind == TunnelKind::Want && data.size >= 9) {
        out.missing = read16(data.data + 7);
        return out.missing != 0;
    }
    return false;
}

bool TunnelReceiver::accept(Bytes data, uint32_t now, TunnelHeard &out)
{
    out = {};
    if (!tunnelDatagram(data))
        return false;
    if ((data.data[2] & 0x0f) != uint8_t(TunnelKind::Data))
        return readTunnelAnswer(data, out);
    out.id = read32(data.data + 3);
    if (data.size <= TUNNEL_HEADER)
        return false;
    const uint8_t index = data.data[7], count = data.data[8];
    const size_t length = data.size - TUNNEL_HEADER;
    // Every part but the last fills its chunk, so the bundle size follows from the last one.
    if (!count || count > TUNNEL_PARTS || index >= count || (index + 1 < count && length != TUNNEL_CHUNK))
        return false;
    expire(now);
    Slot *slot = nullptr;
    for (auto &s : slots)
        if (s.used && s.id == out.id && s.count == count)
            slot = &s;
    if (!slot) {
        for (auto &s : slots)
            if (!s.used) {
                slot = &s;
                break;
            }
        if (!slot) {
            // Both busy: the older gives way, as it is the likelier to fail anyway.
            slot = (now - slots[0].firstAt) >= (now - slots[1].firstAt) ? &slots[0] : &slots[1];
        }
        *slot = {};
        slot->used = true;
        slot->id = out.id;
        slot->count = count;
        slot->firstAt = now;
    }
    if (slot->done || (slot->have & (1u << index)))
        return false;
    memcpy(slot->data.data() + index * TUNNEL_CHUNK, data.data + TUNNEL_HEADER, length);
    slot->have |= uint16_t(1u << index);
    slot->lastAt = now;
    if (index + 1 == count)
        slot->size = uint16_t(index * TUNNEL_CHUNK + length);
    const uint16_t all = uint16_t((1u << count) - 1);
    if (slot->have != all)
        return false;
    slot->done = true;
    // Whole: the sizes inside must account for every byte, or the bundle is reported as invalid (empty).
    out.complete = true;
    const uint8_t *b = slot->data.data();
    const size_t announce = read16(b);
    if (announce == 0 || 4 + announce > slot->size)
        return true;
    const size_t message = read16(b + 2 + announce);
    if (message == 0 || 4 + announce + message != slot->size)
        return true;
    out.announce = {b + 2, announce};
    out.message = {b + 4 + announce, message};
    return true;
}

bool TunnelReceiver::want(uint32_t now, TunnelDatagram &out)
{
    for (auto &s : slots) {
        if (!s.used || s.done || s.wants >= TUNNEL_WANTS || Throttle::isWithinTimespanMsAt(s.lastAt, TUNNEL_QUIET_MS, now))
            continue;
        ++s.wants;
        s.lastAt = now;
        return makeTunnelWant(s.id, uint16_t(~s.have & ((1u << s.count) - 1)), out);
    }
    return false;
}

void TunnelReceiver::expire(uint32_t now)
{
    for (auto &s : slots)
        if (s.used && !Throttle::isWithinTimespanMsAt(s.firstAt, TUNNEL_WAIT_MS + 20000, now))
            s = {};
}

bool TunnelReceiver::empty() const
{
    return !slots[0].used && !slots[1].used;
}

} // namespace bitchat

#pragma once

#include "BridgeCodec.h"
#include "BridgeCompat.h"
#include <algorithm>
#include <array>
#include <cstring>

// People the bridge knows at once; a board short of memory can ask for fewer.
#ifndef BITCHAT_PEER_ENTRIES
#define BITCHAT_PEER_ENTRIES 24
#endif

namespace bitchat
{

template <typename T, size_t Capacity> class BoundedQueue
{
  public:
    bool push(const T &value)
    {
        if (count == Capacity)
            return false;
        entries[(head + count) % Capacity] = value;
        ++count;
        return true;
    }
    T *front() { return count ? &entries[head] : nullptr; }
    void pop()
    {
        if (count) {
            head = (head + 1) % Capacity;
            --count;
        }
    }
    size_t size() const { return count; }
    static constexpr size_t capacity() { return Capacity; }
    void clear() { head = count = 0; }

  private:
    std::array<T, Capacity> entries{};
    size_t head = 0;
    size_t count = 0;
};

class RateGate
{
  public:
    bool admit(uint32_t now, uint32_t interval)
    {
        if (used && Throttle::isWithinTimespanMsAt(last, interval, now))
            return false;
        used = true;
        last = now;
        return true;
    }

  private:
    uint32_t last = 0;
    bool used = false;
};

// Token bucket: a short burst, then one admission per refill interval.
class RateBucket
{
  public:
    bool admit(uint32_t now, uint8_t burst, uint32_t refillMs)
    {
        const uint32_t gained = used ? (now - last) / refillMs : burst;
        if (gained) {
            tokens = uint8_t(std::min<uint32_t>(burst, tokens + gained));
            last = (used && tokens < burst) ? last + gained * refillMs : now;
            used = true;
        }
        if (!tokens)
            return false;
        --tokens;
        return true;
    }

  private:
    uint32_t last = 0;
    uint8_t tokens = 0;
    bool used = false;
};

template <size_t Capacity> class DedupCache
{
  public:
    bool contains(const Digest &digest, uint32_t now, uint32_t lifetime) const
    {
        for (const auto &entry : entries)
            if (entry.used && entry.digest == digest && Throttle::isWithinTimespanMsAt(entry.at, lifetime, now))
                return true;
        return false;
    }
    Result insert(const Digest &digest, uint32_t now, uint32_t lifetime)
    {
        if (contains(digest, now, lifetime))
            return Result::Duplicate;
        for (size_t i = 0; i < Capacity; ++i) {
            auto &entry = entries[(next + i) % Capacity];
            if (!entry.used || !Throttle::isWithinTimespanMsAt(entry.at, lifetime, now)) {
                entry = {digest, now, true};
                next = (next + i + 1) % Capacity;
                return Result::Ok;
            }
        }
        return Result::Full;
    }

  private:
    struct Entry {
        Digest digest{};
        uint32_t at = 0;
        bool used = false;
    };
    std::array<Entry, Capacity> entries{};
    size_t next = 0;
};

struct Peer {
    PeerId id{};
    Announcement announcement{};
    uint32_t seenAt = 0;
    bool used = false;
    // Known through another bridge's LoRa tunnel, not heard here directly: its private packets go over LoRa.
    bool remote = false;
    // When it last announced itself directly to this radio, and when that announce last went over LoRa.
    uint32_t localAt = 0, tunneledAt = 0;
    // Its announce waits to go over LoRa: as a routine presence, or urgently ahead of its private packets.
    bool presenceQueued = false, announceQueued = false;
    // When its last private packet crossed the tunnel, either way: a phone in a chat goes first at the fast rhythm.
    uint32_t chattedAt = 0;
    // The Bluetooth link it announced itself on directly: only its own packets from there go over LoRa.
    uint16_t link = 0xffff;
    // Its private packets over LoRa: a burst, then one every two seconds.
    RateBucket frames;
    RateGate traffic;
    RateBucket lora;
    RateGate presence;
};

class PeerTable
{
  public:
    static constexpr size_t CAPACITY = BITCHAT_PEER_ENTRIES;
    static constexpr uint32_t STALE_MS = 300000;
    Result announce(Crypto &crypto, Bytes frame, uint64_t nowUtc, uint32_t now, Peer *&peer);
    Result authenticate(Crypto &crypto, Bytes frame, uint64_t nowUtc, uint32_t now, Peer *&peer);
    Peer *find(const PeerId &id);
    // The phone whose nickname `text` starts with (any letter case, then a space or ':'); none when two share it.
    Peer *named(Bytes text, size_t &consumed);
    // Every slot, used or not, for a walk over the table.
    Peer &slot(size_t index) { return entries[index]; }
    // Phones known only through another bridge's tunnel: few, so they never crowd out the phones here.
    size_t remoteCount() const;
    void dropOldestRemote();
    size_t size() const;
    bool nicknameShared(const Peer &peer) const;

  private:
    std::array<Peer, CAPACITY> entries{};
};

class BleReassembly
{
  public:
    Result accept(const Packet &fragment, uint32_t now, Frame &output);
    void expire(uint32_t now);
    static constexpr size_t CAPACITY = 3;
    static constexpr size_t MAX_FRAGMENTS = 32;

    bool empty() const
    {
        return std::none_of(entries.begin(), entries.end(), [](const auto &entry) { return entry.used; });
    }

  private:
    struct Assembly {
        PeerId sender{};
        PeerId id{};
        uint64_t timestamp = 0;
        uint32_t started = 0;
        uint16_t count = 0;
        uint8_t originalType = 0;
        uint8_t minimumTtl = 0;
        bool used = false;
        std::array<uint16_t, MAX_FRAGMENTS> sizes{};
        std::array<uint16_t, MAX_FRAGMENTS> offsets{};
        std::array<uint8_t, MAX_FRAME> bytes{};
        size_t occupied = 0;
    };
    std::array<Assembly, CAPACITY> entries{};
};

} // namespace bitchat

#pragma once

#include "BridgeCodec.h"
#include <array>

// The Cashu tunnel between bridges. A payment is the payer's own signed Bitchat frames (the latest
// announce and the message holding the token), carried verbatim as MeshCore channel datagrams and given
// to the phones near the other bridge as if the payer were there. Nothing is changed on the way, so the
// signatures still hold, and nothing is shown before every part arrived: a payment is whole or absent.
//
// Datagram: 'B' 'C', version (high nibble) and kind (low nibble), bundle id (4 bytes), then per kind
//   data  index, count, chunk
//   ack   outcome, phones, node number of the receiving bridge (4 bytes)
//   want  missing parts as a 16-bit mask
// Bundle: announce size (2 bytes, big endian), announce, message size (2 bytes), message.

namespace bitchat
{

constexpr uint16_t TUNNEL_DATA_TYPE = 0xffff; // MeshCore's DATA_TYPE_DEV
constexpr uint8_t TUNNEL_VERSION = 1;
constexpr size_t TUNNEL_DATAGRAM = 160; // below MeshCore's MAX_GROUP_DATA_LENGTH (165)
constexpr size_t TUNNEL_HEADER = 9;
constexpr size_t TUNNEL_CHUNK = TUNNEL_DATAGRAM - TUNNEL_HEADER;
constexpr size_t TUNNEL_PARTS = 12;
constexpr size_t TUNNEL_BUNDLE = TUNNEL_PARTS * TUNNEL_CHUNK;
// A phone checks a frame's time against its own clock with two minutes' tolerance: the whole trip,
// resends included, has to fit inside that.
constexpr uint32_t TUNNEL_WAIT_MS = 100000;
// Without a new part for this long the receiver asks once more for what it lacks.
constexpr uint32_t TUNNEL_QUIET_MS = 12000;
constexpr uint8_t TUNNEL_WANTS = 2;

enum class TunnelKind : uint8_t { Data = 0, Ack = 1, Want = 2 };

enum class TunnelOutcome : uint8_t {
    Delivered = 0, // given to the phones near the receiving bridge
    NoPhones = 1,  // whole and valid, but no phone was there to take it
    Late = 2,      // arrived after the phones would accept it
    Invalid = 3    // signatures or format did not hold
};

struct TunnelDatagram {
    std::array<uint8_t, TUNNEL_DATAGRAM> data{};
    size_t size = 0;
    Bytes bytes() const { return {data.data(), size}; }
};

// What one datagram meant.
struct TunnelHeard {
    TunnelKind kind = TunnelKind::Data;
    uint32_t id = 0;
    // data: the bundle is now whole; announce and message stay empty when its sizes do not add up
    bool complete = false;
    Bytes announce{}, message{};
    // ack
    TunnelOutcome outcome = TunnelOutcome::Delivered;
    uint8_t phones = 0;
    uint32_t node = 0;
    // want
    uint16_t missing = 0;
};

bool tunnelDatagram(Bytes data);
// An ack or a want; false for data and anything malformed.
bool readTunnelAnswer(Bytes data, TunnelHeard &out);
size_t tunnelParts(size_t bundleSize);
bool makeTunnelAck(uint32_t id, TunnelOutcome outcome, uint8_t phones, uint32_t node, TunnelDatagram &out);
bool makeTunnelWant(uint32_t id, uint16_t missing, TunnelDatagram &out);

// One payment on its way out.
class TunnelSender
{
  public:
    bool start(uint32_t id, Bytes announce, Bytes message, uint32_t now);
    bool active() const { return used; }
    uint32_t id() const { return bundleId; }
    uint8_t count() const { return parts; }
    uint32_t startedAt() const { return started; }
    bool hasPending() const { return pending != 0; }
    // The next part to send; false when none is due.
    bool next(TunnelDatagram &out);
    // Parts the receiver asked for are sent again, while there is time.
    void resend(uint16_t missing, uint32_t now);
    void finish() { used = false; }

  private:
    std::array<uint8_t, TUNNEL_BUNDLE> bundle{};
    size_t size = 0;
    uint32_t bundleId = 0, started = 0;
    uint16_t pending = 0;
    uint8_t parts = 0;
    bool used = false;
};

// Parts heard from other bridges, kept until the bundle is whole or too old.
class TunnelReceiver
{
  public:
    // A datagram from the channel. A whole bundle is returned once; later copies of its parts are not.
    bool accept(Bytes data, uint32_t now, TunnelHeard &out);
    // A want for a bundle still incomplete and quiet; false when nothing is to be asked now.
    bool want(uint32_t now, TunnelDatagram &out);
    void expire(uint32_t now);
    bool empty() const;

  private:
    struct Slot {
        std::array<uint8_t, TUNNEL_BUNDLE> data{};
        uint32_t id = 0, firstAt = 0, lastAt = 0;
        uint16_t have = 0, size = 0;
        uint8_t count = 0, wants = 0;
        bool used = false, done = false;
    };
    std::array<Slot, 2> slots{};
};

} // namespace bitchat

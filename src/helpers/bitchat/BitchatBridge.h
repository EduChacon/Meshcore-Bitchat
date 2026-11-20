#pragma once
#ifdef WITH_BITCHAT_BRIDGE
#include "BridgeBle.h"
#include "BridgeCashu.h"
#include "BridgeCrypto.h"
#include "BridgeHistory.h"
#include "BridgeState.h"
#include "BridgeText.h"
#include "BridgeTunnel.h"
#include <Preferences.h>
#include <memory>

// Messages remembered across restarts so they are not taken to LoRa twice; each one is a stored entry.
// A project with little settings storage can ask for fewer.
#ifndef BITCHAT_JOURNAL_ENTRIES
#define BITCHAT_JOURNAL_ENTRIES 64
#endif
// Recent packets remembered to drop the copies phones relay; fewer on a board short of memory.
#ifndef BITCHAT_DEDUP_ENTRIES
#define BITCHAT_DEDUP_ENTRIES 256
#endif

namespace bitchat
{
inline constexpr char PUBLIC_CHANNEL_NAME[] = "#bitchat";
inline constexpr uint8_t NO_CHANNEL = 255;

// What the bridge needs from the companion firmware. Channel secrets are 32 bytes; a 128-bit key
// leaves the second half at zero, as the firmware stores it.
class MeshHost
{
  public:
    // The firmware clock in seconds; it may never have been set.
    virtual uint32_t clockSeconds() = 0;
    virtual uint32_t nodeNumber() = 0;
    virtual const char *nodeName() = 0;
    // Slot of the channel with this secret, created under `name` in a free slot when missing.
    virtual uint8_t provideChannel(const char *name, const uint8_t secret[32]) = 0;
    // Name and secret of a slot in use; false for an empty or invalid one.
    virtual bool readChannel(uint8_t index, char name[32], uint8_t secret[32]) = 0;
    virtual uint32_t airtimeMs(size_t textSize) = 0;
    virtual bool radioBusy() = 0;
    // Transmits "sender: text" on the channel and shows it in the app attached to this radio.
    virtual bool sendChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size) = 0;
    // Shows a line in the attached app only; nothing goes on the air.
    virtual void showChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size) = 0;
    // A datagram on the channel (MeshCore group data of this type), for the Cashu tunnel between bridges.
    // The default says the firmware cannot send one.
    virtual bool sendChannelData(uint8_t index, uint16_t dataType, const uint8_t *data, size_t size)
    {
        (void)index;
        (void)dataType;
        (void)data;
        (void)size;
        return false;
    }
    // What a radio can tell about itself and its neighbours. The defaults say "not known".
    virtual uint16_t batteryMilliVolts() { return 0; }
    // The radio's buzzer and status light, kept as the operator sets them. False when the radio has none.
    virtual bool setSound(bool on)
    {
        (void)on;
        return false;
    }
    virtual bool setLight(bool on)
    {
        (void)on;
        return false;
    }
    struct HeardRadio {
        char name[32];
        // -1 when the route is not known, 0 for a direct neighbour
        int8_t hops;
        uint32_t secondsAgo;
        int16_t snrTenths;
    };
    // Radios heard on LoRa, in any order; at most `capacity`.
    virtual size_t heardRadios(HeardRadio *out, size_t capacity)
    {
        (void)out;
        (void)capacity;
        return 0;
    }
    // The radio's own position in millionths of a degree.
    virtual bool position(int32_t &latitude, int32_t &longitude)
    {
        (void)latitude;
        (void)longitude;
        return false;
    }
    // Whether the operator lets the radio's position go out (as in adverts). A help request asked from
    // Bitchat, by anyone nearby, carries the position only then; one typed in the attached app always does.
    virtual bool positionShared() { return false; }
    // Turns the GPS on so it can set the clock, or back to the operator's choice. False when there is none.
    virtual bool clockFromGps(bool on)
    {
        (void)on;
        return false;
    }
    virtual ~MeshHost() = default;
};
} // namespace bitchat

class BitchatBridge
{
  public:
    void begin(bitchat::MeshHost &meshHost);
    void loop();
    // Channel text heard on LoRa, as "sender: text"; `packetHash` (8 bytes) identifies the packet, and
    // `snrTenths` is its signal to noise ratio in tenths of a decibel, when known.
    void onChannelText(uint8_t channel, const uint8_t *packetHash, const char *text,
                       int16_t snrTenths = bitchat::SNR_UNKNOWN);
    // Channel text typed in the attached app. True when it was a bridge command, which is answered
    // locally and must not be transmitted.
    bool onAppText(uint8_t channel, const char *text, size_t size);
    // Channel data heard on LoRa. True when it was the bridges' own (a part of a payment, a confirmation),
    // which is not for the attached app.
    bool onChannelData(uint8_t channel, uint16_t dataType, const uint8_t *data, size_t size);

    bool enabled() const { return settings.enabled; }
    uint8_t textChannel() const { return settings.channel; }
    bool setEnabled(bool value);
    // Bitchat with LoRa on this channel, or Bluetooth only for NO_CHANNEL.
    bool startBasicMode(uint8_t channel = bitchat::NO_CHANNEL);
    bool eligibleChannel(uint8_t index) const;
    // For a radio with one button: Bitchat + LoRa, then Bitchat only, then off.
    bool nextMode();
    bool linked() const { return link; }
    bool setLink(bool value);
    bool saving() const { return saver; }
    bool setSaving(bool value);
    // Only #mesh text that starts with "!lora " goes to LoRa.
    bool prefixed() const { return prefixOnly; }
    bool setPrefixed(bool value);
    // A help request asked from Bitchat carries the radio's position, as on a trail; off unless asked for.
    bool sosPositioned() const { return sosPosition; }
    bool setSosPosition(bool value);
    // A new Bitchat identity, for when the node's own identity changes: the old one would link them.
    void renewIdentity();
    const char *modeName() const;
    // Phones that are talking, people known, and texts sent to and taken from LoRa.
    void counters(unsigned &phones, unsigned &people, uint32_t &out, uint32_t &in) const;
    const char *status() const;
    const char *hint() const;
    void diagnostics(char *out, size_t size) const;
    void restartLinks() { restart = 1; }
    uint32_t drops() const;

  private:
    struct Settings {
        uint32_t magic = 0x4243434d;
        uint8_t enabled = 1;
        uint8_t channel = bitchat::NO_CHANNEL;
        // The operator chose Bluetooth only, so the public channel is not created again.
        uint8_t localOnly = 0;
        uint8_t applied = 0;
        bitchat::Digest binding{};
    } settings;
    struct JournalEntry {
        std::array<uint8_t, 16> id{};
        uint64_t utc = 0;
    };
    struct RadioItem {
        static constexpr size_t QUEUE = 8;
        std::array<uint8_t, bitchat::LORA_TEXT_LIMIT> payload{};
        uint16_t size = 0;
        uint32_t queuedAt = 0;
    };
    bitchat::MeshHost *host = nullptr;
    Preferences storage;
    bool storageReady = false, identityReady = false, advertisingChanged = true, identityRenew = false;
    bool defaultsPending = false, defaultChannelUnavailable = false;
    // Text another network's bridge brought to Bitchat is taken to this one. Kept in its own stored entry.
    bool link = true;
    bool textReady = false, wasActive = false, textConflict = false;
    // For a minute after Bluetooth comes up the radio advertises as the stock firmware does, so the
    // MeshCore app can always find it by restarting the radio.
    bool bluetoothWas = false, nativeWindow = false;
    uint32_t bluetoothAt = 0;
    // The operator's choice, off unless asked for. Kept in its own stored entry. When on, with no Bitchat
    // phone for a while the radio announces itself about once a second and the bridge looks at its queues
    // less often; the first phone brings both back to full speed.
    bool saver = false, economy = false;
    uint32_t phoneAt = 0;
    // The operator's choice, off unless asked for: phone text goes to LoRa only when it starts with "!lora ".
    bool prefixOnly = false;
    // The operator's choice, off unless asked for: an SOS from Bitchat carries the position. Own stored entry.
    bool sosPosition = false;
    // The house note, given once every 12 hours to each phone that arrives. Kept in its own stored entry.
    struct Note {
        uint8_t portuguese = 0;
        char author[21]{};
        char text[56]{};
    } note;
    uint32_t noteAt = 0;
    bool noteChanged = false;
    // Low battery: told to Bitchat once, then again only after a charge.
    uint32_t batteryAt = 0;
    bool batteryWarned = false;
    // Help requests are spaced out, whoever asks.
    uint32_t sosAt = 0;
    bool sosSent = false;
    // Range test between bridges: the probe this radio sent, who asked for it, and the last answer it gave.
    struct RangeTest {
        uint16_t id = 0;
        uint32_t at = 0;
        bool pending = false, fromApp = false, portuguese = false;
        uint8_t channel = 0, answers = 0;
        uint16_t answeredId = 0;
        uint32_t answeredAt = 0;
        bool answered = false;
    } rangeTest;
    // The payment this radio is taking over LoRa, for telling the payer how it went.
    struct Payment {
        uint32_t id = 0, at = 0, refusedAt = 0, signedAt = 0, heldAt = 0;
        uint64_t amount = 0;
        char unit[13]{};
        char payer[bitchat::LABEL_LIMIT + 1]{};
        bool waiting = false, refused = false, holding = false;
        bitchat::TunnelOutcome outcome = bitchat::TunnelOutcome::Delivered;
        uint32_t node = 0;
        bitchat::PeerId sender{};
        bitchat::Digest messageId{};
    } payment;
    bitchat::RateGate paymentReplies;
    // The GPS is turned on for a while when nothing else set the clock.
    uint32_t gpsAt = 0;
    bool gpsOn = false, gpsTried = false;
    uint32_t ranAt = 0, interval = 1000;
    uint32_t textGuardAt = 0, conflictAt = 0, channelsAt = 0, announcedAt = 0, defaultChannelAt = 0;
    uint32_t lastBudget = 0, tokens = 0;
    uint64_t lastOriginTimestamp = 0, peerClockUtc = 0, clockFloor = 0;
    uint32_t peerClockAt = 0, clockSavedAt = 0, clockRtc = 0, clockAt = 0;
    bool clockSaved = false, clockSampled = false, clockTrusted = false;
    uint32_t forwardedBle = 0, submittedRadio = 0, importedRadio = 0, lost = 0;
    uint32_t receivedBle = 0, acceptedBle = 0, originatedBle = 0;
    std::array<uint32_t, 12> rejected{};
    std::array<uint8_t, 64> identity{};
    std::array<uint8_t, 32> signingPublic{}, noisePublic{};
    bitchat::PeerId ownId{};
    bitchat::Digest textKey{}, textKeyBinding{};
    std::array<JournalEntry, BITCHAT_JOURNAL_ENTRIES> journal{};
    bitchat::BridgeCrypto crypto;
    mutable char guardText[32]{};
    struct Runtime {
        std::unique_ptr<bitchat::PeerTable> peers;
        std::unique_ptr<bitchat::BleReassembly> fragments;
        std::unique_ptr<bitchat::DedupCache<BITCHAT_DEDUP_ENTRIES>> dedup;
        // Text brought from LoRa, kept for the phones that arrive later.
        std::unique_ptr<bitchat::History> history;
        struct Asked {
            uint16_t link = bitchat::NO_LINK;
            uint32_t at = 0;
        };
        std::array<Asked, 4> asked{};
        // Names that spoke on the LoRa channel, for "!bridge who".
        struct Heard {
            std::array<char, bitchat::LABEL_LIMIT + 1> name{};
            uint32_t at = 0;
            bool used = false;
        };
        std::array<Heard, 12> heard{};
        // Phones that were given the house note, and when.
        struct Delivered {
            bitchat::PeerId peer{};
            uint32_t at = 0;
            bool used = false;
        };
        std::array<Delivered, 8> delivered{};
        bitchat::BoundedQueue<RadioItem, RadioItem::QUEUE> radioQueue;
        // The latest announce of a few phones, as they sent it: a payment carries its payer's along.
        struct Announced {
            bitchat::PeerId peer{};
            uint16_t size = 0;
            uint32_t at = 0;
            std::array<uint8_t, 320> frame{};
        };
        std::array<Announced, 3> announced{};
        std::unique_ptr<bitchat::TunnelSender> tunnelOut;
        // A payment waiting for its payer's next announce.
        std::unique_ptr<bitchat::Frame> held;
        std::unique_ptr<bitchat::TunnelReceiver> tunnelIn;
        // Confirmations and requests for missing parts, sent before anything else.
        bitchat::BoundedQueue<bitchat::TunnelDatagram, 3> tunnelControl;
        bitchat::RateGate radioGate, cryptoGate;
        bitchat::BleInput input;
        bitchat::Frame assembled, working, canonical;
    };
    std::unique_ptr<Runtime> runtime;
    bool resourceError = false;
    // 0 idle, 1 requested, 2 phones were told to read the services again and the links drop next.
    uint8_t restart = 0;
    bitchat::Command appCommand = bitchat::Command::None;
    uint8_t appChannel = 0;
    bool appPortuguese = false;
    // The text after a command such as "note", copied for the next tick.
    char appArgument[bitchat::COMMAND_TEXT_LIMIT + 1]{};
    uint32_t restartAt = 0, repliedAt = 0, guidedAt = 0;

    void work(uint32_t now);
    void channelText(uint8_t channel, const uint8_t *packetHash, const char *text, int16_t snrTenths);
    bool appText(uint8_t channel, const char *text, size_t size);
    void answerApp();
    void answerBitchat();
    void guideBitchat(bool portuguese);
    void replyToApp(const char *text);
    // A signed public message from the bridge to every phone, "[bridge] " in front.
    void tellBitchat(const char *text, size_t size, bool keep = false);
    // The answers either app may ask for; `out` gets the text, without any prefix.
    size_t batteryText(bool portuguese, char *out, size_t capacity) const;
    size_t radiosText(bool portuguese, char *out, size_t capacity) const;
    size_t noteText(bool portuguese, char *out, size_t capacity) const;
    bool setNote(const bitchat::BridgeRequest &request, const char *author);
    size_t sos(const bitchat::BridgeRequest &request, const char *who, bool fromApp, char *out, size_t capacity);
    size_t startRangeTest(bool portuguese, bool fromApp, char *out, size_t capacity);
    void rangeTestHeard(bitchat::Bytes name, uint16_t id, bool answer, int16_t theirSnr, int16_t snrTenths);
    void reportRangeTest(const char *text);
    void deliverNote(const bitchat::Packet &packet, uint16_t source);
    void watchBattery(uint32_t now);
    void watchGps(uint32_t now);
    bitchat::GuideMode guideMode() const;
    bool queueRadioText(bitchat::Bytes label, bitchat::Bytes body);
    // The name a phone goes by on LoRa: its nickname, told apart by the start of its ID when shared.
    size_t peerLabel(const bitchat::Peer &peer, char *out, size_t capacity) const;
    // `direct`: the phone is connected to this radio, not relayed by others.
    void answerAsked(const bitchat::BridgeRequest &asked, const bitchat::Peer &peer, bool direct, uint32_t now);
    bool sign(uint8_t type, bitchat::Bytes payload, uint8_t ttl);
    bool ensureRuntime();
    void releaseRuntime();
    void applyDefaults();
    void ensureDefaultChannel();
    bool persist();
    bool initIdentity();
    bool binding(uint8_t index, bitchat::Digest &out) const;
    void refreshChannel();
    bool handleFrame(bitchat::Bytes frame, uint16_t source);
    void rememberAnnounce(const bitchat::Packet &packet, bitchat::Bytes frame);
    // A public message holding a Cashu token: carried whole to the other bridges, or refused with a word
    // to the payer. True when the message held one, so it is never taken as text.
    bool carryCashu(const bitchat::Packet &packet, bitchat::Bytes frame, const bitchat::Peer &peer, const bitchat::Digest &id);
    bool channelPrivate() const;
    void channelData(uint8_t channel, bitchat::Bytes data);
    void deliverPayment(const bitchat::TunnelHeard &heard);
    bitchat::TunnelOutcome checkPayment(const bitchat::TunnelHeard &heard, uint64_t &amount, char *unit, size_t unitSize,
                                        char *payer, size_t payerSize);
    void startPayment(bitchat::Bytes announce, bitchat::Bytes message);
    void paymentAnswered(const bitchat::TunnelHeard &heard, uint32_t now);
    void watchPayment(uint32_t now);
    void reportPayment(bool delivered, uint8_t phones);
    bool sendTunnel();
    bool tunnelDue() const;
    void say(const char *text);
    void exportText(const bitchat::Packet &packet, bitchat::Peer &peer, const bitchat::Digest &id);
    void importText(bitchat::Bytes name, bitchat::Bytes body, const bitchat::Digest *id);
    bool textGuard() const;
    void originate(uint8_t type, bitchat::Bytes payload, bool keep = false);
    void answerSync(const bitchat::Packet &packet, uint16_t source);
    size_t kept() const;
    void noteHeard(const char *name, size_t size);
    // "Ana, Rita +2": whole names, the latest first, as many as fit.
    size_t heardNames(char *out, size_t capacity) const;
    size_t whoText(bool portuguese, char *out, size_t capacity) const;
    void announce();
    bool record(const bitchat::Digest &id, bool persist = true);
    bool seen(const bitchat::Digest &id) const;
    void sendRadio();
    void reject(bitchat::Result result);
    uint64_t utc() const;
    uint64_t trustedUtc() const;
    void watchClock(uint32_t now);
    void adoptPeerClock(bitchat::Bytes frame, const bitchat::Packet &packet);
    void keepClock(uint32_t now);
};
extern BitchatBridge bitchat_bridge;
#endif

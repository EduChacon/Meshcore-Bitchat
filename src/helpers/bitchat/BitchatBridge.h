#pragma once
#ifdef WITH_BITCHAT_BRIDGE
#include "BridgeBle.h"
#include "BridgeCashu.h"
#include "BridgeCrypto.h"
#include "BridgeHistory.h"
#include "BridgeNoise.h"
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

// Phones that can hold a private chat with the bridge at once; the one quiet longest gives way.
#ifndef BITCHAT_PRIVATE_SESSIONS
#define BITCHAT_PRIVATE_SESSIONS 3
#endif

namespace bitchat
{
inline constexpr char PUBLIC_CHANNEL_NAME[] = "#bitchat";

// A private chat with one phone: its Noise session, and the handshake under way, if any. A new handshake
// leaves the old session working until it completes, as the apps do.
struct PrivateSession {
    PeerId peer{};
    bool used = false, ready = false, handshaking = false;
    uint8_t failures = 0;
    // The link the phone talks to the bridge on directly, if any: answers go only there.
    uint16_t link = NO_LINK;
    uint32_t usedAt = 0, handshakeAt = 0, initiatedAt = 0, openedAt = 0;
    // The time on the phone's last packet, so the answers are not dated before their question.
    uint64_t heardStamp = 0;
    // Handshakes cost curve operations and anyone may claim a phone's ID: a few per phone, then slowly.
    RateBucket handshakes;
    // Acks and answers: ten at once (the guide is five), then one a second, so a flood is not echoed in full.
    RateBucket outgoing;
    // Direct messages to LoRa contacts from this phone.
    RateBucket directs;
    NoiseSender send;
    NoiseReceiver receive;
    NoiseHandshake handshake;
    // The IDs of the last messages, hashed: the app sends a message again until it hears it was delivered.
    std::array<uint32_t, 6> recent{};
    uint8_t recentNext = 0;
};
struct PrivateSessions {
    std::array<PrivateSession, BITCHAT_PRIVATE_SESSIONS> slots{};
};
// Direct messages from LoRa waiting for a phone that is away: delivered when it opens a private chat again.
// An item stays until the phone acknowledges it ("delivered" with its message ID).
struct Mailbox {
    struct Item {
        PeerId phone{};
        std::array<uint8_t, 8> from{};
        uint32_t at = 0, sentAt = 0;
        uint8_t size = 0;
        bool used = false;
        char id[37];
        char text[255];
    };
    std::array<Item, 8> items{};
};
inline constexpr uint8_t NO_CHANNEL = 255;
// The bridge's airtime budget: it fills back at a tenth of the time passed (10% of the airtime on average).
inline constexpr uint32_t BRIDGE_BUDGET_MS = 12000;

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
    // The first 8 bytes of the radio's MeshCore public key, and a signature with that identity.
    virtual bool nodeKeyPrefix(uint8_t keyPrefix[8])
    {
        (void)keyPrefix;
        return false;
    }
    virtual bool signAsNode(const uint8_t *data, size_t size, uint8_t signature[64])
    {
        (void)data;
        (void)size;
        (void)signature;
        return false;
    }
    enum class NodeCheck : uint8_t { Unknown, Valid, Invalid };
    // Checks a signature by a radio this one has as a contact (its key came in an advert); `name` gets the
    // contact's name. Unknown when no contact has that key.
    virtual NodeCheck verifyNode(const uint8_t keyPrefix[8], const uint8_t *data, size_t size, const uint8_t signature[64],
                                 char name[32])
    {
        (void)keyPrefix;
        (void)data;
        (void)size;
        (void)signature;
        (void)name;
        return NodeCheck::Unknown;
    }
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
    // Direct messages with the radio's LoRa contacts, for the private chat (BridgeDirect.cpp). A contact is named
    // by 8 bytes: the start of its public key on MeshCore, its node number on Meshtastic.
    struct LoraContact {
        uint8_t id[8];
        char name[32];
    };
    // The FAVOURITE chat contact whose name `text` starts with: the longest such name, in any letter case, followed
    // by a space or ':'. `consumed` is the length of the name. False when none matches; `ambiguous` when two
    // favourites have it. Only the owner's favourites can be written to from Bitchat.
    virtual bool findContact(const char *text, size_t size, LoraContact &out, size_t &consumed, bool &ambiguous)
    {
        (void)text, (void)size, (void)out, (void)consumed;
        ambiguous = false;
        return false;
    }
    // Whether the contact with this ID is one of the owner's favourites: only their answers reach Bitchat.
    virtual bool favouriteContact(const uint8_t id[8])
    {
        (void)id;
        return false;
    }
    // A direct message, dated `timestamp` (seconds, the bridge's own clock: the firmware's may be unset).
    virtual bool sendDirectText(const uint8_t id[8], uint32_t timestamp, const char *text, size_t size)
    {
        (void)id, (void)timestamp, (void)text, (void)size;
        return false;
    }
    // The longest text one direct message carries; 0 when the firmware sends none for the bridge.
    virtual size_t directTextLimit() { return 0; }
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
    // A radio's advert heard on LoRa, signature already checked: the first bytes of its key and the time it
    // signed. With no other source, two radios that agree within a minute give the bridge the time.
    void onRadioTime(const uint8_t keyPrefix[4], uint32_t seconds);
    // A direct text message the radio received from a LoRa contact (its 8-byte ID and name). The firmware still
    // shows it to its own app; the bridge passes it on to a Bitchat phone that is talking with that contact,
    // or that is named at its start ("@ana ...").
    void onDirectText(const uint8_t id[8], const char *name, const char *text, size_t size);
    bool directMessages() const { return dmEnabled; }
    bool setDirectMessages(bool value);
    bool tunnel() const { return tunnelOn; }
    // The key that proves a bridge on this channel in its announce (for tests that play another bridge).
    const uint8_t *gatewayKey() const { return textKey.data(); }
    bool setTunnel(bool value);
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
    // The operator's choice, on unless turned off: Bitchat phones send direct messages to LoRa contacts through
    // the private chat. Own stored entry.
    bool dmEnabled = true;
    // The operator's choice, off unless asked for: private chats between phones near different bridges, over the
    // LoRa channel (BridgeRemote.cpp). Costs airtime on the channel. Own stored entry.
    // On by default since bridge 1.0; the owner turns it off with "!ponte túnel off".
    bool tunnelOn = true;
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
        // Asked in a private chat: the result goes back there, never to the public chat.
        bool privately = false;
        bitchat::PeerId asker{};
    } rangeTest;
    // The payment this radio is taking over LoRa, for telling the payer how it went.
    struct Payment {
        uint32_t id = 0, at = 0, answeredAt = 0, signedAt = 0, heldAt = 0;
        uint64_t amount = 0;
        char unit[13]{};
        char payer[bitchat::LABEL_LIMIT + 1]{};
        bool waiting = false, answered = false, holding = false, trusted = false;
        // The best answer so far: a delivery a known radio signed ends the wait at once.
        bitchat::TunnelOutcome outcome = bitchat::TunnelOutcome::Delivered;
        uint8_t phones = 0, rank = 0, channel = bitchat::NO_CHANNEL;
        // The channel the parts go on: if it changes meanwhile, the rest is not sent.
        bitchat::Digest binding{};
        uint32_t node = 0;
        char bridge[15]{};
        std::array<uint8_t, 8> messageHash{};
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
    // The time came from LoRa adverts: a phone's own time, when one announces itself, replaces it.
    bool peerClockFromRadio = false;
    // The LoRa time came from one radio alone: any phone, or two radios that agree, replace it.
    bool peerClockSingle = false;
    // The time each Bluetooth link's phone gave lately: one link, one vote, so one device cannot carry the clock
    // off against the phones next to it.
    struct PhoneTime {
        uint16_t link = 0xffff;
        uint64_t utc = 0;
        uint32_t at = 0;
        bool used = false;
    };
    std::array<PhoneTime, 4> phoneTimes{};
    // A standby for another bridge holds only while the text it should carry shows up on LoRa.
    bool standbyCheck = false;
    uint8_t standbyBodySize = 0;
    std::array<uint8_t, 20> standbyBody{};
    uint32_t standbyCheckAt = 0, conflictRefusedAt = 0;
    // Tunnel parts sent over LoRa and packets from the tunnel given to the phones here (the !bridge report).
    uint32_t tunnelSent = 0, tunnelReceived = 0;
    struct RadioTime {
        std::array<uint8_t, 4> key{};
        uint64_t utc = 0;
        uint32_t at = 0;
        bool used = false;
    };
    std::array<RadioTime, 6> radioTimes{};
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
        // Private chats with phones, made when the first one starts.
        std::unique_ptr<bitchat::PrivateSessions> privates;
        bitchat::RateBucket handshakes;
        // Which phone wrote to which LoRa contact through the bridge, for the answers.
        struct DirectThread {
            bitchat::PeerId phone{};
            std::array<uint8_t, 8> contact{};
            uint32_t at = 0;
            bool used = false;
        };
        std::array<DirectThread, 8> threads{};
        std::unique_ptr<bitchat::Mailbox> mailbox;
        bitchat::RateBucket directs;
        // Direct messages from LoRa just passed on: a resend after a lost ack is not passed on twice.
        struct RecentDirect {
            std::array<uint8_t, 8> from{};
            uint32_t hash = 0, at = 0;
        };
        std::array<RecentDirect, 4> recentDirects{};
        uint8_t nextRecentDirect = 0;
        uint32_t directSeconds = 0;
        // Frames between phones near different bridges: parts to send and parts heard.
        struct Frames {
            struct Part {
                bitchat::TunnelDatagram datagram;
                uint32_t queuedAt = 0;
                // A phone's routine presence: it leaves room in the airtime budget for the chats themselves.
                bool presence = false;
                // A part of a phone's announce (whose phone, which part of how many): the phone counts as
                // announced on the other side only once the last part went.
                bool announce = false;
                bitchat::PeerId peer{};
                uint8_t index = 0, count = 1;
            };
            // Handshake parts first: they must cross within seconds. Then announces and messages; then the
            // presence of the phones at the fast rhythm (the app forgets them after 60 s), the slower ones last.
            bitchat::BoundedQueue<Part, 6> urgent, out, fast, presence;
            // The latest announce of the phones at the fast rhythm, as it goes over LoRa (a hop older), so the
            // bridge sends it on its own clock: kept here, not among the few announces shared with payments.
            struct Latest {
                bitchat::PeerId peer{};
                uint32_t at = 0;
                uint16_t size = 0;
                std::array<uint8_t, bitchat::TUNNEL_FRAME> frame{};
            };
            std::array<Latest, 2> latest{};
            bitchat::FrameReceiver in;
            uint32_t nextId = 0, heardAt = 0;
            // All the phones' private packets together.
            bitchat::RateBucket budget;
            // Turns between these parts and the public text, so neither waits for the other.
            bool turn = false;
        };
        std::unique_ptr<Frames> frames;
        bitchat::BleInput input;
        bitchat::Frame assembled, working, canonical;
    };
    std::unique_ptr<Runtime> runtime;
    bool resourceError = false;
    uint32_t runtimeAttachment = 0; // bleAttachments() when the runtime was allocated
    // 0 idle, 1 requested, 2 phones were told to read the services again and the links drop next.
    uint8_t restart = 0;
    bitchat::Command appCommand = bitchat::Command::None;
    uint8_t appChannel = 0;
    bool appPortuguese = false;
    // The text after a command such as "note", copied for the next tick.
    char appArgument[bitchat::COMMAND_TEXT_LIMIT + 1]{};
    uint32_t restartAt = 0, repliedAt = 0, guidedAt = 0;
    // While a private request is answered, the answers go to that chat instead of the public one.
    bitchat::PrivateSession *privateReply = nullptr;
    uint32_t privateIn = 0, privateOut = 0;

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
    // The time for a packet this bridge sends: never the same twice.
    uint64_t nextTimestamp();
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
    void reportPayment();
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
    void adoptPeerClock(bitchat::Bytes frame, const bitchat::Packet &packet, uint16_t source);
    void keepClock(uint32_t now);
    // Private chats (BridgePrivate.cpp): a Noise packet addressed to this bridge.
    void privateFrame(const bitchat::Packet &packet, const bitchat::Peer &peer, uint16_t source, uint32_t now);
    bitchat::PrivateSession *privateSession(const bitchat::PeerId &peer, uint32_t now, bool create);
    void privateHandshake(bitchat::PrivateSession &session, const bitchat::Packet &packet, uint16_t link, uint32_t now);
    void privateTransport(bitchat::PrivateSession &session, const bitchat::Packet &packet, const bitchat::Peer &peer,
                          uint16_t link, uint32_t now);
    void privateRequest(bitchat::PrivateSession &session, const bitchat::Peer &peer, bitchat::Bytes text, bool direct,
                        uint32_t now);
    bool initiatePrivate(bitchat::PrivateSession &session, uint32_t now);
    bool sendPrivate(bitchat::PrivateSession &session, uint8_t kind, bitchat::Bytes body, bool linkOnly = false);
    // Text to the chat being answered; a leading "[bridge] " is left out there.
    bool replyPrivately(const char *text, size_t size);
    // `id`, when given, gets the message ID; `linkOnly` sends only to the phone's own link, nowhere else.
    bool sendPrivateText(bitchat::PrivateSession &session, const char *text, size_t size, char *id = nullptr,
                         bool linkOnly = false);
    bool sendNoise(uint8_t type, const bitchat::PeerId &to, bitchat::Bytes payload, uint16_t link, bool linkOnly = false);
    void mailAcknowledged(const bitchat::PeerId &phone, bitchat::Bytes id);
    bitchat::Peer *namedPhone(bitchat::Bytes text, size_t &consumed);
    // Direct messages with LoRa contacts (BridgeDirect.cpp).
    void privateDirect(bitchat::PrivateSession &session, const bitchat::Peer &peer, bitchat::Bytes text, uint32_t now);
    void directText(const uint8_t id[8], const char *name, const char *text, size_t size);
    void deliverMail(bitchat::PrivateSession &session, uint32_t now);
    void offerMail(const bitchat::PeerId &phone, uint32_t now);
    // Private chats between phones near different bridges (BridgeRemote.cpp).
    void tunnelOut(const bitchat::Packet &packet, bitchat::Peer &peer, bitchat::Bytes relayed, uint16_t source, uint32_t now);
    void tunnelIn(bitchat::Bytes data, uint32_t now);
    bool fastPresence(const bitchat::Peer &peer, uint32_t now) const;
    static bool relayedCopy(const Runtime::Announced &kept, std::array<uint8_t, sizeof(Runtime::Announced::frame)> &out);
    bool partnerActive(uint32_t now) const;
    void tunnelPresence(uint32_t now);
    size_t fastSlots() const;
    void announceDone(const bitchat::PeerId &id, bool sent, bool presence, uint32_t now);
    void keepLatest(const bitchat::Peer &peer, bitchat::Bytes relayed, uint32_t now);
    void clearAnnounceMarks();
    bool frameReady(uint32_t now);
    bool queueFrame(bitchat::Bytes frame, uint32_t now, bool urgent = false, bool presence = false, bool fast = false);
    bool sendFrame();
    void expireHandshake(bitchat::PrivateSession &session, uint32_t now);
};
extern BitchatBridge bitchat_bridge;
#endif

#pragma once
#ifdef WITH_BITCHAT_BRIDGE
#include "BridgeBle.h"
#include "BridgeCrypto.h"
#include "BridgeState.h"
#include "BridgeText.h"
#include <Preferences.h>
#include <memory>

// Messages remembered across restarts so they are not taken to LoRa twice; each one is a stored entry.
// A project with little settings storage can ask for fewer.
#ifndef BITCHAT_JOURNAL_ENTRIES
#define BITCHAT_JOURNAL_ENTRIES 64
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
    virtual ~MeshHost() = default;
};
} // namespace bitchat

class BitchatBridge
{
  public:
    void begin(bitchat::MeshHost &meshHost);
    void loop();
    // Channel text heard on LoRa, as "sender: text"; `packetHash` (8 bytes) identifies the packet.
    void onChannelText(uint8_t channel, const uint8_t *packetHash, const char *text);
    // Channel text typed in the attached app. True when it was a bridge command, which is answered
    // locally and must not be transmitted.
    bool onAppText(uint8_t channel, const char *text, size_t size);

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
    bool storageReady = false, identityReady = false, advertisingChanged = true;
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
        std::unique_ptr<bitchat::DedupCache<256>> dedup;
        bitchat::BoundedQueue<RadioItem, RadioItem::QUEUE> radioQueue;
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
    uint32_t restartAt = 0, repliedAt = 0;

    void work(uint32_t now);
    void channelText(uint8_t channel, const uint8_t *packetHash, const char *text);
    bool appText(uint8_t channel, const char *text, size_t size);
    void answerApp();
    void answerBitchat();
    void replyToApp(const char *text);
    bool ensureRuntime();
    void releaseRuntime();
    void applyDefaults();
    void ensureDefaultChannel();
    bool persist();
    bool initIdentity();
    bool binding(uint8_t index, bitchat::Digest &out) const;
    void refreshChannel();
    bool handleFrame(bitchat::Bytes frame, uint16_t source);
    void exportText(const bitchat::Packet &packet, bitchat::Peer &peer, const bitchat::Digest &id);
    void importText(bitchat::Bytes name, bitchat::Bytes body, const bitchat::Digest *id);
    bool textGuard() const;
    void originate(uint8_t type, bitchat::Bytes payload);
    void announce();
    bool record(const bitchat::Digest &id);
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

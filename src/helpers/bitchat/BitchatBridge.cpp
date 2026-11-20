#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include "BridgePlatform.h"
#include <Arduino.h>
#include <Curve25519.h>
#include <HKDF.h>
#include <esp_system.h>
#include <new>

using namespace bitchat;
BitchatBridge bitchat_bridge;
static constexpr uint32_t DEDUP_MS = 125000;
static constexpr uint64_t JOURNAL_MS = 600000;
static constexpr uint32_t GUARD_MS = 30000;
static constexpr uint32_t CONFLICT_MS = 90000;
static constexpr uint32_t RADIO_DEADLINE_MS = 120000;
static constexpr uint32_t BUDGET_MS = 12000;
static constexpr uint32_t NATIVE_WINDOW_MS = 60000;
static constexpr uint32_t ECONOMY_AFTER_MS = 180000;
static constexpr uint32_t NOTE_EVERY_MS = 12UL * 3600 * 1000;
static constexpr uint32_t SOS_EVERY_MS = 300000;
static constexpr uint32_t NOTE_CHANGE_MS = 600000;

// Whether a note command carries text that is left after cleaning.
static bool noteArgument(const BridgeRequest &request)
{
    char probe[56];
    return request.command == Command::Note && cleanText(request.argument, probe, sizeof(probe), sizeof(probe) - 1);
}
static constexpr uint32_t TEST_WAIT_MS = 120000;
static constexpr uint32_t GPS_AFTER_MS = 30000;
static constexpr uint32_t GPS_FOR_MS = 600000;
static constexpr uint32_t GPS_AGAIN_MS = 6UL * 3600 * 1000;
static constexpr uint16_t BATTERY_LOW_MV = 3450;
static constexpr uint16_t BATTERY_RECOVERED_MV = 3600;
// What the attached app shows as the sender of the bridge's own answers.
static constexpr char APP_SENDER[] = "bridge: ";
// The well-known key of the MeshCore "Public" channel, where a bridge would be noise for everyone.
static constexpr uint8_t MESH_PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                                0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

// A hashtag channel is keyed by the first 16 bytes of the SHA-256 of its name, so the name is all a
// MeshCore user needs to join it.
static void publicSecret(Crypto &crypto, uint8_t secret[32])
{
    Digest hash;
    crypto.hash({reinterpret_cast<const uint8_t *>(PUBLIC_CHANNEL_NAME), sizeof(PUBLIC_CHANNEL_NAME) - 1}, hash);
    std::fill_n(secret, 32, 0);
    std::copy_n(hash.begin(), 16, secret);
}

static void bindingOf(const uint8_t secret[32], Digest &out)
{
    SHA256 sha;
    static const char purpose[] = "meshcore-bitchat/channel/v1";
    sha.update(purpose, sizeof(purpose));
    sha.update(secret, 32);
    sha.finalize(out.data(), out.size());
}

void BitchatBridge::begin(MeshHost &meshHost)
{
    host = &meshHost;
    setupBootDiagnostics();
    storageReady = storage.begin("bc-meshcore", false);
    if (storageReady && storage.isKey("settings")) {
        if (storage.getBytesLength("settings") != sizeof(settings) ||
            storage.getBytes("settings", &settings, sizeof(settings)) != sizeof(settings) || settings.magic != 0x4243434d ||
            settings.enabled > 1 || settings.localOnly > 1 || settings.applied > 1) {
            settings = Settings{};
            storageReady = false;
        }
    }
    defaultsPending = storageReady && settings.applied == 0;
    for (size_t i = 0; storageReady && i < journal.size(); ++i) {
        char key[8];
        snprintf(key, sizeof(key), "j%02u", unsigned(i));
        auto &entry = journal[i];
        if (storage.isKey(key) &&
            (storage.getBytesLength(key) != sizeof(entry) || storage.getBytes(key, &entry, sizeof(entry)) != sizeof(entry)))
            storageReady = false;
    }
    uint8_t stored = 1;
    if (storageReady && storage.isKey("link") && storage.getBytesLength("link") == 1 && storage.getBytes("link", &stored, 1) == 1)
        link = stored != 0;
    stored = 0;
    if (storageReady && storage.isKey("saver") && storage.getBytesLength("saver") == 1 && storage.getBytes("saver", &stored, 1) == 1)
        saver = stored != 0;
    stored = 0;
    if (storageReady && storage.isKey("prefix") && storage.getBytesLength("prefix") == 1 && storage.getBytes("prefix", &stored, 1) == 1)
        prefixOnly = stored != 0;
    stored = 0;
    if (storageReady && storage.isKey("sospos") && storage.getBytesLength("sospos") == 1 && storage.getBytes("sospos", &stored, 1) == 1)
        sosPosition = stored != 0;
    if (storageReady && storage.isKey("note") && storage.getBytesLength("note") == sizeof(note) &&
        storage.getBytes("note", &note, sizeof(note)) == sizeof(note)) {
        note.author[sizeof(note.author) - 1] = 0;
        note.text[sizeof(note.text) - 1] = 0;
        note.portuguese = note.portuguese != 0;
    } else {
        note = Note{};
    }
    if (storageReady && storage.isKey("clock") && storage.getBytesLength("clock") == sizeof(clockFloor))
        storage.getBytes("clock", &clockFloor, sizeof(clockFloor));
#ifdef BRIDGE_BUILD_EPOCH
    clockFloor = std::max<uint64_t>(clockFloor, uint64_t(BRIDGE_BUILD_EPOCH) * 1000);
#endif
    refreshChannel();
    bleConfigure(false, false, false);
    ranAt = millis();
}

// The firmware clock only jumps when the app, a GPS fix or an operator command sets it. What it
// holds before that may be a guess from stored contacts, which is no use for packet freshness.
void BitchatBridge::watchClock(uint32_t now)
{
    const uint32_t clock = host->clockSeconds();
    if (clockSampled) {
        const int64_t drift = int64_t(clock) - int64_t(clockRtc) - int64_t(uint32_t(now - clockAt) / 1000);
        if (drift > 2 || drift < -2)
            clockTrusted = true;
    }
    clockSampled = true;
    clockRtc = clock;
    clockAt = now;
}

uint64_t BitchatBridge::trustedUtc() const
{
    const uint64_t clock = uint64_t(host->clockSeconds()) * 1000;
    return clockTrusted && clock >= clockFloor ? clock : 0;
}

uint64_t BitchatBridge::utc() const
{
    const uint64_t trusted = trustedUtc();
    if (trusted || !peerClockUtc)
        return trusted;
    return peerClockUtc + uint32_t(millis() - peerClockAt);
}

// Until a trusted source sets the clock, a phone that announces itself directly supplies the time:
// never before the last trusted time, and afterwards only forward. The firmware clock is left alone.
void BitchatBridge::adoptPeerClock(Bytes frame, const Packet &packet)
{
    if (trustedUtc() || packet.type != 1 || packet.ttl != 7 || packet.timestamp < clockFloor)
        return;
    const uint64_t current = utc();
    // A phone next to the radio moves the time only forward. A time from LoRa adverts it may also put back, by
    // up to a quarter of an hour: an old announce heard again cannot take the bridge further into the past.
    static constexpr uint64_t PHONE_BACK_MS = 15ULL * 60 * 1000;
    if (current && (peerClockFromRadio ? packet.timestamp + PHONE_BACK_MS < current : packet.timestamp <= current + FRESHNESS_MS))
        return;
    Peer *peer = nullptr;
    if (runtime->peers->announce(crypto, frame, packet.timestamp, millis(), peer) != Result::Ok)
        return;
    peerClockUtc = packet.timestamp;
    peerClockAt = millis();
    peerClockFromRadio = false;
    announcedAt = peerClockAt - 60000;
}

// Adverts are signed with the time their radio keeps. One radio may be wrong, and one transmitter may sign with
// several keys, so the time comes from the largest group of radios agreeing within a minute, the earliest of the
// group so a fast clock does not lead. An advert no newer than the last one of its radio is a replay and counts
// for nothing. The LoRa time changes only when a larger group disagrees with it; it never goes below the last
// trusted time, and the firmware clock is left alone.
void BitchatBridge::onRadioTime(const uint8_t keyPrefix[4], uint32_t seconds)
{
    static constexpr uint32_t KEEP_MS = 30UL * 60 * 1000;
    static constexpr uint64_t AGREE_MS = 60000;
    const uint64_t heard = uint64_t(seconds) * 1000;
    // An advert may arrive before begin().
    if (!host || trustedUtc() || heard < clockFloor)
        return;
#ifdef BRIDGE_BUILD_EPOCH
    // Ten years after this firmware was built: a radio claiming more has a broken clock.
    if (heard > (uint64_t(BRIDGE_BUILD_EPOCH) + 10ULL * 365 * 86400) * 1000)
        return;
#endif
    const uint32_t now = millis();
    RadioTime *slot = nullptr, *spare = nullptr;
    for (auto &entry : radioTimes) {
        if (entry.used && !Throttle::isWithinTimespanMsAt(entry.at, KEEP_MS, now))
            entry.used = false;
        if (entry.used && std::equal(entry.key.begin(), entry.key.end(), keyPrefix))
            slot = &entry;
    }
    // A free entry, or the one heard longest ago.
    for (auto &entry : radioTimes)
        if (!spare || (spare->used && (!entry.used || int32_t(entry.at - spare->at) < 0)))
            spare = &entry;
    if (slot) {
        // Its own time carried to now: an advert that does not move it forward is an old one heard again.
        if (heard <= slot->utc + uint32_t(now - slot->at))
            return;
    } else {
        slot = spare;
        std::copy_n(keyPrefix, slot->key.size(), slot->key.begin());
    }
    slot->utc = heard;
    slot->at = now;
    slot->used = true;
    const auto carried = [now](const RadioTime &entry) { return entry.utc + uint32_t(now - entry.at); };
    const auto near = [](uint64_t a, uint64_t b) { return (a > b ? a - b : b - a) <= AGREE_MS; };
    uint64_t best = 0;
    size_t bestCount = 0;
    for (const auto &a : radioTimes) {
        if (!a.used)
            continue;
        const uint64_t ta = carried(a);
        size_t count = 0;
        uint64_t earliest = ta;
        for (const auto &b : radioTimes)
            if (b.used && near(carried(b), ta)) {
                ++count;
                earliest = std::min(earliest, carried(b));
            }
        if (count > bestCount || (count == bestCount && earliest < best)) {
            bestCount = count;
            best = earliest;
        }
    }
    if (bestCount < 2)
        return;
    const uint64_t current = utc();
    if (current) {
        // A phone's own time stands.
        if (!peerClockFromRadio || near(best, current))
            return;
        size_t support = 0;
        for (const auto &entry : radioTimes)
            support += entry.used && near(carried(entry), current);
        if (bestCount <= support)
            return;
    }
    peerClockUtc = best;
    peerClockAt = now;
    peerClockFromRadio = true;
    announcedAt = now - 60000;
}

void BitchatBridge::keepClock(uint32_t now)
{
    const uint64_t trusted = trustedUtc();
    if (trusted ? (clockSaved && Throttle::isWithinTimespanMsAt(clockSavedAt, 3600000, now))
                : (!peerClockUtc || Throttle::isWithinTimespanMsAt(peerClockAt, 3600000, now)))
        return;
    if (trusted) {
        // Only a trusted time raises the floor kept across restarts; a phone could otherwise lock the bridge in the future.
        clockFloor = trusted;
        clockSaved = true;
        clockSavedAt = now;
        if (storageReady)
            storage.putBytes("clock", &clockFloor, sizeof(clockFloor));
    } else {
        peerClockUtc += uint32_t(now - peerClockAt);
        peerClockAt = now;
    }
}

bool BitchatBridge::persist()
{
    settings.applied = 1;
    defaultsPending = false;
    advertisingChanged = true;
    if (!storageReady || storage.putBytes("settings", &settings, sizeof(settings)) != sizeof(settings)) {
        storageReady = false;
        settings.enabled = false;
        return false;
    }
    return true;
}

void BitchatBridge::applyDefaults()
{
    if (!defaultsPending || !storageReady || !ensureRuntime())
        return;
    if (!initIdentity())
        return;
    // Applied once; a later choice of off or Bluetooth only survives restarts.
    settings.enabled = true;
    textGuardAt = millis();
    persist();
    ensureDefaultChannel();
}

// The public channel is part of the out-of-the-box setup. Unless the operator chose Bluetooth only or
// another channel, it is created again whenever it is missing, so a wiped channel list heals by itself.
void BitchatBridge::ensureDefaultChannel()
{
    if (defaultsPending || !storageReady || !settings.enabled || settings.localOnly || textReady)
        return;
    uint8_t secret[32];
    publicSecret(crypto, secret);
    Digest standard;
    bindingOf(secret, standard);
    if (settings.channel != NO_CHANNEL && settings.binding != standard)
        return;
    const uint8_t channel = host->provideChannel(PUBLIC_CHANNEL_NAME, secret);
    Digest fingerprint{};
    if (channel == NO_CHANNEL || !binding(channel, fingerprint)) {
        defaultChannelUnavailable = true;
        return;
    }
    defaultChannelUnavailable = false;
    if (settings.channel != channel || settings.binding != fingerprint) {
        settings.channel = channel;
        settings.binding = fingerprint;
        textGuardAt = millis();
        persist();
    }
    refreshChannel();
}

bool BitchatBridge::initIdentity()
{
    if (identityReady)
        return true;
    if (!storageReady)
        return false;
    const auto zero = [](uint8_t b) { return b == 0; };
    if (storage.isKey("identity") && !identityRenew) {
        if (storage.getBytesLength("identity") != identity.size() ||
            storage.getBytes("identity", identity.data(), identity.size()) != identity.size()) {
            storageReady = false;
            return false;
        }
    }
    // A stored identity of zeros was given up by renewIdentity(): a new one is made.
    if (!storage.isKey("identity") || identityRenew || std::all_of(identity.begin(), identity.end(), zero)) {
        // The hardware generator needs the radio running to be unpredictable.
        if (!platformBluetooth.isActive())
            return false;
        esp_fill_random(identity.data(), identity.size());
        // Nothing usable came from the generator: try again later rather than keep a weak key.
        if (std::all_of(identity.begin(), identity.begin() + 32, zero) || std::all_of(identity.begin() + 32, identity.end(), zero))
            return false;
        identity[32] &= 0xf8;
        identity[63] = (identity[63] & 0x7f) | 0x40;
        if (storage.putBytes("identity", identity.data(), identity.size()) != identity.size()) {
            storageReady = false;
            return false;
        }
        identityRenew = false;
    }
    Ed25519::derivePublicKey(signingPublic.data(), identity.data());
    if (!Curve25519::eval(noisePublic.data(), identity.data() + 32, nullptr))
        return false;
    Digest hash;
    crypto.hash({noisePublic.data(), noisePublic.size()}, hash);
    std::copy_n(hash.begin(), ownId.size(), ownId.begin());
    identityReady = true;
    return true;
}

void BitchatBridge::renewIdentity()
{
    // Zeros in storage mark the old identity as given up, also across a restart before the new one exists.
    identity.fill(0);
    if (storageReady)
        storage.putBytes("identity", identity.data(), identity.size());
    // The private chats were with the old identity.
    if (runtime && runtime->privates)
        for (auto &session : runtime->privates->slots) {
            session.handshake.wipe();
            session = {};
        }
    identityRenew = true;
    identityReady = false;
}

bool BitchatBridge::setEnabled(bool value)
{
    resourceError = false;
    if (value) {
        if (!ensureRuntime())
            return false;
        if (!initIdentity()) {
            releaseRuntime();
            return false;
        }
    } else {
        releaseRuntime();
    }
    settings.enabled = value;
    textGuardAt = millis();
    if (runtime)
        runtime->radioQueue.clear();
    tokens = value ? BUDGET_MS : 0;
    lastBudget = millis();
    return persist();
}

bool BitchatBridge::ensureRuntime()
{
    if (runtime)
        return true;
    if (!platformBluetooth.isActive())
        return false;
    memoryRequest("bridge-runtime", sizeof(Runtime) + sizeof(PeerTable) + sizeof(DedupCache<BITCHAT_DEDUP_ENTRIES>) + bleBufferSize(),
                  std::max(sizeof(Runtime), sizeof(DedupCache<BITCHAT_DEDUP_ENTRIES>)));
    if (allocateBuffer(runtime) && allocateBuffer(runtime->peers) && allocateBuffer(runtime->dedup) && bleAllocateBuffers()) {
        runtimeAttachment = bleAttachments();
        bootDiagnostic("bridge-buffers-ready");
        return true;
    }
    releaseRuntime();
    resourceError = true;
    bootDiagnostic("bridge-memory-refused");
    return false;
}

bool BitchatBridge::startBasicMode(uint8_t channel)
{
    Digest fingerprint{};
    if (channel != NO_CHANNEL && !binding(channel, fingerprint))
        return false;
    resourceError = false;
    if (!ensureRuntime())
        return false;
    if (!initIdentity()) {
        releaseRuntime();
        return false;
    }
    settings.channel = channel;
    settings.binding = fingerprint;
    settings.localOnly = channel == NO_CHANNEL;
    settings.enabled = true;
    defaultChannelUnavailable = false;
    textConflict = false;
    textGuardAt = millis();
    runtime->radioQueue.clear();
    tokens = BUDGET_MS;
    lastBudget = millis();
    const bool saved = persist();
    refreshChannel();
    return saved;
}

bool BitchatBridge::setLink(bool value)
{
    const uint8_t stored = value;
    if (!storageReady || storage.putBytes("link", &stored, 1) != 1)
        return false;
    link = value;
    return true;
}

bool BitchatBridge::setSaving(bool value)
{
    const uint8_t stored = value;
    if (!storageReady || storage.putBytes("saver", &stored, 1) != 1)
        return false;
    saver = value;
    return true;
}

bool BitchatBridge::setPrefixed(bool value)
{
    const uint8_t stored = value;
    if (!storageReady || storage.putBytes("prefix", &stored, 1) != 1)
        return false;
    prefixOnly = value;
    return true;
}

bool BitchatBridge::setSosPosition(bool value)
{
    const uint8_t stored = value;
    if (!storageReady || storage.putBytes("sospos", &stored, 1) != 1)
        return false;
    sosPosition = value;
    return true;
}

bool BitchatBridge::nextMode()
{
    if (!settings.enabled) {
        // Back on with LoRa: the public channel is provided again on the next tick.
        settings.localOnly = 0;
        defaultChannelAt = millis() - 10000;
        return setEnabled(true);
    }
    if (!settings.localOnly)
        return startBasicMode();
    return setEnabled(false);
}

const char *BitchatBridge::modeName() const
{
    return !settings.enabled ? "Off" : settings.localOnly ? "Bitchat only" : "Bitchat + LoRa";
}

void BitchatBridge::counters(unsigned &phones, unsigned &people, uint32_t &out, uint32_t &in) const
{
    phones = unsigned(blePhones());
    people = unsigned(runtime ? runtime->peers->size() : 0);
    out = submittedRadio;
    in = importedRadio;
}

void BitchatBridge::releaseRuntime()
{
    // The keys of the private chats are not left behind in freed memory.
    if (runtime && runtime->privates)
        for (auto &session : runtime->privates->slots) {
            session.handshake.wipe();
            session = {};
        }
    bleReleaseBuffers();
    runtime.reset();
    // A payment on its way goes with the runtime that carried it.
    payment = {};
}

bool BitchatBridge::eligibleChannel(uint8_t index) const
{
    char name[32];
    uint8_t secret[32];
    return host && host->readChannel(index, name, secret) && name[0] != 0 &&
           !std::equal(std::begin(MESH_PUBLIC_KEY), std::end(MESH_PUBLIC_KEY), secret);
}

bool BitchatBridge::binding(uint8_t index, Digest &out) const
{
    char name[32];
    uint8_t secret[32];
    if (!eligibleChannel(index) || !host->readChannel(index, name, secret))
        return false;
    bindingOf(secret, out);
    return true;
}

void BitchatBridge::refreshChannel()
{
    Digest current;
    const bool hadText = textReady;
    textReady = settings.channel != NO_CHANNEL && binding(settings.channel, current) && current == settings.binding;
    channelsAt = millis();
    // Other bridges learn about this one from the proof in its next announcement.
    if (textReady != hadText)
        announcedAt = channelsAt - 60000;
    if (textReady && textKeyBinding != settings.binding) {
        char name[32];
        uint8_t secret[32];
        host->readChannel(settings.channel, name, secret);
        static const char info[] = "meshcore-bitchat/text/v1";
        hkdf<SHA256>(textKey.data(), textKey.size(), secret, 32, settings.binding.data(), 32, info, sizeof(info) - 1);
        textKeyBinding = settings.binding;
    }
}

bool BitchatBridge::textGuard() const
{
    return Throttle::isWithinTimespanMsAt(textGuardAt, GUARD_MS, millis());
}

const char *BitchatBridge::status() const
{
    if (platformBluetooth.startupFailed)
        return "Bluetooth error";
    if (!storageReady)
        return "Storage error";
    if (resourceError)
        return "Low memory: bridge OFF";
    if (!settings.enabled)
        return "Bridge is off";
    if (!platformBluetooth.isActive())
        return "Bluetooth is off";
    if (!identityReady)
        return "Starting...";
    if (!utc())
        return "Clock not set";
    if (settings.channel != NO_CHANNEL && !textReady)
        return "LoRa channel changed";
    if (settings.channel == NO_CHANNEL && !settings.localOnly && defaultChannelUnavailable)
        return "No LoRa channel";
    if (textConflict)
        return "Standby: other bridge";
    if (textReady && textGuard()) {
        // A countdown, so the wait is visibly moving.
        const uint32_t left = (GUARD_MS - uint32_t(millis() - textGuardAt) + 999) / 1000;
        snprintf(guardText, sizeof(guardText), "Starting LoRa: %lu s", static_cast<unsigned long>(left));
        return guardText;
    }
    if (nativeWindow) {
        const uint32_t left = (NATIVE_WINDOW_MS - uint32_t(millis() - bluetoothAt) + 999) / 1000;
        snprintf(guardText, sizeof(guardText), "Bitchat starts: %lu s", static_cast<unsigned long>(left));
        return guardText;
    }
    return textReady ? "Ready: Bitchat + LoRa" : "Ready: Bitchat only";
}

// What the operator can do about the current status; empty when nothing is needed.
const char *BitchatBridge::hint() const
{
    if (platformBluetooth.startupFailed || !storageReady || resourceError)
        return "Restart the radio";
    if (!settings.enabled)
        return "To turn it on, send: !bridge on";
    if (!platformBluetooth.isActive())
        return "Turn Bluetooth on at the radio";
    if (identityReady && !utc())
        return "Open Bitchat nearby, or connect this app again";
    if (settings.channel != NO_CHANNEL && !textReady)
        return "Send !bridge lora in the channel to use";
    if (settings.channel == NO_CHANNEL && !settings.localOnly && defaultChannelUnavailable)
        return "Free a channel slot in the app";
    return "";
}

// The clock the bridge is using and why phone packets were refused.
void BitchatBridge::diagnostics(char *out, size_t size) const
{
    const uint32_t seconds = uint32_t(utc() / 1000);
    const auto count = [this](Result result) { return static_cast<unsigned long>(rejected[static_cast<size_t>(result)]); };
    snprintf(out, size, "UTC %02lu:%02lu:%02lu %s. rx %lu ok %lu tx %lu. old %lu sig %lu fmt %lu. ver %lu pol %lu full %lu",
             static_cast<unsigned long>(seconds / 3600 % 24), static_cast<unsigned long>(seconds / 60 % 60),
             static_cast<unsigned long>(seconds % 60),
             trustedUtc()            ? "app"
             : peerClockFromRadio && peerClockUtc ? "lora"
             : peerClockUtc          ? "phone"
                                     : "none",
             static_cast<unsigned long>(receivedBle), static_cast<unsigned long>(acceptedBle),
             static_cast<unsigned long>(originatedBle), count(Result::Expired) + count(Result::NoClock),
             count(Result::Unauthenticated), count(Result::Malformed) + count(Result::Oversize) + count(Result::Incomplete),
             count(Result::Unsupported), count(Result::Policy), count(Result::Full));
}

static void displayCount(uint32_t value, char out[8])
{
    const char *suffix = "";
    if (value >= 1000000000) {
        value /= 1000000000;
        suffix = "G";
    } else if (value >= 1000000) {
        value /= 1000000;
        suffix = "M";
    } else if (value >= 100000) {
        value /= 1000;
        suffix = "k";
    }
    snprintf(out, 8, "%lu%s", static_cast<unsigned long>(value), suffix);
}

// Shown only in the app attached to this radio, as a channel message from "bridge".
void BitchatBridge::replyToApp(const char *text)
{
    char line[LORA_TEXT_LIMIT + 1];
    const int size = snprintf(line, sizeof(line), "%s%s", APP_SENDER, text);
    if (size > 0)
        host->showChannelText(appChannel, host->clockSeconds(), line, std::min<size_t>(size, LORA_TEXT_LIMIT));
}

// Self-test asked from the MeshCore app: what the bridge sees, then what to do about it.
void BitchatBridge::answerApp()
{
    const Command command = appCommand;
    appCommand = Command::None;
    char text[LORA_TEXT_LIMIT + 1];
    if (command == Command::Reset) {
        restartLinks();
        replyToApp("Bluetooth restarts in 3 s. Then open Bitchat near the radio and wait 2 min.");
        return;
    }
    if (command == Command::Help) {
        replyToApp("!bridge shows the state. Also: !bridge on, off, local (Bluetooth only), lora (Bitchat + LoRa on the "
                   "channel where you send it), reset.");
        replyToApp("Options: !bridge link on, link off (text from Meshtastic bridges); economy on, economy off (slower "
                   "Bluetooth while no Bitchat phone is near).");
        replyToApp("!bridge guide explains the bridge and !bridge who lists who was heard on LoRa, here or in Bitchat. In "
                   "Portuguese: !ponte guia, !ponte quem.");
        replyToApp("Here or in Bitchat: !bridge note <text> (a note for each phone that arrives; note off clears it), "
                   "battery, sos <text>, test.");
        replyToApp("Only here: !bridge radios (who was heard), prefix on/off (only !lora text to LoRa), position on/off "
                   "(SOS from Bitchat with position).");
        replyToApp("Only here: !bridge sound on/off, light on/off (the radio's buzzer and status light).");
        replyToApp("In Portuguese: !ponte recado, bateria, radios, sos, teste, posicao on, som off, luz off.");
        return;
    }
    if (command == Command::Guide || command == Command::GuidePt) {
        for (size_t line = 0; line < GUIDE_LINES; ++line)
            replyToApp(guideLine(command == Command::GuidePt, true, guideMode(), line));
        return;
    }
    const bool portuguese = appPortuguese;
    const size_t room = sizeof(text) - strlen(APP_SENDER);
    if (command == Command::PrefixOn || command == Command::PrefixOff) {
        const bool value = command == Command::PrefixOn;
        if (!setPrefixed(value))
            replyToApp(portuguese ? "Não mudou: erro de memória" : "Not changed: Storage error");
        else if (portuguese)
            replyToApp(value ? "Prefixo ligado: só o texto do #mesh iniciado por !lora vai ao LoRa; o resto fica no Bitchat."
                             : "Prefixo desligado: todo o texto público do #mesh vai ao LoRa.");
        else
            replyToApp(value ? "Prefix on: only #mesh text starting with !lora goes to LoRa; the rest stays in Bitchat."
                             : "Prefix off: all public #mesh text goes to LoRa.");
        return;
    }
    if (command == Command::SoundOn || command == Command::SoundOff || command == Command::LightOn ||
        command == Command::LightOff) {
        const bool sound = command == Command::SoundOn || command == Command::SoundOff;
        const bool value = command == Command::SoundOn || command == Command::LightOn;
        if (!(sound ? host->setSound(value) : host->setLight(value)))
            replyToApp(sound ? (portuguese ? "Este rádio não tem som." : "This radio has no buzzer.")
                             : (portuguese ? "Este rádio não tem luz de aviso." : "This radio has no status light."));
        else if (portuguese)
            replyToApp(sound ? (value ? "Som ligado: o rádio apita com mensagens, quando o app MeshCore não está conectado."
                                      : "Som desligado: o rádio não apita mais. Vale também depois de reiniciar.")
                             : (value ? "Luz ligada: a luz de aviso pisca como de costume."
                                      : "Luz desligada: a luz de aviso fica apagada. Vale também depois de reiniciar."));
        else
            replyToApp(sound ? (value ? "Sound on: the radio beeps for messages while the MeshCore app is not connected."
                                      : "Sound off: the radio no longer beeps. It stays so after a restart.")
                             : (value ? "Light on: the status light blinks as usual."
                                      : "Light off: the status light stays dark. It stays so after a restart."));
        return;
    }
    if (command == Command::PositionOn || command == Command::PositionOff) {
        const bool value = command == Command::PositionOn;
        if (!setSosPosition(value))
            replyToApp(portuguese ? "Não mudou: erro de memória" : "Not changed: Storage error");
        else if (portuguese)
            replyToApp(value ? "Posição ligada: um SOS pedido pelo Bitchat leva a posição do rádio a todos no canal."
                             : "Posição desligada: um SOS pedido pelo Bitchat só leva a posição se o rádio a divulga.");
        else
            replyToApp(value ? "Position on: an SOS asked from Bitchat carries the radio's position to everyone on the channel."
                             : "Position off: an SOS asked from Bitchat carries the position only if the radio shares it.");
        return;
    }
    if (command == Command::Note || command == Command::NoteOff) {
        BridgeRequest request;
        request.command = command;
        request.portuguese = portuguese;
        request.argument = {reinterpret_cast<const uint8_t *>(appArgument), strlen(appArgument)};
        if (command == Command::Note && !noteArgument(request))
            noteText(portuguese, text, room);
        else if (!setNote(request, host->nodeName()))
            snprintf(text, room, "%s", portuguese ? "Não mudou: erro de memória" : "Not changed: Storage error");
        else if (command == Command::NoteOff)
            snprintf(text, room, "%s", portuguese ? "Recado apagado." : "Note cleared.");
        else
            snprintf(text, room, "%s",
                     portuguese ? "Recado guardado: cada telefone com Bitchat que chegar o recebe, uma vez a cada 12 h."
                                : "Note kept: each Bitchat phone that arrives gets it, once every 12 h.");
        replyToApp(text);
        return;
    }
    if (command == Command::Battery || command == Command::Radios) {
        if (command == Command::Battery)
            batteryText(portuguese, text, room);
        else
            radiosText(portuguese, text, room);
        replyToApp(text);
        return;
    }
    if (command == Command::Sos) {
        BridgeRequest request;
        request.command = command;
        request.portuguese = portuguese;
        request.argument = {reinterpret_cast<const uint8_t *>(appArgument), strlen(appArgument)};
        sos(request, host->nodeName(), true, text, room);
        replyToApp(text);
        return;
    }
    if (command == Command::Test) {
        startRangeTest(portuguese, true, text, room);
        replyToApp(text);
        return;
    }
    if (command == Command::Who || command == Command::WhoPt) {
        whoText(command == Command::WhoPt, text, sizeof(text) - strlen(APP_SENDER));
        replyToApp(text);
        return;
    }
    if (command == Command::EconomyOn || command == Command::EconomyOff) {
        const bool value = command == Command::EconomyOn;
        if (!setSaving(value))
            replyToApp("Not changed: Storage error");
        else
            replyToApp(value ? "Economy on: after 3 min with no Bitchat phone the radio announces itself once a second. "
                               "Apps may take longer to find it."
                             : "Economy off: the radio always announces itself at full speed.");
        return;
    }
    if (command == Command::LinkOn || command == Command::LinkOff) {
        const bool value = command == Command::LinkOn;
        if (!setLink(value))
            replyToApp("Not changed: Storage error");
        else
            replyToApp(value ? "Link on: text a Meshtastic bridge brings to Bitchat also goes to this channel."
                             : "Link off: text from Meshtastic bridges stays in Bitchat.");
        return;
    }
    if (command != Command::Status) {
        bool done = false;
        const char *result = "";
        if (command == Command::Off) {
            done = setEnabled(false);
            result = "Bridge turned off. Send !bridge on to turn it back on.";
        } else if (command == Command::On) {
            done = setEnabled(true);
            result = "Bridge turned on. Send !bridge to see its state.";
        } else if (command == Command::Local) {
            done = startBasicMode();
            result = "Bitchat only: phones are relayed by Bluetooth, nothing goes to LoRa.";
        } else if (!eligibleChannel(appChannel)) {
            replyToApp("Not changed: the Public channel cannot be used. Send !bridge lora in another channel.");
            return;
        } else {
            done = startBasicMode(appChannel);
            result = "Bitchat + LoRa on this channel.";
        }
        if (done)
            snprintf(text, sizeof(text), "%s", result);
        else
            snprintf(text, sizeof(text), "Not changed: %s", status());
        replyToApp(text);
        return;
    }
    const unsigned phones = unsigned(blePhones()), people = unsigned(runtime ? runtime->peers->size() : 0);
    char tx[8], rx[8], missed[8], name[32] = "off";
    uint8_t secret[32];
    if (textReady)
        host->readChannel(settings.channel, name, secret);
    displayCount(submittedRadio, tx);
    displayCount(importedRadio, rx);
    displayCount(drops(), missed);
    snprintf(text, sizeof(text), "%s. Bluetooth %u, Bitchat %u, people %u. LoRa %.16s%s out %s in %s lost %s. Link %s. Economy %s. Kept %u",
             status(), unsigned(bleConnections()), phones, people, name, prefixOnly ? " (!lora)" : "", tx, rx, missed,
             link ? "on" : "off", !saver ? "off" : economy ? "active" : "on", unsigned(kept()));
    replyToApp(text);
    diagnostics(text, sizeof(text));
    replyToApp(text);
    // One line per message: the counters, then each Bluetooth link.
    char links[280];
    bleDescribe(links, sizeof(links));
    for (char *line = links; line && *line;) {
        char *end = strchr(line, '\n');
        if (end)
            *end = 0;
        replyToApp(line);
        line = end ? end + 1 : nullptr;
    }
    if (hint()[0])
        snprintf(text, sizeof(text), "%s", hint());
    else if (!wasActive)
        snprintf(text, sizeof(text), "The bridge is not running: %s", status());
    else if (!phones)
        snprintf(text, sizeof(text), "No Bitchat app has talked to this radio. Open Bitchat nearby and wait 2 min. If it "
                                     "stays at 0, send: !bridge reset");
    else if (!people)
        snprintf(text, sizeof(text), "Bitchat is talking but nobody was accepted: see old, sig and fmt above.");
    else
        snprintf(text, sizeof(text), "Bitchat link OK. Send !bridge in Bitchat #mesh to test the way back.");
    replyToApp(text);
}

// Self-test asked from Bitchat: seeing this answer proves both directions of the Bluetooth link.
void BitchatBridge::answerBitchat()
{
    char text[BITCHAT_TEXT_LIMIT + 1], tx[8], rx[8], missed[8];
    displayCount(submittedRadio, tx);
    displayCount(importedRadio, rx);
    displayCount(drops(), missed);
    const int size = snprintf(text, sizeof(text), "%s%s. Phones %u, people %u. LoRa out %s, in %s, lost %s", REPLY_PREFIX,
                              status(), unsigned(blePhones()), unsigned(runtime->peers->size()), tx, rx, missed);
    if (privateReply) {
        replyPrivately(text, std::min<size_t>(size, BITCHAT_TEXT_LIMIT));
        return;
    }
    if (!Throttle::isWithinTimespanMsAt(announcedAt, 5000, millis()))
        announce();
    originate(2, {reinterpret_cast<const uint8_t *>(text), std::min<size_t>(size, BITCHAT_TEXT_LIMIT)});
}

// The guide asked from Bitchat: a few signed messages, for people who never see the radio's own app.
void BitchatBridge::guideBitchat(bool portuguese)
{
    if (!privateReply && !Throttle::isWithinTimespanMsAt(announcedAt, 5000, millis()))
        announce();
    for (size_t line = 0; line < GUIDE_LINES; ++line) {
        char text[BITCHAT_TEXT_LIMIT + 1];
        const int size = snprintf(text, sizeof(text), "%s%s", REPLY_PREFIX, guideLine(portuguese, false, guideMode(), line));
        if (privateReply)
            replyPrivately(text, std::min<size_t>(size, BITCHAT_TEXT_LIMIT));
        else
            originate(2, {reinterpret_cast<const uint8_t *>(text), std::min<size_t>(size, BITCHAT_TEXT_LIMIT)});
    }
}

// What snprintf wrote, limited to the buffer.
static size_t fitted(int size, size_t capacity)
{
    return size < 0 || !capacity ? 0 : std::min<size_t>(size_t(size), capacity - 1);
}

GuideMode BitchatBridge::guideMode() const
{
    return !textReady ? GuideMode::NoLora : prefixOnly ? GuideMode::OnlyPrefixed : GuideMode::AllText;
}

void BitchatBridge::tellBitchat(const char *text, size_t size, bool keep)
{
    if (!runtime || !identityReady)
        return;
    if (privateReply) {
        replyPrivately(text, size);
        return;
    }
    char line[BITCHAT_TEXT_LIMIT + 1];
    const size_t length = fitted(snprintf(line, sizeof(line), "%s%.*s", REPLY_PREFIX, int(size), text), sizeof(line));
    if (!Throttle::isWithinTimespanMsAt(announcedAt, 5000, millis()))
        announce();
    originate(2, {reinterpret_cast<const uint8_t *>(line), length}, keep);
}

// Anyone nearby may ask, so answers are spaced out. The settings stay with the app attached to the radio:
// for those, Bitchat gets the state.
void BitchatBridge::answerAsked(const BridgeRequest &asked, const Peer &peer, bool direct, uint32_t now)
{
    const bool portuguese = asked.portuguese;
    char text[BITCHAT_TEXT_LIMIT + 1];
    const size_t room = sizeof(text) - (sizeof(REPLY_PREFIX) - 1);
    size_t size = 0;
    if (asked.command == Command::Guide || asked.command == Command::GuidePt) {
        // In a private chat the guide goes to the one who asked, every time.
        if (privateReply || !Throttle::isWithinTimespanMsAt(guidedAt, 30000, now)) {
            if (!privateReply)
                guidedAt = now;
            guideBitchat(asked.command == Command::GuidePt);
        }
        return;
    }
    // A help request is not held back by someone else's question; it has its own spacing.
    // Only a request that can go out now; refusals wait like any other answer.
    const bool urgent = asked.command == Command::Sos && wasActive && textReady && !textConflict && !textGuard() &&
                        runtime->radioQueue.size() < RadioItem::QUEUE &&
                        !(sosSent && Throttle::isWithinTimespanMsAt(sosAt, SOS_EVERY_MS, now));
    // A private chat has its own spacing (BridgePrivate.cpp).
    if (!urgent && !privateReply && Throttle::isWithinTimespanMsAt(repliedAt, 5000, now))
        return;
    if (!privateReply)
        repliedAt = now;
    char label[LABEL_LIMIT + 8];
    switch (asked.command) {
    case Command::Who:
    case Command::WhoPt:
        size = whoText(asked.command == Command::WhoPt, text, room);
        break;
    case Command::Battery:
        size = batteryText(portuguese, text, room);
        break;
    case Command::Radios:
        // The list says who the radio talks to and when: it stays with the radio's own app.
        size = fitted(snprintf(text, room, "%s",
                               portuguese ? "Rádios: só no aplicativo do próprio rádio, por privacidade."
                                          : "Radios: only in the radio's own app, for privacy."),
                      room);
        break;
    case Command::Test:
        size = startRangeTest(portuguese, false, text, room);
        break;
    case Command::Sos:
        peerLabel(peer, label, sizeof(label));
        size = sos(asked, label, false, text, room);
        break;
    case Command::Note:
    case Command::NoteOff:
        if (asked.command == Command::Note && !noteArgument(asked)) {
            size = noteText(portuguese, text, room);
        } else if (!direct) {
            // Only someone next to the radio changes the note, not a phone further along the Bitchat mesh.
            size = fitted(snprintf(text, room, "%s",
                                   portuguese ? "Para trocar o recado, fique perto do rádio."
                                              : "To change the note, be next to the radio."),
                          room);
        } else if (noteChanged && Throttle::isWithinTimespanMsAt(noteAt, NOTE_CHANGE_MS, now)) {
            // Anyone nearby may change it; each change is a write to flash and goes to every phone again.
            size = fitted(snprintf(text, room, "%s",
                                   portuguese ? "O recado mudou há pouco; pode ser trocado de novo em 10 min."
                                              : "The note changed a moment ago; it can change again in 10 min."),
                          room);
        } else {
            peerLabel(peer, label, sizeof(label));
            const bool saved = setNote(asked, label);
            if (saved) {
                noteChanged = true;
                noteAt = now;
            }
            size = fitted(snprintf(text, room, "%s",
                                   !saved                                 ? (portuguese ? "Recado não guardado: erro de memória."
                                                                                        : "Note not kept: storage error.")
                                   : asked.command == Command::NoteOff    ? (portuguese ? "Recado apagado." : "Note cleared.")
                                   : portuguese ? "Recado guardado: quem chegar com o Bitchat o recebe."
                                                : "Note kept: whoever arrives with Bitchat gets it."),
                          room);
        }
        break;
    default:
        answerBitchat();
        return;
    }
    tellBitchat(text, size);
}

size_t BitchatBridge::batteryText(bool portuguese, char *out, size_t capacity) const
{
    const uint16_t millivolts = host->batteryMilliVolts();
    const int percent = batteryPercent(millivolts);
    if (percent < 0)
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Bateria: este rádio não informa a carga." : "Battery: this radio does not report it."),
                      capacity);
    return fitted(snprintf(out, capacity, portuguese ? "Bateria: %u,%02u V, cerca de %d%%." : "Battery: %u.%02u V, about %d%%.",
                           unsigned(millivolts / 1000), unsigned(millivolts % 1000 / 10), percent),
                  capacity);
}

static size_t ageText(uint32_t seconds, bool portuguese, char *out, size_t capacity)
{
    if (seconds < 60)
        return fitted(snprintf(out, capacity, "%s", portuguese ? "agora" : "now"), capacity);
    if (seconds < 3600)
        return fitted(snprintf(out, capacity, "%lu min", static_cast<unsigned long>(seconds / 60)), capacity);
    return fitted(snprintf(out, capacity, "%lu h", static_cast<unsigned long>(seconds / 3600)), capacity);
}

// "Casa Ana (direct, 5 min), RPT Sul (2 hops, 1 h) +3": the radios heard in the last day, the latest first.
size_t BitchatBridge::radiosText(bool portuguese, char *out, size_t capacity) const
{
    static constexpr size_t MOST = 16;
    static constexpr uint32_t DAY = 86400;
    MeshHost::HeardRadio radios[MOST];
    const size_t found = std::min(host->heardRadios(radios, MOST), MOST);
    std::array<bool, MOST> listed{};
    size_t left = 0;
    for (size_t i = 0; i < found; ++i)
        left += radios[i].secondsAgo < DAY;
    if (!left)
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Nenhum rádio foi ouvido no LoRa nas últimas 24 h."
                                          : "No radio was heard on LoRa in the last 24 h."),
                      capacity);
    size_t size = fitted(snprintf(out, capacity, "%s", portuguese ? "Rádios ouvidos: " : "Radios heard: "), capacity);
    const size_t head = size;
    while (left) {
        size_t latest = MOST;
        for (size_t i = 0; i < found; ++i)
            if (!listed[i] && radios[i].secondsAgo < DAY && (latest == MOST || radios[i].secondsAgo < radios[latest].secondsAgo))
                latest = i;
        const auto &radio = radios[latest];
        char name[LABEL_LIMIT + 1], age[16], hops[16] = "", snr[16] = "", item[96];
        if (!cleanLabel({reinterpret_cast<const uint8_t *>(radio.name), strnlen(radio.name, sizeof(radio.name))}, name,
                        sizeof(name)))
            snprintf(name, sizeof(name), "?");
        ageText(radio.secondsAgo, portuguese, age, sizeof(age));
        if (radio.hops == 0)
            snprintf(hops, sizeof(hops), "%s, ", portuguese ? "direto" : "direct");
        else if (radio.hops > 0)
            snprintf(hops, sizeof(hops), "%d %s, ", radio.hops,
                     radio.hops == 1 ? (portuguese ? "salto" : "hop") : (portuguese ? "saltos" : "hops"));
        if (radio.snrTenths != SNR_UNKNOWN) {
            char value[8];
            formatTenths(radio.snrTenths, value, sizeof(value), portuguese);
            snprintf(snr, sizeof(snr), ", %s dB", value);
        }
        const size_t length = fitted(snprintf(item, sizeof(item), "%s%s (%s%s%s)", size > head ? ", " : "", name, hops, age, snr),
                                     sizeof(item));
        // room for this one and, if others stay out, for how many they are
        if (size + length + (left > 1 ? 5 : 0) >= capacity)
            break;
        memcpy(out + size, item, length);
        size += length;
        out[size] = 0;
        listed[latest] = true;
        --left;
    }
    if (left && size + 5 < capacity)
        size += fitted(snprintf(out + size, capacity - size, " +%u", unsigned(left)), capacity - size);
    return size;
}

size_t BitchatBridge::noteText(bool portuguese, char *out, size_t capacity) const
{
    if (!note.text[0])
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Não há recado. Para deixar um: !ponte recado <texto>"
                                          : "No note. To leave one: !bridge note <text>"),
                      capacity);
    if (!note.author[0])
        return fitted(snprintf(out, capacity, "%s%s", portuguese ? "Recado: " : "Note: ", note.text), capacity);
    return fitted(snprintf(out, capacity, portuguese ? "Recado de %s: %s" : "Note from %s: %s", note.author, note.text), capacity);
}

bool BitchatBridge::setNote(const BridgeRequest &request, const char *author)
{
    Note next{};
    if (request.command == Command::Note) {
        if (!cleanText(request.argument, next.text, sizeof(next.text), sizeof(next.text) - 1))
            return false;
        cleanText({reinterpret_cast<const uint8_t *>(author), strlen(author)}, next.author, sizeof(next.author),
                  sizeof(next.author) - 1);
        next.portuguese = request.portuguese;
    }
    if (!storageReady || storage.putBytes("note", &next, sizeof(next)) != sizeof(next))
        return false;
    note = next;
    // A new note goes to everyone again.
    if (runtime)
        runtime->delivered = {};
    return true;
}

// The note goes to the arriving phone alone, after the bridge's announcement so the phone can check it,
// and with a TTL that keeps other phones from relaying it.
void BitchatBridge::deliverNote(const Packet &packet, uint16_t source)
{
    const uint32_t now = millis();
    if (source == NO_LINK || !utc())
        return;
    auto *slot = &runtime->delivered[0];
    for (auto &entry : runtime->delivered) {
        if (entry.used && entry.peer == packet.sender) {
            slot = &entry;
            break;
        }
        if (slot->used && (!entry.used || int32_t(entry.at - slot->at) < 0))
            slot = &entry;
    }
    if (slot->used && slot->peer == packet.sender && Throttle::isWithinTimespanMsAt(slot->at, NOTE_EVERY_MS, now))
        return;
    char text[BITCHAT_TEXT_LIMIT + 1];
    const size_t prefix = fitted(snprintf(text, sizeof(text), "%s", REPLY_PREFIX), sizeof(text));
    const size_t size = prefix + noteText(note.portuguese, text + prefix, sizeof(text) - prefix);
    if (!Throttle::isWithinTimespanMsAt(announcedAt, 5000, now))
        announce();
    if (!sign(2, {reinterpret_cast<const uint8_t *>(text), size}, 1) || !bleSendTo(runtime->working.bytes(), source))
        return;
    ++originatedBle;
    *slot = {packet.sender, now, true};
}

// "-23.55052": five decimals, about a metre.
static size_t coordinate(int32_t millionths, char *out, size_t capacity)
{
    const uint32_t magnitude = millionths < 0 ? uint32_t(-int64_t(millionths)) : uint32_t(millionths);
    return fitted(snprintf(out, capacity, "%s%lu.%05lu", millionths < 0 ? "-" : "", static_cast<unsigned long>(magnitude / 1000000),
                           static_cast<unsigned long>(magnitude % 1000000 / 10)),
                  capacity);
}

size_t BitchatBridge::sos(const BridgeRequest &request, const char *who, bool fromApp, char *out, size_t capacity)
{
    const bool portuguese = request.portuguese;
    const uint32_t now = millis();
    if (!runtime || !wasActive || !textReady || textConflict)
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "SOS não enviado: esta ponte não está ligada ao LoRa."
                                          : "SOS not sent: this bridge is not on LoRa."),
                      capacity);
    if (sosSent && Throttle::isWithinTimespanMsAt(sosAt, SOS_EVERY_MS, now)) {
        const unsigned left = unsigned((SOS_EVERY_MS - uint32_t(now - sosAt) + 59999) / 60000);
        return fitted(snprintf(out, capacity,
                               portuguese ? "SOS já enviado há pouco; pode ser repetido em %u min."
                                          : "SOS sent a moment ago; it can be repeated in %u min.",
                               left),
                      capacity);
    }
    char said[81], where[40] = "", body[LORA_TEXT_LIMIT], label[LABEL_LIMIT + 1];
    if (!cleanText(request.argument, said, sizeof(said), sizeof(said) - 1))
        snprintf(said, sizeof(said), "%s", portuguese ? "pedido de socorro" : "help needed");
    int32_t latitude = 0, longitude = 0;
    // From Bitchat anyone nearby can ask, so the position goes only where the operator shares it anyway.
    const bool allowed = fromApp || sosPosition || host->positionShared();
    const bool located = allowed && host->position(latitude, longitude) && (latitude || longitude);
    if (located) {
        char lat[16], lon[16];
        coordinate(latitude, lat, sizeof(lat));
        coordinate(longitude, lon, sizeof(lon));
        snprintf(where, sizeof(where), " (%s, %s)", lat, lon);
    }
    const size_t bodySize = fitted(snprintf(body, sizeof(body), "SOS: %s%s", said, where), sizeof(body));
    size_t labelSize = cleanLabel({reinterpret_cast<const uint8_t *>(who), strlen(who)}, label, sizeof(label));
    if (!labelSize)
        labelSize = fitted(snprintf(label, sizeof(label), "bridge"), sizeof(label));
    if (!queueRadioText({reinterpret_cast<const uint8_t *>(label), labelSize}, {reinterpret_cast<const uint8_t *>(body), bodySize}))
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "SOS não enviado: o LoRa está ocupado; tente de novo em instantes."
                                          : "SOS not sent: LoRa is busy; try again shortly."),
                      capacity);
    sosSent = true;
    sosAt = now;
    return fitted(snprintf(out, capacity, "%s",
                           located ? (portuguese ? "SOS enviado ao LoRa, com a posição do rádio."
                                                 : "SOS sent to LoRa, with the radio's position.")
                           : !allowed ? (portuguese ? "SOS enviado ao LoRa, sem posição: este rádio não a divulga."
                                                    : "SOS sent to LoRa, without a position: this radio does not share it.")
                                      : (portuguese ? "SOS enviado ao LoRa, sem posição: o rádio não sabe onde está."
                                                    : "SOS sent to LoRa, without a position: the radio does not know it.")),
                  capacity);
}

size_t BitchatBridge::startRangeTest(bool portuguese, bool fromApp, char *out, size_t capacity)
{
    const uint32_t now = millis();
    if (!runtime || !wasActive || !textReady || textConflict)
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Teste não enviado: esta ponte não está ligada ao LoRa."
                                          : "Test not sent: this bridge is not on LoRa."),
                      capacity);
    if (rangeTest.pending)
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Já há um teste em curso; o resultado sai em até 2 min."
                                          : "A test is already running; results come within 2 min."),
                      capacity);
    uint32_t random = 0;
    esp_fill_random(&random, sizeof(random));
    const uint16_t id = uint16_t(1000 + random % 9000);
    char body[24], label[LABEL_LIMIT + 1];
    const size_t bodySize = fitted(snprintf(body, sizeof(body), "%s%04u", TEST_MARK, unsigned(id)), sizeof(body));
    const char *name = host->nodeName();
    size_t labelSize = cleanLabel({reinterpret_cast<const uint8_t *>(name), strlen(name)}, label, sizeof(label));
    if (!labelSize)
        labelSize = fitted(snprintf(label, sizeof(label), "bridge"), sizeof(label));
    if (!queueRadioText({reinterpret_cast<const uint8_t *>(label), labelSize}, {reinterpret_cast<const uint8_t *>(body), bodySize}))
        return fitted(snprintf(out, capacity, "%s",
                               portuguese ? "Teste não enviado: o LoRa está ocupado; tente de novo em instantes."
                                          : "Test not sent: LoRa is busy; try again shortly."),
                      capacity);
    rangeTest.id = id;
    rangeTest.at = now;
    rangeTest.pending = true;
    rangeTest.fromApp = fromApp;
    rangeTest.privately = !fromApp && privateReply;
    if (privateReply)
        rangeTest.asker = privateReply->peer;
    rangeTest.portuguese = portuguese;
    rangeTest.channel = appChannel;
    rangeTest.answers = 0;
    return fitted(snprintf(out, capacity,
                           portuguese ? "Teste %04u enviado ao LoRa. As pontes que o ouvirem respondem; resultado em até 2 min."
                                      : "Test %04u sent to LoRa. Bridges that hear it answer; results within 2 min.",
                           unsigned(id)),
                  capacity);
}

// A probe from another bridge is answered once with how it arrived; an answer to this radio's probe is
// reported to whoever asked for it.
void BitchatBridge::rangeTestHeard(Bytes name, uint16_t id, bool answer, int16_t theirSnr, int16_t snrTenths)
{
    const uint32_t now = millis();
    char label[LABEL_LIMIT + 1];
    size_t labelSize = cleanLabel(name, label, sizeof(label));
    if (!labelSize)
        labelSize = fitted(snprintf(label, sizeof(label), "mesh"), sizeof(label));
    noteHeard(label, labelSize);
    char heard[8], back[8];
    if (!answer) {
        if (rangeTest.pending && id == rangeTest.id)
            return;
        if (rangeTest.answered && (rangeTest.answeredId == id || Throttle::isWithinTimespanMsAt(rangeTest.answeredAt, 20000, now)) &&
            Throttle::isWithinTimespanMsAt(rangeTest.answeredAt, TEST_WAIT_MS, now))
            return;
        char body[48], own[LABEL_LIMIT + 1];
        formatTenths(snrTenths, heard, sizeof(heard), false);
        const size_t bodySize =
            fitted(snprintf(body, sizeof(body), "%s%04u ouvido %s dB", TEST_MARK, unsigned(id), heard), sizeof(body));
        const char *ownName = host->nodeName();
        size_t ownSize = cleanLabel({reinterpret_cast<const uint8_t *>(ownName), strlen(ownName)}, own, sizeof(own));
        if (!ownSize)
            ownSize = fitted(snprintf(own, sizeof(own), "bridge"), sizeof(own));
        if (queueRadioText({reinterpret_cast<const uint8_t *>(own), ownSize}, {reinterpret_cast<const uint8_t *>(body), bodySize})) {
            rangeTest.answered = true;
            rangeTest.answeredId = id;
            rangeTest.answeredAt = now;
        }
        return;
    }
    if (!rangeTest.pending || id != rangeTest.id)
        return;
    ++rangeTest.answers;
    // a shorter name keeps the whole result in one Bitchat message
    char shortName[21];
    if (!cleanLabel(name, shortName, sizeof(shortName)))
        snprintf(shortName, sizeof(shortName), "%s", "mesh");
    formatTenths(theirSnr, heard, sizeof(heard), rangeTest.portuguese);
    formatTenths(snrTenths, back, sizeof(back), rangeTest.portuguese);
    char text[BITCHAT_TEXT_LIMIT];
    snprintf(text, sizeof(text),
             rangeTest.portuguese ? "Teste %04u: %s ouviu com %s dB; a volta chegou com %s dB."
                                  : "Test %04u: %s heard it at %s dB; the answer came at %s dB.",
             unsigned(id), shortName, heard, back);
    reportRangeTest(text);
}

void BitchatBridge::reportRangeTest(const char *text)
{
    if (rangeTest.fromApp) {
        const uint8_t asked = appChannel;
        appChannel = rangeTest.channel;
        replyToApp(text);
        appChannel = asked;
    } else if (rangeTest.privately) {
        // Back to the private chat it was asked in; never to the public one.
        PrivateSession *session = runtime ? privateSession(rangeTest.asker, millis(), false) : nullptr;
        if (session && session->ready) {
            privateReply = session;
            tellBitchat(text, strlen(text));
            privateReply = nullptr;
        }
    } else {
        tellBitchat(text, strlen(text));
    }
}

// Low battery is told to the phones nearby once, and again only after the radio was charged.
void BitchatBridge::watchBattery(uint32_t now)
{
    if (Throttle::isWithinTimespanMsAt(batteryAt, 60000, now))
        return;
    batteryAt = now;
    const uint16_t millivolts = host->batteryMilliVolts();
    if (millivolts > BATTERY_RECOVERED_MV) {
        batteryWarned = false;
        return;
    }
    // without a clock the warning could not be signed; it waits for one
    if (batteryWarned || millivolts < 2500 || millivolts >= BATTERY_LOW_MV || !bleListeners() || !utc())
        return;
    char text[BITCHAT_TEXT_LIMIT];
    const size_t size = fitted(snprintf(text, sizeof(text), "Bateria baixa / Battery low: %u,%02u V",
                                        unsigned(millivolts / 1000), unsigned(millivolts % 1000 / 10)),
                               sizeof(text));
    tellBitchat(text, size);
    batteryWarned = true;
}

// A radio with a GPS but no time from the app or a phone turns the GPS on for a while; once the clock is
// trusted the GPS goes back to the operator's choice.
void BitchatBridge::watchGps(uint32_t now)
{
    if (clockTrusted) {
        if (gpsOn) {
            host->clockFromGps(false);
            gpsOn = false;
        }
        return;
    }
    if (gpsOn) {
        if (!Throttle::isWithinTimespanMsAt(gpsAt, GPS_FOR_MS, now)) {
            host->clockFromGps(false);
            gpsOn = false;
            gpsAt = now;
        }
        return;
    }
    // A phone's time is enough for the bridge; the GPS is only for a radio with no time at all.
    // A time from LoRa adverts is not a trusted one: the GPS still gives the real time.
    if (!settings.enabled || (utc() && !peerClockFromRadio) ||
        (gpsTried ? Throttle::isWithinTimespanMsAt(gpsAt, GPS_AGAIN_MS, now) : now < GPS_AFTER_MS))
        return;
    gpsTried = true;
    gpsAt = now;
    gpsOn = host->clockFromGps(true);
}

void BitchatBridge::reject(Result result)
{
    if (result != Result::Ok && result != Result::Incomplete)
        ++rejected[static_cast<size_t>(result)];
}

uint32_t BitchatBridge::drops() const
{
    // Duplicates and policy refusals are normal mesh traffic, not lost messages.
    return bleDrops() + lost;
}

bool BitchatBridge::seen(const Digest &id) const
{
    const uint64_t now = utc();
    for (const auto &entry : journal)
        if (entry.utc && std::equal(entry.id.begin(), entry.id.end(), id.begin()) &&
            (entry.utc > now || now - entry.utc < JOURNAL_MS))
            return true;
    return false;
}

bool BitchatBridge::record(const Digest &id, bool persist)
{
    const uint64_t now = utc();
    if (!storageReady || !now || seen(id))
        return false;
    // A full journal gives up its oldest entry: refusing new messages would silence the bridge.
    size_t slot = 0;
    for (size_t i = 1; i < journal.size(); ++i)
        if (journal[i].utc < journal[slot].utc)
            slot = i;
    auto &entry = journal[slot];
    std::copy_n(id.begin(), entry.id.size(), entry.id.begin());
    entry.utc = now;
    // Text kept for an absent phone is remembered in memory only: nothing is written to flash for nobody.
    if (!persist)
        return true;
    char key[8];
    snprintf(key, sizeof(key), "j%02u", unsigned(slot));
    if (storage.putBytes(key, &entry, sizeof(entry)) == sizeof(entry))
        return true;
    storageReady = false;
    advertisingChanged = true;
    return false;
}

// Two messages in the same instant still need distinct timestamps; a corrected clock must not keep an old lead.
uint64_t BitchatBridge::nextTimestamp()
{
    uint64_t timestamp = utc();
    if (lastOriginTimestamp >= timestamp && lastOriginTimestamp - timestamp < 1000)
        timestamp = lastOriginTimestamp + 1;
    lastOriginTimestamp = timestamp;
    return timestamp;
}

bool BitchatBridge::sign(uint8_t type, Bytes payload, uint8_t ttl)
{
    if (!identityReady || !utc()) {
        reject(Result::NoClock);
        return false;
    }
    const uint64_t timestamp = nextTimestamp();
    if (encode(type, ownId, nullptr, timestamp, ttl, payload, {}, runtime->working) != Result::Ok ||
        signingBytes(runtime->working.bytes(), runtime->canonical) != Result::Ok)
        return false;
    std::array<uint8_t, 64> signature;
    Ed25519::sign(signature.data(), identity.data(), signingPublic.data(), runtime->canonical.data.data(),
                  runtime->canonical.size);
    if (encode(type, ownId, nullptr, timestamp, ttl, payload, {signature.data(), signature.size()}, runtime->working) !=
        Result::Ok)
        return false;
    Digest id;
    packetDigest(crypto, runtime->working.bytes(), id);
    if (runtime->dedup->insert(id, millis(), DEDUP_MS) != Result::Ok) {
        reject(Result::Full);
        return false;
    }
    return true;
}

void BitchatBridge::originate(uint8_t type, Bytes payload, bool keep)
{
    if (!sign(type, payload, 7))
        return;
    // Kept even with nobody listening: the phone that arrives later asks for what it lacks.
    if (keep && BITCHAT_HISTORY_ENTRIES && allocateBuffer(runtime->history))
        runtime->history->keep(crypto, runtime->working.bytes());
    if (!bleBroadcast(runtime->working.bytes()))
        reject(Result::Full);
    else
        ++originatedBle;
}

void BitchatBridge::noteHeard(const char *name, size_t size)
{
    const uint32_t now = millis();
    auto *slot = &runtime->heard[0];
    for (auto &entry : runtime->heard) {
        if (entry.used && strlen(entry.name.data()) == size && !memcmp(entry.name.data(), name, size)) {
            slot = &entry;
            break;
        }
        // otherwise a free place, or the one heard longest ago
        if (slot->used && (!entry.used || int32_t(entry.at - slot->at) < 0))
            slot = &entry;
    }
    slot->name.fill(0);
    memcpy(slot->name.data(), name, std::min(size, slot->name.size() - 1));
    slot->at = now;
    slot->used = true;
}

size_t BitchatBridge::heardNames(char *out, size_t capacity) const
{
    const uint32_t now = millis();
    std::array<bool, 12> listed{};
    size_t size = 0, left = 0;
    for (const auto &entry : runtime->heard)
        left += entry.used && Throttle::isWithinTimespanMsAt(entry.at, uint32_t(HISTORY_MS), now);
    out[0] = 0;
    while (left) {
        const Runtime::Heard *latest = nullptr;
        size_t index = 0;
        for (size_t i = 0; i < runtime->heard.size(); ++i) {
            const auto &entry = runtime->heard[i];
            if (!listed[i] && entry.used && Throttle::isWithinTimespanMsAt(entry.at, uint32_t(HISTORY_MS), now) &&
                (!latest || int32_t(entry.at - latest->at) > 0)) {
                latest = &entry;
                index = i;
            }
        }
        // room for this name, its separator and, if others stay out, the count of them
        const size_t need = strlen(latest->name.data()) + (size ? 2 : 0), tail = left > 1 ? 4 : 0;
        if (size + need + tail >= capacity)
            break;
        size += snprintf(out + size, capacity - size, "%s%s", size ? ", " : "", latest->name.data());
        listed[index] = true;
        --left;
    }
    if (left && size + 4 < capacity)
        size += snprintf(out + size, capacity - size, " +%u", unsigned(left));
    return size;
}

size_t BitchatBridge::whoText(bool portuguese, char *out, size_t capacity) const
{
    const size_t size = snprintf(out, capacity, "%s", portuguese ? "No LoRa, últimas 6 h: " : "On LoRa, last 6 h: ");
    if (!runtime || !heardNames(out + size, capacity - size))
        return snprintf(out, capacity, "%s",
                        portuguese ? "Ninguém foi ouvido no LoRa nas últimas 6 h." : "Nobody was heard on LoRa in the last 6 h.");
    return strlen(out);
}

size_t BitchatBridge::kept() const
{
    return runtime && runtime->history ? runtime->history->count(utc()) : 0;
}

// A phone says what public messages it holds; the kept ones it lacks go back to it alone, a few at a
// time, the newest first. It asks again every few seconds, so the rest follows.
void BitchatBridge::answerSync(const Packet &packet, uint16_t source)
{
    static constexpr size_t BURST = 3;
    const uint32_t now = millis();
    SyncRequest request;
    if (!runtime->history || source == NO_LINK || !utc() || (packet.directed() && packet.recipient != ownId) ||
        decodeSyncRequest(packet.payload, request) != Result::Ok)
        return;
    // One answer per phone every five seconds; the table keeps the phones that asked last.
    auto *slot = &runtime->asked[0];
    for (auto &entry : runtime->asked) {
        if (entry.link == source) {
            slot = &entry;
            break;
        }
        if (slot->link != NO_LINK && (entry.link == NO_LINK || int32_t(entry.at - slot->at) < 0))
            slot = &entry;
    }
    if (slot->link == source && Throttle::isWithinTimespanMsAt(slot->at, 5000, now))
        return;
    *slot = {source, now};
    size_t cursor = 0;
    for (size_t sent = 0; sent < BURST && runtime->history->next(crypto, request, utc(), cursor, runtime->working);)
        if (bleSendTo(runtime->working.bytes(), source))
            ++sent;
        else
            break;
}

void BitchatBridge::announce()
{
    if (!utc())
        return;
    std::array<uint8_t, 150> payload{};
    char name[24];
    // The same four characters that open the radio's factory name.
    snprintf(name, sizeof(name), "LoRa bridge %04lx", static_cast<unsigned long>(host->nodeNumber() >> 16));
    size_t length = strlen(name);
    payload[0] = 1;
    payload[1] = length;
    std::copy_n(reinterpret_cast<uint8_t *>(name), length, payload.begin() + 2);
    size_t offset = length + 2;
    payload[offset++] = 2;
    payload[offset++] = 32;
    std::copy(noisePublic.begin(), noisePublic.end(), payload.begin() + offset);
    offset += 32;
    payload[offset++] = 3;
    payload[offset++] = 32;
    std::copy(signingPublic.begin(), signingPublic.end(), payload.begin() + offset);
    offset += 32;
    if (textReady) {
        payload[offset++] = GATEWAY_TLV;
        payload[offset++] = GATEWAY_PROOF;
        makeGatewayProof(crypto, ownId, host->nodeNumber(), textKey.data(), payload.data() + offset);
        offset += GATEWAY_PROOF;
    }
    announcedAt = millis();
    originate(1, {payload.data(), offset});
}

// Returns true when the frame cost a signature check, so the caller can pace the next one.
bool BitchatBridge::handleFrame(Bytes frame, uint16_t source)
{
    const uint32_t now = millis();
    Packet packet;
    Result result = decode(frame, packet);
    if (result != Result::Ok) {
        reject(result);
        return false;
    }
    if (packet.type == 0x20) {
        if (!allocateBuffer(runtime->fragments)) {
            reject(Result::Full);
            ++lost;
            return false;
        }
        result = runtime->fragments->accept(packet, now, runtime->assembled);
        if (result != Result::Ok) {
            reject(result);
            return false;
        }
        frame = runtime->assembled.bytes();
        if (decode(frame, packet) != Result::Ok)
            return false;
    }
    if (packet.type == 0x21 && packet.sender != ownId && !packet.replay()) {
        answerSync(packet, source);
        return false;
    }
    if (packet.sender == ownId || packet.type == 0x21 || packet.replay()) {
        reject(Result::Policy);
        return false;
    }
    adoptPeerClock(frame, packet);
    Digest id;
    result = packetDigest(crypto, frame, id);
    // Every phone relays what it hears, so most arrivals are copies: drop them before the signature check.
    if (result == Result::Ok && runtime->dedup->contains(id, now, DEDUP_MS))
        result = Result::Duplicate;
    if (result != Result::Ok) {
        reject(result);
        return false;
    }
    Peer *peer = nullptr;
    result = runtime->peers->authenticate(crypto, frame, utc(), now, peer);
    if (result == Result::Ok && !peer)
        result = Result::Unauthenticated;
    if (result == Result::Ok)
        result = runtime->dedup->insert(id, now, DEDUP_MS);
    if (result != Result::Ok) {
        reject(result);
        return true;
    }
    ++acceptedBle;
    uint32_t other = 0;
    // Two bridges on one channel would duplicate every text: the higher node number stands by.
    if (textReady && packet.type == 1 && gatewayProofNode(crypto, packet, host->nodeNumber(), textKey.data(), other) &&
        other < host->nodeNumber()) {
        textConflict = true;
        conflictAt = now;
    }
    if (!peer->traffic.admit(now, 30)) {
        reject(Result::Full);
        ++lost;
        return true;
    }
    // Addressed to this bridge: a private chat. Not passed on; no phone does.
    if (packet.directed() && packet.recipient == ownId) {
        if (packet.type == 0x10 || packet.type == 0x11)
            privateFrame(packet, *peer, source, now);
        return true;
    }
    if (relay(frame, runtime->working) == Result::Ok) {
        if (bleBroadcast(runtime->working.bytes(), source))
            ++forwardedBle;
    }
    if (packet.type == 1 && !peer->announcement.gateway)
        rememberAnnounce(packet, frame);
    // A phone that arrives, announcing itself directly to this radio, is given the house note.
    if (packet.type == 1 && packet.ttl == 7 && note.text[0] && !peer->announcement.gateway)
        deliverNote(packet, source);
    const bool publicMessage = packet.type == 2 && !packet.directed();
    BridgeRequest asked;
    if (publicMessage && !packet.compressed())
        parseBridgeCommand(packet.payload, asked);
    if (asked.command != Command::None) {
        answerAsked(asked, *peer, packet.ttl == 7 && source != NO_LINK, now);
        return true;
    }
    // A Cashu token travels whole by its own way, or not at all: never as text, never in pieces.
    if (publicMessage && !peer->announcement.gateway && carryCashu(packet, frame, *peer, id))
        return true;
    // Like any phone that stays in the house, the bridge keeps what it heard for whoever arrives later. The
    // newcomer shows a message only if it has met the author before: the app accepts a peer's announce for
    // fifteen minutes and then relies on the identities it remembers.
    if (publicMessage && BITCHAT_HISTORY_ENTRIES && allocateBuffer(runtime->history))
        runtime->history->keep(crypto, frame);
    if (textReady && publicMessage)
        exportText(packet, *peer, id);
    return true;
}

size_t BitchatBridge::peerLabel(const Peer &peer, char *out, size_t capacity) const
{
    size_t size = cleanLabel({reinterpret_cast<const uint8_t *>(peer.announcement.nickname.data()),
                              strnlen(peer.announcement.nickname.data(), peer.announcement.nickname.size())},
                             out, std::min(capacity, LABEL_LIMIT - 4));
    // Same rule as the Bitchat app: equal nicknames are told apart by the start of the peer ID.
    if (!size || runtime->peers->nicknameShared(peer))
        size += snprintf(out + size, 6, "#%02x%02x", peer.id[0], peer.id[1]);
    return size;
}

// The bridge's own text for LoRa (a help request, a range test), sent as "label: body" in one message.
bool BitchatBridge::queueRadioText(Bytes label, Bytes body)
{
    TextPlan plan;
    NativeText native;
    if (!runtime || !textReady || textConflict || textGuard() || planNativeText(label.size, body, plan) != Result::Ok ||
        plan.count != 1 || makeNativeText(label, body, plan, 0, native) != Result::Ok ||
        runtime->radioQueue.size() >= RadioItem::QUEUE)
        return false;
    RadioItem item;
    item.size = uint16_t(native.size);
    item.queuedAt = millis();
    std::copy_n(native.data.begin(), native.size, item.payload.begin());
    return runtime->radioQueue.push(item);
}

void BitchatBridge::exportText(const Packet &packet, Peer &peer, const Digest &id)
{
    Bytes body;
    char label[LABEL_LIMIT + 8];
    size_t labelSize = 0;
    Result result;
    if (peer.announcement.gateway) {
        // Text from a bridge of this network already came from LoRa: sending it back would loop. What a
        // bridge of the other network brought crosses to this one, once.
        result = link ? foreignTextForLoRa(packet, label, sizeof(label), labelSize, body) : Result::Policy;
    } else {
        result = publicTextForLoRa(packet, body);
    }
    if (result == Result::Ok && textConflict)
        result = Result::Policy;
    // A token never goes to LoRa as text, whoever brought it: only whole, by the tunnel.
    if (result == Result::Ok && mentionsCashu(packet.payload))
        result = Result::Policy;
    // With the prefix on, only what the person marked goes to LoRa; text other bridges bring is not theirs to mark.
    if (result == Result::Ok && prefixOnly && !peer.announcement.gateway && !startsWith(packet.payload, LORA_PREFIX))
        result = Result::Policy;
    if (result == Result::Ok && seen(id))
        result = Result::Duplicate;
    if (result != Result::Ok) {
        reject(result);
        return;
    }
    if (!peer.announcement.gateway)
        labelSize = peerLabel(peer, label, sizeof(label));
    TextPlan plan;
    result = planNativeText(labelSize, body, plan);
    if (result == Result::Ok && (textGuard() || runtime->radioQueue.size() + plan.count > RadioItem::QUEUE ||
                                 !peer.lora.admit(millis(), 6, 6000) || !record(id)))
        result = Result::Full;
    if (result != Result::Ok) {
        reject(result);
        ++lost;
        return;
    }
    for (size_t part = 0; part < plan.count; ++part) {
        NativeText native;
        if (makeNativeText({reinterpret_cast<const uint8_t *>(label), labelSize}, body, plan, part, native) != Result::Ok)
            continue;
        RadioItem item;
        item.size = uint16_t(native.size);
        item.queuedAt = millis();
        std::copy_n(native.data.begin(), native.size, item.payload.begin());
        if (!runtime->radioQueue.push(item))
            reject(Result::Full);
    }
}

// Text for the Bitchat side, shown as "[MC name] text" under the bridge's own identity.
void BitchatBridge::importText(Bytes name, Bytes body, const Digest *id)
{
    char label[LABEL_LIMIT + 1];
    size_t labelSize = cleanLabel(name, label, sizeof(label));
    if (!labelSize)
        labelSize = snprintf(label, sizeof(label), "mesh");
    // only what was heard on the air has a record identifier
    if (id)
        noteHeard(label, labelSize);
    TextPlan plan;
    const Result result = planBitchatText(labelSize, body, plan);
    if (result != Result::Ok || (id && !record(*id, blePhones() > 0))) {
        reject(result == Result::Ok ? Result::Full : result);
        ++lost;
        return;
    }
    // A phone that just connected may not know the bridge key yet; its messages would be discarded.
    if (bleListeners() && !Throttle::isWithinTimespanMsAt(announcedAt, 5000, millis()))
        announce();
    for (size_t part = 0; part < plan.count; ++part) {
        NativeText text;
        if (makeBitchatText({reinterpret_cast<const uint8_t *>(label), labelSize}, body, plan, part, text) == Result::Ok)
            originate(2, text.bytes(), true);
    }
    // Only what was heard on LoRa counts as received from it; app text has no record identifier.
    if (id)
        ++importedRadio;
}

void BitchatBridge::channelText(uint8_t channel, const uint8_t *packetHash, const char *text, int16_t snrTenths)
{
    if (!host || !text || !settings.enabled || !runtime || !wasActive || !storageReady || !identityReady || !utc())
        return;
    refreshChannel();
    if (!textReady || textConflict || textGuard() || channel != settings.channel)
        return;
    const size_t size = strlen(text);
    // MeshCore channel text is "sender: text" by convention.
    const char *split = strstr(text, ": ");
    Bytes name{reinterpret_cast<const uint8_t *>(text), split ? size_t(split - text) : 0};
    Bytes body{reinterpret_cast<const uint8_t *>(split ? split + 2 : text), split ? size - name.size - 2 : size};
    // A range test between bridges is answered or reported here; it is not text for Bitchat.
    uint16_t testId = 0;
    bool testAnswer = false;
    int16_t theirSnr = SNR_UNKNOWN;
    if (name.size && parseRangeTest(body, testId, testAnswer, theirSnr)) {
        rangeTestHeard(name, testId, testAnswer, theirSnr, snrTenths);
        return;
    }
    // With no phone connected the text is still signed and kept, for whoever arrives in the next hours.
    if (!blePhones() && !BITCHAT_HISTORY_ENTRIES)
        return;
    // Commands are for the app attached to a radio; one heard on the air is neither obeyed nor relayed.
    // A note or a help request typed on another radio is that person's text, and goes on as such.
    BridgeRequest heardCommand;
    if (!body.size || (parseBridgeCommand(body, heardCommand) && heardCommand.command != Command::Sos &&
                       heardCommand.command != Command::Note && heardCommand.command != Command::NoteOff))
        return;
    // A token cut to fit LoRa text is useless and looks like money, and its later pieces no longer
    // carry its name: tokens cross only whole, by the tunnel.
    if (tokenLike(body)) {
        reject(Result::Policy);
        return;
    }
    std::array<uint8_t, 42> material{};
    material[0] = 'M';
    material[1] = 'C';
    std::copy(settings.binding.begin(), settings.binding.end(), material.begin() + 2);
    std::copy_n(packetHash, 8, material.begin() + 34);
    Digest id;
    crypto.hash({material.data(), material.size()}, id);
    if (seen(id)) {
        reject(Result::Duplicate);
        return;
    }
    importText(name, body, &id);
}

bool BitchatBridge::appText(uint8_t channel, const char *text, size_t size)
{
    if (!host || !text)
        return false;
    const Bytes body{reinterpret_cast<const uint8_t *>(text), strnlen(text, size)};
    BridgeRequest request;
    if (parseBridgeCommand(body, request)) {
        // Answered on the next tick, in the channel where it was typed.
        appCommand = request.command;
        appChannel = channel;
        appPortuguese = request.portuguese;
        const size_t length = std::min(request.argument.size, sizeof(appArgument) - 1);
        if (length)
            memcpy(appArgument, request.argument.data, length);
        appArgument[length] = 0;
        interval = 0;
        return true;
    }
    if (!settings.enabled || !runtime || !wasActive || !storageReady || !identityReady || !utc())
        return false;
    refreshChannel();
    // Typed on the bridge's channel, a token would go on the air as text anyone with the key may spend.
    if (textReady && channel == settings.channel && mentionsCashu(body)) {
        appChannel = channel;
        replyToApp("Não enviado: tokens Cashu vão pelo Bitchat (chat público), não pelo canal do MeshCore.");
        return true;
    }
    if (textReady && !textConflict && !textGuard() && channel == settings.channel && (blePhones() || BITCHAT_HISTORY_ENTRIES) &&
        body.size) {
        const char *name = host->nodeName();
        importText({reinterpret_cast<const uint8_t *>(name), strlen(name)}, body, nullptr);
    }
    return false;
}

void BitchatBridge::sendRadio()
{
    auto *item = runtime->radioQueue.front();
    // A confirmation or a request for a missing part goes first: the payer's two minutes are running.
    if ((!item || runtime->tunnelControl.size()) && tunnelDue()) {
        if (!host->radioBusy() && runtime->radioGate.admit(millis(), 1500))
            sendTunnel();
        return;
    }
    if (!item)
        return;
    if (!Throttle::isWithinTimespanMsAt(item->queuedAt, RADIO_DEADLINE_MS, millis()) || !textReady || textConflict) {
        reject(Result::Expired);
        ++lost;
        runtime->radioQueue.pop();
        return;
    }
    if (host->radioBusy() || !runtime->radioGate.admit(millis(), 1500))
        return;
    const uint32_t cost = host->airtimeMs(item->size);
    if (cost > tokens)
        return;
    // The bridge's own clock dates the message: the firmware's may never have been set.
    if (!host->sendChannelText(settings.channel, uint32_t(utc() / 1000), reinterpret_cast<const char *>(item->payload.data()),
                               item->size)) {
        reject(Result::Full);
        return;
    }
    tokens -= cost;
    runtime->radioQueue.pop();
    ++submittedRadio;
}

// The three entry points hand their work to a stack large enough for it; see onLargeStack.
void BitchatBridge::loop()
{
    if (!host)
        return;
    const uint32_t now = millis();
    if (Throttle::isWithinTimespanMsAt(ranAt, interval, now))
        return;
    ranAt = now;
    interval = 1000;
    struct Call {
        BitchatBridge *self;
        uint32_t now;
    } call{this, now};
    onLargeStack(
        [](void *argument) {
            auto *c = static_cast<Call *>(argument);
            c->self->work(c->now);
        },
        &call);
}

void BitchatBridge::onChannelText(uint8_t channel, const uint8_t *packetHash, const char *text, int16_t snrTenths)
{
    struct Call {
        BitchatBridge *self;
        uint8_t channel;
        const uint8_t *hash;
        const char *text;
        int16_t snr;
    } call{this, channel, packetHash, text, snrTenths};
    onLargeStack(
        [](void *argument) {
            auto *c = static_cast<Call *>(argument);
            c->self->channelText(c->channel, c->hash, c->text, c->snr);
        },
        &call);
}

bool BitchatBridge::onAppText(uint8_t channel, const char *text, size_t size)
{
    struct Call {
        BitchatBridge *self;
        uint8_t channel;
        const char *text;
        size_t size;
        bool command;
    } call{this, channel, text, size, false};
    onLargeStack(
        [](void *argument) {
            auto *c = static_cast<Call *>(argument);
            c->command = c->self->appText(c->channel, c->text, c->size);
        },
        &call);
    return call.command;
}

void BitchatBridge::work(uint32_t now)
{
    watchClock(now);
    watchGps(now);
    const bool bluetoothReady = platformBluetooth.isActive();
    if (runtime && bleAttachments() != runtimeAttachment) {
        // The Bluetooth stack was restarted between two ticks (WadaMesh does it around an update over
        // Wi-Fi): the old server and its links are gone, so the bridge starts over on the new one.
        releaseRuntime();
        advertisingChanged = true;
    }
    if (bluetoothReady && !resourceError)
        applyDefaults();
    if (!Throttle::isWithinTimespanMsAt(defaultChannelAt, 10000, now)) {
        defaultChannelAt = now;
        ensureDefaultChannel();
    }
    if (settings.enabled && storageReady && bluetoothReady && !resourceError && ensureRuntime() && !identityReady)
        initIdentity();
    const bool active = settings.enabled && storageReady && identityReady && runtime && bluetoothReady;
    if (!active && runtime)
        releaseRuntime();
    if (active != wasActive) {
        advertisingChanged = true;
        tokens = active ? BUDGET_MS : 0;
        lastBudget = now;
        wasActive = active;
        phoneAt = now;
    }
    if (active && blePhones())
        phoneAt = now;
    const bool quiet = saver && active && !Throttle::isWithinTimespanMsAt(phoneAt, ECONOMY_AFTER_MS, now);
    if (quiet != economy) {
        economy = quiet;
        advertisingChanged = true;
    }
    if (bluetoothReady && !bluetoothWas)
        bluetoothAt = now;
    bluetoothWas = bluetoothReady;
    const bool window = bluetoothReady && Throttle::isWithinTimespanMsAt(bluetoothAt, NATIVE_WINDOW_MS, now);
    if (window != nativeWindow) {
        nativeWindow = window;
        advertisingChanged = true;
    }
    if (appCommand != Command::None) {
        // A command may turn the bridge off or change its mode, so this tick ends here.
        answerApp();
        interval = 20;
        return;
    }
    if (restart == 1 && bluetoothReady) {
        bleServicesChanged();
        restart = 2;
        restartAt = now;
    } else if (restart == 2 && !Throttle::isWithinTimespanMsAt(restartAt, 3000, now)) {
        restart = 0;
        if (bluetoothReady)
            bleDropConnections();
        advertisingChanged = true;
    }
    if (advertisingChanged) {
        bleConfigure(active, nativeWindow, economy);
        if (bluetoothReady)
            platformBluetooth.refreshBridgeAdvertising();
        advertisingChanged = false;
    }
    if (!active)
        return;
    interval = economy ? 200 : 20;
    keepClock(now);
    watchBattery(now);
    if (rangeTest.pending && !Throttle::isWithinTimespanMsAt(rangeTest.at, TEST_WAIT_MS, now)) {
        rangeTest.pending = false;
        if (!rangeTest.answers) {
            char text[BITCHAT_TEXT_LIMIT];
            snprintf(text, sizeof(text),
                     rangeTest.portuguese ? "Teste %04u: nenhuma outra ponte respondeu em 2 min."
                                          : "Test %04u: no other bridge answered within 2 min.",
                     unsigned(rangeTest.id));
            reportRangeTest(text);
        }
    }
    if (!Throttle::isWithinTimespanMsAt(channelsAt, 1000, now))
        refreshChannel();
    if (textConflict && !Throttle::isWithinTimespanMsAt(conflictAt, CONFLICT_MS, now)) {
        textConflict = false;
        textGuardAt = now;
    }
    const uint32_t elapsed = now - lastBudget;
    if (elapsed >= 1000) {
        // LoRa airtime the bridge allows itself: 10% of the time, in bursts of at most 12 s.
        tokens = std::min<uint32_t>(BUDGET_MS, tokens + std::min<uint32_t>(elapsed, 60000) / 10);
        lastBudget = now;
    }
    // Signing costs CPU: announce to a phone that just subscribed, then only while someone listens.
    if (bleNeedsAnnouncement() || (bleListeners() && !Throttle::isWithinTimespanMsAt(announcedAt, 20000, now)))
        announce();
    if (runtime->cryptoGate.admit(now, 30))
        for (unsigned frames = 0; frames < 4 && bleReceive(runtime->input); ++frames) {
            ++receivedBle;
            if (handleFrame(runtime->input.frame.bytes(), runtime->input.link))
                break;
        }
    blePoll();
    watchPayment(now);
    if (runtime->fragments) {
        runtime->fragments->expire(now);
        if (runtime->fragments->empty())
            runtime->fragments.reset();
    }
    sendRadio();
}
#endif

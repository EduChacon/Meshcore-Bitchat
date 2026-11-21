// End-to-end host test: the production bridge and Bluetooth adapter, simulated phones and a simulated MeshCore companion.
#include "HeapModel.h"
#include "cashu_vectors.h"
#include "gcs_reference.h"
#include "helpers/bitchat/BitchatBridge.h"
#include "helpers/bitchat/BridgeNoise.h"
#include "helpers/bitchat/BridgePlatform.h"
#include <Curve25519.h>
#include <NimBLEDevice.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace bitchat;
static BridgeCrypto crypto;
static size_t checks = 0;
static void check(bool passed, int line, const char *expression)
{
    ++checks;
    if (!passed) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}
#define CHECK(expression) check((expression), __LINE__, #expression)

// The companion's Bluetooth interface, reduced to what the bridge asks of it.
static unsigned refreshes = 0;
namespace bitchat
{
PlatformBluetooth platformBluetooth;
void PlatformBluetooth::refreshBridgeAdvertising()
{
    ++refreshes;
    bleAdvertise();
}
} // namespace bitchat

static constexpr uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                           0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
// sha256("#bitchat"), first 16 bytes: what the MeshCore app derives from the channel name.
static constexpr uint8_t BITCHAT_KEY[16] = {0xd6, 0xdd, 0x19, 0xd7, 0x38, 0x6a, 0xaa, 0x32,
                                            0x21, 0x88, 0xaf, 0x44, 0x23, 0x40, 0x58, 0x01};

struct Line {
    uint8_t channel;
    uint32_t timestamp;
    std::string text;
};
struct FakeMesh : MeshHost {
    struct Channel {
        std::string name;
        std::array<uint8_t, 32> secret{};
    };
    std::array<Channel, 8> channels;
    // As a companion that was never told the time: a fixed date in 2024, advancing.
    uint32_t clockBase = 1715770351, clockSetAt = 0;
    uint32_t node = 0x00c0ffee;
    std::string name = "R\xc3\xa1"
                       "dio Sul";
    std::vector<Line> radio, app;
    bool busy = false, refuse = false;
    unsigned saves = 0;
    void setClock(uint32_t seconds)
    {
        clockBase = seconds;
        clockSetAt = millis();
    }
    uint32_t clockSeconds() override { return clockBase + (millis() - clockSetAt) / 1000; }
    uint32_t nodeNumber() override { return node; }
    const char *nodeName() override { return name.c_str(); }
    uint8_t provideChannel(const char *label, const uint8_t secret[32]) override
    {
        int free = -1;
        for (size_t i = 0; i < channels.size(); ++i) {
            if (channels[i].name.empty()) {
                if (free < 0 && i > 0)
                    free = int(i);
            } else if (std::equal(channels[i].secret.begin(), channels[i].secret.end(), secret)) {
                return uint8_t(i);
            }
        }
        if (free < 0)
            return NO_CHANNEL;
        channels[free].name = label;
        std::copy_n(secret, 32, channels[free].secret.begin());
        ++saves;
        return uint8_t(free);
    }
    bool readChannel(uint8_t index, char label[32], uint8_t secret[32]) override
    {
        if (index >= channels.size() || channels[index].name.empty())
            return false;
        snprintf(label, 32, "%s", channels[index].name.c_str());
        std::copy(channels[index].secret.begin(), channels[index].secret.end(), secret);
        return true;
    }
    uint32_t airtimeMs(size_t) override { return 1000; }
    bool radioBusy() override { return busy; }
    bool sendChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size) override
    {
        if (refuse)
            return false;
        radio.push_back({index, timestamp, std::string(text, size)});
        showChannelText(index, timestamp, text, size);
        return true;
    }
    void showChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size) override
    {
        app.push_back({index, timestamp, std::string(text, size)});
    }
    // Channel datagrams the bridge sent: the parts and answers of the Cashu tunnel.
    std::vector<std::vector<uint8_t>> data;
    bool sendChannelData(uint8_t index, uint16_t type, const uint8_t *bytes, size_t size) override
    {
        if (refuse || index != 1 || type != TUNNEL_DATA_TYPE || size > 165)
            return false;
        data.emplace_back(bytes, bytes + size);
        return true;
    }
    // What a radio may know about itself; nothing until a test sets it.
    uint16_t battery = 0;
    std::vector<HeardRadio> heard;
    bool located = false, gps = false;
    int32_t latitude = 0, longitude = 0;
    std::vector<bool> gpsCalls;
    uint16_t batteryMilliVolts() override { return battery; }
    // The radio's MeshCore identity, and the radios it knows as contacts (key prefix, public key, name).
    std::array<uint8_t, 32> nodeSecret{}, nodePublic{};
    bool identity = false;
    struct Contact {
        std::array<uint8_t, 32> key;
        std::string name;
        bool favourite = false;
    };
    std::vector<Contact> contacts;
    bool nodeKeyPrefix(uint8_t prefix[8]) override
    {
        if (identity)
            std::copy_n(nodePublic.begin(), 8, prefix);
        return identity;
    }
    bool signAsNode(const uint8_t *data, size_t size, uint8_t signature[64]) override
    {
        if (identity)
            Ed25519::sign(signature, nodeSecret.data(), nodePublic.data(), data, size);
        return identity;
    }
    NodeCheck verifyNode(const uint8_t prefix[8], const uint8_t *data, size_t size, const uint8_t signature[64],
                         char label[32]) override
    {
        for (const auto &c : contacts)
            if (std::equal(c.key.begin(), c.key.begin() + 8, prefix)) {
                if (!Ed25519::verify(signature, c.key.data(), data, size))
                    return NodeCheck::Invalid;
                snprintf(label, 32, "%s", c.name.c_str());
                return NodeCheck::Valid;
            }
        return NodeCheck::Unknown;
    }
    // Direct messages: the contacts above, by name; what the bridge sent.
    struct Direct {
        std::array<uint8_t, 8> to;
        std::string text;
    };
    std::vector<Direct> directs;
    size_t directLimit = 160;
    bool findContact(const char *text, size_t size, LoraContact &out, size_t &consumed, bool &ambiguous) override
    {
        size_t best = 0;
        ambiguous = false;
        for (const auto &c : contacts) {
            if (!c.favourite)
                continue;
            const size_t n = c.name.size();
            if (!n || size <= n || (text[n] != ' ' && text[n] != ':') || n < best)
                continue;
            bool same = true;
            for (size_t i = 0; i < n && same; ++i)
                same = tolower((unsigned char)text[i]) == tolower((unsigned char)c.name[i]);
            if (!same)
                continue;
            ambiguous = n == best;
            best = n;
            std::copy_n(c.key.begin(), 8, out.id);
            snprintf(out.name, sizeof(out.name), "%s", c.name.c_str());
        }
        consumed = best;
        return best > 0;
    }
    bool favouriteContact(const uint8_t id[8]) override
    {
        for (const auto &c : contacts)
            if (c.favourite && std::equal(c.key.begin(), c.key.begin() + 8, id))
                return true;
        return false;
    }
    std::vector<uint32_t> directTimes;
    bool sendDirectText(const uint8_t id[8], uint32_t timestamp, const char *text, size_t size) override
    {
        directTimes.push_back(timestamp);
        Direct d{};
        std::copy_n(id, 8, d.to.begin());
        d.text.assign(text, size);
        directs.push_back(d);
        return true;
    }
    size_t directTextLimit() override { return directLimit; }
    // The buzzer and status light: -1 until set.
    int sound = -1, light = -1;
    bool hasAlerts = true;
    bool setSound(bool on) override
    {
        if (hasAlerts)
            sound = on;
        return hasAlerts;
    }
    bool setLight(bool on) override
    {
        if (hasAlerts)
            light = on;
        return hasAlerts;
    }
    size_t heardRadios(HeardRadio *out, size_t capacity) override
    {
        const size_t count = std::min(capacity, heard.size());
        std::copy_n(heard.begin(), count, out);
        return count;
    }
    bool shared = false;
    bool positionShared() override { return shared; }
    bool position(int32_t &lat, int32_t &lon) override
    {
        lat = latitude;
        lon = longitude;
        return located;
    }
    bool clockFromGps(bool on) override
    {
        gpsCalls.push_back(on);
        return gps;
    }
};
static MeshHost::HeardRadio radio(const char *name, int8_t hops, uint32_t secondsAgo, int16_t snr = SNR_UNKNOWN)
{
    MeshHost::HeardRadio out{};
    snprintf(out.name, sizeof(out.name), "%s", name);
    out.hops = hops;
    out.secondsAgo = secondsAgo;
    out.snrTenths = snr;
    return out;
}
static FakeMesh mesh;
static BitchatBridge *bridge;
static NimBLEServer server;
static uint64_t stamp = 0;
// The real time, which only the phones know at first.
static constexpr uint32_t EPOCH = 1795000000;

static uint64_t nowUtc()
{
    return uint64_t(EPOCH + millis() / 1000) * 1000;
}
static void run(uint32_t ms)
{
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 20) {
        fakeNow += 20;
        bridge->loop();
    }
}
static std::string text(Bytes bytes)
{
    return {reinterpret_cast<const char *>(bytes.data), bytes.size};
}
static std::vector<std::string> texts(const std::vector<Line> &lines)
{
    std::vector<std::string> out;
    for (const auto &line : lines)
        out.push_back(line.text);
    return out;
}

struct Phone {
    std::array<uint8_t, 32> signingKey{}, signingPublic{}, noiseKey{}, noisePublic{};
    PeerId id{};
    std::string nickname;
    uint16_t handle = NO_LINK;
    Phone(const char *name, uint8_t seed, uint16_t link = NO_LINK) : nickname(name), handle(link)
    {
        for (size_t i = 0; i < 32; ++i) {
            signingKey[i] = uint8_t(seed * 31 + i);
            noiseKey[i] = uint8_t(seed * 17 + 3 * i);
        }
        noiseKey[0] &= 0xf8;
        noiseKey[31] = (noiseKey[31] & 0x7f) | 0x40;
        Ed25519::derivePublicKey(signingPublic.data(), signingKey.data());
        Curve25519::eval(noisePublic.data(), noiseKey.data(), nullptr);
        Digest hash;
        crypto.hash({noisePublic.data(), noisePublic.size()}, hash);
        std::copy_n(hash.begin(), id.size(), id.begin());
    }
    Frame packet(uint8_t type, Bytes payload, uint8_t ttl = 7) const
    {
        Frame unsignedFrame, canonical, frame;
        stamp = std::max(stamp + 1, nowUtc());
        CHECK(encode(type, id, nullptr, stamp, ttl, payload, {}, unsignedFrame) == Result::Ok);
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), signingKey.data(), signingPublic.data(), canonical.data.data(), canonical.size);
        CHECK(encode(type, id, nullptr, stamp, ttl, payload, {signature.data(), signature.size()}, frame) == Result::Ok);
        return frame;
    }
    Frame announce(Bytes extra = {}, uint8_t ttl = 7) const
    {
        std::vector<uint8_t> payload{1, uint8_t(nickname.size())};
        payload.insert(payload.end(), nickname.begin(), nickname.end());
        payload.insert(payload.end(), {2, 32});
        payload.insert(payload.end(), noisePublic.begin(), noisePublic.end());
        payload.insert(payload.end(), {3, 32});
        payload.insert(payload.end(), signingPublic.begin(), signingPublic.end());
        payload.insert(payload.end(), extra.data, extra.data + extra.size);
        return packet(1, {payload.data(), payload.size()}, ttl);
    }
    Frame say(const std::string &message) const
    {
        return packet(2, {reinterpret_cast<const uint8_t *>(message.data()), message.size()});
    }
    // A long message as the app sends it, compressed (here as stored DEFLATE blocks, which any inflater reads).
    Frame sayCompressed(const std::string &message) const
    {
        std::vector<uint8_t> payload{uint8_t(message.size() >> 8), uint8_t(message.size())};
        for (size_t at = 0; at < message.size() || at == 0;) {
            const size_t n = std::min<size_t>(message.size() - at, 1000);
            const bool last = at + n == message.size();
            payload.insert(payload.end(), {uint8_t(last), uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8)});
            payload.insert(payload.end(), message.begin() + at, message.begin() + at + n);
            at += n;
            if (last)
                break;
        }
        Frame unsignedFrame, canonical, frame;
        stamp = std::max(stamp + 1, nowUtc());
        CHECK(encode(2, id, nullptr, stamp, 7, {payload.data(), payload.size()}, {}, unsignedFrame) == Result::Ok);
        unsignedFrame.data[11] |= 4;
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), signingKey.data(), signingPublic.data(), canonical.data.data(), canonical.size);
        CHECK(encode(2, id, nullptr, stamp, 7, {payload.data(), payload.size()}, {signature.data(), signature.size()}, frame) ==
              Result::Ok);
        frame.data[11] |= 4;
        return frame;
    }
    void write(const Frame &frame) const
    {
        auto &c = server.service.characteristic;
        ble_gap_conn_desc desc{handle};
        c.value.assign(reinterpret_cast<const char *>(frame.data.data()), frame.size);
        c.callbacks->onWrite(&c, &desc);
    }
    void connect() const
    {
        auto &c = server.service.characteristic;
        ble_gap_conn_desc desc{handle};
        bleConnect(handle);
        c.callbacks->onSubscribe(&c, &desc, 1);
    }
};

// Frames the bridge notified to one phone, reassembled the way the app's stream assembler does.
static std::vector<std::vector<uint8_t>> received(uint16_t handle)
{
    std::vector<uint8_t> stream;
    for (const auto &part : fakeSent)
        if (part.first == handle)
            stream.insert(stream.end(), part.second.begin(), part.second.end());
    std::vector<std::vector<uint8_t>> frames;
    size_t offset = 0, length = 0;
    while (offset < stream.size()) {
        CHECK(wireFrameLength({stream.data() + offset, stream.size() - offset}, length) == Result::Ok);
        CHECK(length <= stream.size() - offset);
        frames.emplace_back(stream.begin() + offset, stream.begin() + offset + length);
        offset += length;
    }
    return frames;
}
static std::vector<std::string> chat(uint16_t handle, const PeerId *from = nullptr)
{
    std::vector<std::string> messages;
    for (const auto &frame : received(handle)) {
        Packet packet;
        CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
        if (packet.type == 2 && (!from || packet.sender == *from))
            messages.push_back(text(packet.payload));
    }
    return messages;
}
static void fromLoRa(const std::string &message, uint8_t channel = 1, int16_t snr = SNR_UNKNOWN)
{
    static uint64_t counter = 5000;
    uint8_t hash[8];
    write64(hash, ++counter);
    bridge->onChannelText(channel, hash, message.c_str(), snr);
}
static bool contains(const std::vector<std::string> &lines, const std::string &wanted)
{
    return std::find(lines.begin(), lines.end(), wanted) != lines.end();
}
static bool fromApp(const std::string &message, uint8_t channel = 1)
{
    return bridge->onAppText(channel, message.data(), message.size());
}
static std::string token(const char *name)
{
    for (const auto &c : TOKENS)
        if (std::string(c.name) == name)
            return c.text;
    CHECK(false);
    return {};
}
static bool toBridge(const TunnelDatagram &d, uint8_t channel = 1)
{
    return bridge->onChannelData(channel, TUNNEL_DATA_TYPE, d.data.data(), d.size);
}
static bool toBridge(const std::vector<uint8_t> &d, uint8_t channel = 1)
{
    return bridge->onChannelData(channel, TUNNEL_DATA_TYPE, d.data(), d.size());
}
static constexpr uint32_t ANNOUNCE_WAIT = 45000;
static bool frameEquals(const std::vector<uint8_t> &a, Bytes b)
{
    return a.size() == b.size && std::equal(a.begin(), a.end(), b.data);
}


// A private chat as the apps hold one: Noise XX, the phone's side done with the same Noise code (checked against an
// independent implementation in test_noise.cpp), unsigned packets addressed to the other side.
struct PrivatePhone {
    const Phone &phone;
    PeerId bridge;
    NoiseHandshake handshake;
    NoiseSender send;
    NoiseReceiver receive;
    bool ready = false;
    // As the iPhone app sends a packet larger than the link takes (a T1000-E link: 244 bytes per write): the padded
    // packet cut into fragments of 202 bytes.
    bool fragments = false;
    void transmit(const Frame &frame) const
    {
        if (!fragments || frame.size <= 244) {
            phone.write(frame);
            return;
        }
        Packet whole;
        CHECK(decode(frame.bytes(), whole) == Result::Ok);
        const size_t chunk = 202, total = (frame.size + chunk - 1) / chunk;
        for (size_t index = 0; index < total; ++index) {
            std::vector<uint8_t> payload{
                9, 8, 7, 6, 5, 4, 3, 2, uint8_t(index >> 8), uint8_t(index), uint8_t(total >> 8), uint8_t(total), whole.type};
            const size_t start = index * chunk, size = std::min(chunk, frame.size - start);
            payload.insert(payload.end(), frame.data.begin() + start, frame.data.begin() + start + size);
            Frame part;
            CHECK(encode(0x20, phone.id, &bridge, whole.timestamp, 7, {payload.data(), payload.size()}, {}, part) == Result::Ok);
            phone.write(part);
        }
    }
    // The message IDs of what read() returned, in the same order.
    std::vector<std::string> ids;
    // "delivered" for one of the bridge's messages, as the app sends it.
    void ack(const std::string &id)
    {
        std::vector<uint8_t> plain{3};
        plain.insert(plain.end(), id.begin(), id.end());
        std::vector<uint8_t> sealed(plain.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(send, {plain.data(), plain.size()}, sealed.data()));
        transmit(directed(0x11, {sealed.data(), sealed.size()}));
    }
    Frame directed(uint8_t type, Bytes payload, bool signedFrame = false) const
    {
        Frame unsignedFrame, canonical, frame;
        stamp = std::max(stamp + 1, nowUtc());
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {}, unsignedFrame, true) == Result::Ok);
        if (!signedFrame)
            return unsignedFrame;
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {}, unsignedFrame) == Result::Ok);
        // As Android sends them: signed.
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), phone.signingKey.data(), phone.signingPublic.data(), canonical.data.data(),
                      canonical.size);
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {signature.data(), signature.size()}, frame, true) ==
              Result::Ok);
        return frame;
    }
    // The packets the bridge addressed to this phone, of one type, since the last clear.
    std::vector<std::vector<uint8_t>> fromBridge(uint8_t type) const
    {
        std::vector<std::vector<uint8_t>> out;
        for (const auto &frame : received(phone.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            if (p.type == type && p.sender == bridge && p.directed() && p.recipient == phone.id) {
                CHECK(!p.signedPacket() && p.ttl == 7);
                out.emplace_back(p.payload.data, p.payload.data + p.payload.size);
            }
        }
        return out;
    }
    void open(uint8_t seed, bool signedFrames = false)
    {
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(seed + 5 * i);
        handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(handshake.writeMessage1(m1));
        fakeSent.clear();
        transmit(directed(0x10, {m1, sizeof(m1)}, signedFrames));
        run(100);
        const auto answers = fromBridge(0x10);
        CHECK(answers.size() == 1 && answers[0].size() == NOISE_MESSAGE2);
        CHECK(handshake.readMessage2({answers[0].data(), answers[0].size()}));
        PeerId proved;
        noisePeerId(handshake.remoteStatic(), proved);
        CHECK(proved == bridge);
        uint8_t m3[NOISE_MESSAGE3];
        CHECK(handshake.writeMessage3(phone.noiseKey.data(), phone.noisePublic.data(), m3));
        transmit(directed(0x10, {m3, sizeof(m3)}, signedFrames));
        handshake.split(send, receive);
        ready = true;
        run(100);
    }
    void message(const std::string &id, const std::string &content)
    {
        std::vector<uint8_t> plain{1, 0, uint8_t(id.size())};
        plain.insert(plain.end(), id.begin(), id.end());
        plain.insert(plain.end(), {1, uint8_t(content.size())});
        plain.insert(plain.end(), content.begin(), content.end());
        std::vector<uint8_t> sealed(plain.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(send, {plain.data(), plain.size()}, sealed.data()));
        transmit(directed(0x11, {sealed.data(), sealed.size()}));
    }
    // What the bridge said in this chat since the last clear: delivered IDs, and the texts it sent.
    void read(std::vector<std::string> &delivered, std::vector<std::string> &said)
    {
        for (const auto &wire : fromBridge(0x11)) {
            std::vector<uint8_t> plain(wire.size());
            CHECK(noiseDecrypt(receive, {wire.data(), wire.size()}, plain.data()));
            const size_t size = wire.size() - NOISE_TRANSPORT_OVERHEAD;
            if (plain[0] == 3) {
                delivered.emplace_back(plain.begin() + 1, plain.begin() + size);
                continue;
            }
            CHECK(plain[0] == 1 && size > 41 && plain[1] == 0 && plain[2] == 36 && plain[3 + 36] == 1);
            const size_t length = plain[3 + 37];
            CHECK(3 + 38 + length == size && length <= 255);
            said.emplace_back(plain.begin() + 41, plain.begin() + 41 + length);
            ids.emplace_back(plain.begin() + 3, plain.begin() + 39);
        }
    }
};

static void privateChat(const Phone &ana, const Phone &bia, const Phone &caio, const PeerId &bridgeId)
{
    // The bridge announces the Noise key its ID stands for.
    Announcement own;
    bool found = false;
    fakeSent.clear();
    ana.write(ana.announce());
    run(6000);
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        if (p.type == 1 && p.sender == bridgeId)
            found = decodeAnnouncement(p.payload, own) == Result::Ok;
    }
    if (!found) {
        bridge->restartLinks();
        run(1);
    }
    PeerId announced;
    noisePeerId(own.noiseKey.data(), announced);
    CHECK(!found || announced == bridgeId);

    mesh.battery = 3920;
    const auto publicBefore = chat(bia.handle, &bridgeId).size();
    const auto radioBefore = mesh.radio.size();
    PrivatePhone chatAna{ana, bridgeId};
    chatAna.open(40);
    std::vector<std::string> delivered, said;
    // The command word alone is enough; the answer comes in the chat, delivered first.
    fakeSent.clear();
    chatAna.message("ID-1", "bateria");
    run(200);
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-1"});
    CHECK(said == std::vector<std::string>{"Bateria: 3,92 V, cerca de 69%."});
    // The radio's settings are not changed from Bitchat: the phone is told where they are changed. And an answer is
    // never dated before its question, even when the phone's clock runs ahead of the bridge's.
    {
        const bool tunnelBefore = bridge->tunnel();
        stamp = nowUtc() + 3000;
        fakeSent.clear();
        chatAna.message("ID-T", "túnel on");
        const uint64_t asked = stamp;
        run(200);
        std::vector<std::string> d, said;
        chatAna.read(d, said);
        CHECK(said.size() == 1 && said[0].rfind("Isso só o dono do rádio muda", 0) == 0);
        CHECK(bridge->tunnel() == tunnelBefore);
        for (const auto &frame : received(ana.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            if (p.sender == bridgeId && p.type == 0x11)
                CHECK(p.timestamp > asked);
        }
    }
    // Nothing of it in public, nothing on LoRa, and the private packets were not passed on to other phones.
    CHECK(chat(bia.handle, &bridgeId).size() == publicBefore && mesh.radio.size() == radioBefore);
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(p.sender != ana.id || (p.type != 0x10 && p.type != 0x11));
    }
    // The app sends a message again until it is told it was delivered: delivered again, answered once.
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(1100);
    chatAna.message("ID-1", "bateria");
    run(200);
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-1"} && said.empty());
    // The written command, the status, the help for anything else, and the guide in four messages.
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(1100);
    chatAna.message("ID-2", "!bridge battery");
    run(1100);
    chatAna.message("ID-3", "ponte");
    run(1100);
    chatAna.message("ID-4", "oi, tudo bem?");
    run(1100);
    chatAna.message("ID-5", "guia");
    run(200);
    chatAna.read(delivered, said);
    CHECK((delivered == std::vector<std::string>{"ID-2", "ID-3", "ID-4", "ID-5"}));
    CHECK(said.size() == 3 + GUIDE_LINES);
    CHECK(said[0] == "Battery: 3.92 V, about 69%.");
    CHECK(said[1].rfind("Ready: Bitchat + LoRa. Phones ", 0) == 0);
    CHECK(said[2].rfind("Ponte LoRa, conversa privada. Escreva: ponte (estado), bateria", 0) == 0);
    for (size_t i = 0; i < GUIDE_LINES; ++i)
        CHECK(said[3 + i] == guideLine(true, false, GuideMode::AllText, bridge->tunnel(), i));
    // Asked twice at once: both answered.
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(10000);
    chatAna.message("ID-6", "quem");
    chatAna.message("ID-7", "quem");
    run(200);
    chatAna.read(delivered, said);
    CHECK((delivered == std::vector<std::string>{"ID-6", "ID-7"}) && said.size() == 2);
    // A flood is not echoed in full: ten packets at once, then one a second. A message that could not be
    // acknowledged is not remembered, so the app's next try is answered.
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(10000);
    for (int i = 0; i < 12; ++i)
        chatAna.message("F-" + std::to_string(i), "bateria");
    run(5000);
    chatAna.read(delivered, said);
    CHECK(delivered.size() < 12 && delivered.size() + said.size() <= 16);
    std::string missed;
    for (int i = 0; i < 12 && missed.empty(); ++i)
        if (!contains(delivered, "F-" + std::to_string(i)))
            missed = "F-" + std::to_string(i);
    CHECK(!missed.empty());
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(10000);
    chatAna.message(missed, "bateria");
    run(200);
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{missed} && said.size() == 1);
    // The public chat's spacing is not touched: someone asking there right after is answered.
    fakeSent.clear();
    bia.write(bia.say("!ponte bateria"));
    run(200);
    CHECK(chat(bia.handle, &bridgeId).back() == "[bridge] Bateria: 3,92 V, cerca de 69%.");

    // Junk under Ana's ID while she is talking changes nothing.
    const auto junk = [&](int count) {
        for (int i = 0; i < count; ++i) {
            uint8_t bytes[40];
            for (int j = 0; j < 40; ++j)
                bytes[j] = uint8_t(i * 50 + j + count);
            ana.write(chatAna.directed(0x11, {bytes, sizeof(bytes)}));
            run(100);
        }
    };
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(5000);
    chatAna.message("ID-J", "bateria");
    run(200);
    junk(4);
    CHECK(chatAna.fromBridge(0x10).empty());
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-J"} && said.size() == 1);
    // A forged message: nothing delivered, nothing answered. Three in a row with nothing genuine for ten seconds:
    // the bridge starts a new handshake.
    delivered.clear();
    said.clear();
    fakeSent.clear();
    run(11000);
    for (int i = 0; i < 3; ++i) {
        uint8_t junk[40];
        for (int j = 0; j < 40; ++j)
            junk[j] = uint8_t(i * 50 + j);
        ana.write(chatAna.directed(0x11, {junk, sizeof(junk)}));
        run(100);
    }
    chatAna.read(delivered, said);
    CHECK(delivered.empty() && said.empty());
    {
        const auto restart = chatAna.fromBridge(0x10);
        CHECK(restart.size() == 1 && restart[0].size() == NOISE_MESSAGE1);
        // The phone answers it as the responder; the new session replaces the old one.
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(90 + i);
        PrivatePhone again{ana, bridgeId};
        again.handshake.start(false, e);
        CHECK(again.handshake.readMessage1({restart[0].data(), restart[0].size()}));
        uint8_t m2[NOISE_MESSAGE2];
        CHECK(again.handshake.writeMessage2(ana.noiseKey.data(), ana.noisePublic.data(), m2));
        fakeSent.clear();
        ana.write(again.directed(0x10, {m2, sizeof(m2)}));
        run(100);
        const auto third = again.fromBridge(0x10);
        CHECK(third.size() == 1 && third[0].size() == NOISE_MESSAGE3);
        CHECK(again.handshake.readMessage3({third[0].data(), third[0].size()}));
        PeerId proved;
        noisePeerId(again.handshake.remoteStatic(), proved);
        CHECK(proved == bridgeId);
        again.handshake.split(again.send, again.receive);
        fakeSent.clear();
        run(1100);
        again.message("ID-8", "bateria");
        run(200);
        delivered.clear();
        said.clear();
        again.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{"ID-8"} && said.size() == 1);
        // The old session is gone.
        fakeSent.clear();
        run(1100);
        chatAna.message("ID-9", "bateria");
        run(200);
        CHECK(again.fromBridge(0x11).empty());
    }

    // A handshake whose static key is not the one the sender's ID stands for: no session.
    {
        PrivatePhone impostor{caio, bridgeId};
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(7 * i + 1);
        impostor.handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(impostor.handshake.writeMessage1(m1));
        fakeSent.clear();
        run(1100);
        caio.write(impostor.directed(0x10, {m1, sizeof(m1)}, true));
        run(100);
        const auto answers = impostor.fromBridge(0x10);
        CHECK(answers.size() == 1 && impostor.handshake.readMessage2({answers[0].data(), answers[0].size()}));
        uint8_t m3[NOISE_MESSAGE3];
        // Ana's key, not Caio's.
        CHECK(impostor.handshake.writeMessage3(ana.noiseKey.data(), ana.noisePublic.data(), m3));
        caio.write(impostor.directed(0x10, {m3, sizeof(m3)}, true));
        impostor.handshake.split(impostor.send, impostor.receive);
        run(100);
        fakeSent.clear();
        impostor.message("ID-X", "bateria");
        run(200);
        CHECK(impostor.fromBridge(0x11).empty() && impostor.fromBridge(0x10).empty());
        // The bridge kept waiting for a genuine third message; once that wait is over, a new handshake from it.
        run(21000);
        impostor.message("ID-X2", "bateria");
        run(200);
        CHECK(impostor.fromBridge(0x11).empty());
        const auto restart = impostor.fromBridge(0x10);
        CHECK(restart.size() == 1 && restart[0].size() == NOISE_MESSAGE1);
        // Nobody answers it. Not again at once, but the chat is not stuck either: after a minute the next
        // message brings a new one.
        fakeSent.clear();
        run(30000);
        impostor.message("ID-Y", "bateria");
        run(200);
        CHECK(impostor.fromBridge(0x10).empty());
        run(31000);
        impostor.message("ID-Z", "bateria");
        run(200);
        const auto again = impostor.fromBridge(0x10);
        CHECK(again.size() == 1 && again[0].size() == NOISE_MESSAGE1);
    }

    // Signed packets, as Android sends them, work the same; both chats are kept at once. Bytes under Bia's ID
    // between the bridge's second message and her third do not end the handshake.
    {
        PrivatePhone chatBia{bia, bridgeId};
        run(11000);
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(77 + 5 * i);
        chatBia.handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(chatBia.handshake.writeMessage1(m1));
        fakeSent.clear();
        bia.write(chatBia.directed(0x10, {m1, sizeof(m1)}, true));
        run(100);
        const auto answers = chatBia.fromBridge(0x10);
        CHECK(answers.size() == 1 && chatBia.handshake.readMessage2({answers[0].data(), answers[0].size()}));
        uint8_t m3[NOISE_MESSAGE3], forged[NOISE_MESSAGE3];
        CHECK(chatBia.handshake.writeMessage3(bia.noiseKey.data(), bia.noisePublic.data(), m3));
        for (size_t i = 0; i < sizeof(forged); ++i)
            forged[i] = uint8_t(i * 3);
        bia.write(chatBia.directed(0x10, {forged, sizeof(forged)}));
        run(100);
        bia.write(chatBia.directed(0x10, {m3, sizeof(m3)}, true));
        chatBia.handshake.split(chatBia.send, chatBia.receive);
        run(100);
        fakeSent.clear();
        chatBia.message("B-1", "teste");
        run(200);
        std::vector<std::string> d, s;
        chatBia.read(d, s);
        CHECK(d == std::vector<std::string>{"B-1"} && s.size() == 1 && s[0].rfind("Teste ", 0) == 0);

        // Strangers that just announced themselves send ciphertext: they get a handshake each if a slot is free,
        // but a chat in use is not given up for them.
        std::vector<Phone> strangers;
        for (uint8_t i = 0; i < 3; ++i)
            strangers.emplace_back("x", uint8_t(60 + i), uint16_t(31 + i));
        for (const auto &stranger : strangers) {
            stranger.connect();
            stranger.write(stranger.announce());
        }
        run(500);
        for (const auto &stranger : strangers) {
            PrivatePhone fake{stranger, bridgeId};
            uint8_t bytes[40] = {1, 2, 3};
            stranger.write(fake.directed(0x11, {bytes, sizeof(bytes)}));
            run(100);
        }
        fakeSent.clear();
        d.clear();
        s.clear();
        chatBia.message("B-2", "bateria");
        run(200);
        chatBia.read(d, s);
        CHECK(d == std::vector<std::string>{"B-2"} && s.size() == 1);
        for (const auto &stranger : strangers)
            bleDisconnect(stranger.handle);

        // The range test asked in private reports there, not in the public chat.
        const auto publicNow = chat(bia.handle, &bridgeId).size();
        fakeSent.clear();
        d.clear();
        s.clear();
        run(130000);
        chatBia.read(d, s);
        CHECK(s.size() == 1 && s[0].find("nenhuma outra ponte respondeu") != std::string::npos);
        CHECK(chat(bia.handle, &bridgeId).size() == 0 || chat(bia.handle, &bridgeId).size() <= publicNow);
        for (const auto &line : chat(bia.handle, &bridgeId))
            CHECK(line.find("nenhuma outra ponte respondeu") == std::string::npos);
    }
    {
        // An iPhone on a link that takes 244 bytes per write (a T1000-E) sends the padded handshake and messages in
        // fragments.
        PrivatePhone pieces{caio, bridgeId};
        pieces.fragments = true;
        pieces.open(30);
        std::vector<std::string> delivered, said;
        fakeSent.clear();
        pieces.message("ID-F", "bateria");
        run(200);
        pieces.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{"ID-F"});
        CHECK(said == std::vector<std::string>{"Bateria: 3,92 V, cerca de 69%."});
        run(3000);
    }
}

// Stage 2: "@Name text" in the private chat goes to a LoRa contact; the answers come back to the chat.
static void directChat(const Phone &ana, const Phone &bia, const PeerId &bridgeId)
{
    const auto key = [](uint8_t seed) {
        std::array<uint8_t, 32> k{};
        for (size_t i = 0; i < k.size(); ++i)
            k[i] = uint8_t(seed + 7 * i);
        return k;
    };
    const auto keptContacts = mesh.contacts;
    mesh.contacts.push_back({key(0x50), "Fulano", true});
    mesh.contacts.push_back({key(0x60), "Casa Ana", true});
    mesh.contacts.push_back({key(0x70), "CASA ANA", true});
    mesh.contacts.push_back({key(0x80), "Fulano de Tal", true});
    mesh.contacts.push_back({key(0xa0), "Estranho", false});
    mesh.contacts.push_back({key(0x90), "Beltrano", true});
    mesh.directs.clear();
    const auto publicBefore = chat(bia.handle, &bridgeId).size();
    run(70000);
    PrivatePhone chatAna{ana, bridgeId};
    chatAna.open(120);
    std::vector<std::string> delivered, said;
    const auto ask = [&](const std::string &id, const std::string &text) {
        delivered.clear();
        said.clear();
        fakeSent.clear();
        chatAna.message(id, text);
        run(300);
        chatAna.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{id});
        return said.size() == 1 ? said[0] : std::string("<" + std::to_string(said.size()) + " answers>");
    };
    // The longest name wins, any letter case; the phone's name goes in front.
    CHECK(ask("D-1", "@fulano de tal: oi, tudo bem?") == "Enviado a Fulano de Tal pelo LoRa.");
    CHECK(mesh.directs.size() == 1 && mesh.directs[0].text == "[Bitchat ana] oi, tudo bem?");
    CHECK(std::equal(mesh.directs[0].to.begin(), mesh.directs[0].to.end(), key(0x80).begin()));
    run(21000);
    // A line break cannot push the phone's name out of sight. Dated by the bridge's clock.
    CHECK(ask("D-2", "@Fulano chego\n\xc3\xa0s 18h") == "Enviado a Fulano pelo LoRa.");
    CHECK(mesh.directs.size() == 2 && mesh.directs[1].text == "[Bitchat ana] chego \xc3\xa0s 18h");
    CHECK(mesh.directTimes.size() == 2 && mesh.directTimes[1] > mesh.directTimes[0] &&
          mesh.directTimes[1] + 5 >= uint32_t(nowUtc() / 1000) && mesh.directTimes[1] <= uint32_t(nowUtc() / 1000) + 1);
    // Unknown, shared by two, no text, too long: refused with a word, nothing sent.
    run(21000);
    // Not a contact, or a contact the owner did not mark as favourite: the same answer, nothing sent.
    CHECK(ask("D-3", "@Ninguem oi").rfind("N\xc3\xa3o \xc3\xa9 poss\xc3\xadvel mandar a \"Ninguem\"", 0) == 0);
    CHECK(ask("D-3b", "@Estranho oi").rfind("N\xc3\xa3o \xc3\xa9 poss\xc3\xadvel mandar a \"Estranho\"", 0) == 0);
    CHECK(ask("D-4", "@casa ana oi") == "H\xc3\xa1 mais de um contato \"CASA ANA\"; mensagem n\xc3\xa3o enviada.");
    CHECK(ask("D-5", "@Fulano") == "Escreva a mensagem depois do nome: @Fulano texto.");
    CHECK(ask("D-6", "@Fulano   ") == "Escreva a mensagem depois do nome: @Fulano texto.");
    run(10000);
    CHECK(ask("D-7", "@Fulano " + std::string(170, 'x')) == "Mensagem longa demais: at\xc3\xa9 146 bytes.");
    CHECK(mesh.directs.size() == 2);
    // Two at once, then the limit.
    run(45000);
    CHECK(ask("D-8", "@Fulano um") == "Enviado a Fulano pelo LoRa.");
    CHECK(ask("D-9", "@Fulano dois") == "Enviado a Fulano pelo LoRa.");
    CHECK(ask("D-10", "@Fulano tr\xc3\xaas").rfind("Aguarde um pouco", 0) == 0);
    CHECK(mesh.directs.size() == 4);

    // Fulano answers: to the phone that wrote to him, in its chat.
    fakeSent.clear();
    delivered.clear();
    said.clear();
    const auto fulano = key(0x50);
    const std::string answer = "tudo \xc3\xb3timo";
    bridge->onDirectText(fulano.data(), "Fulano", answer.data(), answer.size());
    run(200);
    chatAna.ids.clear();
    chatAna.read(delivered, said);
    CHECK(said == std::vector<std::string>{"[MC Fulano] tudo \xc3\xb3timo"});
    // His radio sends it again (its ack was lost): passed on once.
    fakeSent.clear();
    said.clear();
    bridge->onDirectText(fulano.data(), "Fulano", answer.data(), answer.size());
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty());
    // Not acknowledged by the app: sent again when Ana announces herself after half a minute; once acknowledged,
    // never again.
    const std::string answerId = chatAna.ids.at(0);
    fakeSent.clear();
    run(31000);
    ana.write(ana.announce());
    run(300);
    chatAna.ids.clear();
    chatAna.read(delivered, said);
    CHECK(said == std::vector<std::string>{"[MC Fulano] tudo \xc3\xb3timo"} && chatAna.ids.at(0) == answerId);
    chatAna.ack(answerId);
    run(300);
    fakeSent.clear();
    said.clear();
    run(31000);
    ana.write(ana.announce());
    run(300);
    chatAna.read(delivered, said);
    CHECK(said.empty());
    // A favourite's message starting "@ana" goes to her; from a contact that is not a favourite, nothing.
    fakeSent.clear();
    const auto stranger = key(0xa0);
    bridge->onDirectText(stranger.data(), "Estranho", "@ana oi", 7);
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty());
    // A message of the owner's own, from a contact no phone wrote to: not for Bitchat.
    fakeSent.clear();
    said.clear();
    const auto other = key(0x90);
    bridge->onDirectText(other.data(), "Z\xc3\xa9", "oi", 2);
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty());
    for (const auto &line : chat(bia.handle, &bridgeId))
        CHECK(line.find("tudo") == std::string::npos);
    CHECK(chat(bia.handle, &bridgeId).size() == publicBefore);
    // Half an hour after Ana last wrote to him, Fulano's messages are his and the owner's again.
    run(30 * 60 * 1000);
    ana.write(ana.announce());
    run(300);
    fakeSent.clear();
    said.clear();
    bridge->onDirectText(fulano.data(), "Fulano", "mais tarde", 10);
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty());
    // The chat itself ended after half an hour without use: Ana's app opens it again.
    run(21000);
    chatAna.open(130);

    // The owner turns it off in the radio's app: nothing goes, nothing comes.
    mesh.app.clear();
    CHECK(fromApp("!ponte privado off"));
    run(100);
    CHECK(mesh.app.back().text.find("Mensagens privadas desligadas") != std::string::npos && !bridge->directMessages());
    run(21000);
    CHECK(ask("D-11", "@Fulano oi").rfind("Mensagens privadas pelo LoRa est\xc3\xa3o desligadas", 0) == 0);
    fakeSent.clear();
    said.clear();
    bridge->onDirectText(fulano.data(), "Fulano", "oi", 2);
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty() && mesh.directs.size() == 4);
    CHECK(fromApp("!bridge dm on"));
    run(100);
    CHECK(bridge->directMessages());

    // Half an hour later Bia's earlier chat with the bridge has ended.
    run(31 * 60 * 1000);
    bia.write(bia.announce());
    run(300);
    // "@bia ..." from LoRa to a phone with no chat open: kept, and when Bia announces herself the bridge opens the
    // chat and delivers it.
    fakeSent.clear();
    const std::string toBia = "@bia o port\xc3\xa3o est\xc3\xa1 aberto";
    bridge->onDirectText(other.data(), "Beltrano", toBia.data(), toBia.size());
    run(200);
    {
        PrivatePhone chatBia{bia, bridgeId};
        // She is in reach: the bridge already asked her for a handshake.
        auto opened = chatBia.fromBridge(0x10);
        if (opened.empty()) {
            fakeSent.clear();
            bia.write(bia.announce());
            run(300);
            opened = chatBia.fromBridge(0x10);
        }
        CHECK(opened.size() == 1 && opened[0].size() == NOISE_MESSAGE1);
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(200 - i);
        chatBia.handshake.start(false, e);
        CHECK(chatBia.handshake.readMessage1({opened[0].data(), opened[0].size()}));
        uint8_t m2[NOISE_MESSAGE2];
        CHECK(chatBia.handshake.writeMessage2(bia.noiseKey.data(), bia.noisePublic.data(), m2));
        fakeSent.clear();
        bia.write(chatBia.directed(0x10, {m2, sizeof(m2)}));
        run(300);
        const auto third = chatBia.fromBridge(0x10);
        CHECK(third.size() == 1 && chatBia.handshake.readMessage3({third[0].data(), third[0].size()}));
        chatBia.handshake.split(chatBia.send, chatBia.receive);
        std::vector<std::string> d, s;
        chatBia.read(d, s);
        CHECK(s == std::vector<std::string>{"[MC Beltrano] o port\xc3\xa3o est\xc3\xa1 aberto"});
    }

    mesh.contacts = keptContacts;
}

// Stage 3: with the tunnel on, phones near different bridges list each other and chat privately over LoRa.
static Frame directedFrame(const Phone &from, const PeerId &to, uint8_t type, Bytes payload, uint8_t ttl = 7)
{
    Frame frame;
    stamp = std::max(stamp + 1, nowUtc());
    CHECK(encode(type, from.id, &to, stamp, ttl, payload, {}, frame, true) == Result::Ok);
    return frame;
}
// What the other bridge would send: the frame without padding, in kind 3 parts.
static void fromOtherBridge(Bytes frame)
{
    static uint32_t id = 0x70000000;
    Packet p;
    CHECK(decode(frame, p) == Result::Ok);
    const Bytes natural{frame.data, p.naturalSize};
    ++id;
    for (uint8_t i = 0; i < tunnelFrameParts(natural.size); ++i) {
        TunnelDatagram d;
        CHECK(makeTunnelFrame(id, natural, i, d));
        CHECK(bridge->onChannelData(1, TUNNEL_DATA_TYPE, d.data.data(), d.size));
    }
}
// The frames this bridge sent over LoRa since the last clear, put together as the other bridge would.
static std::vector<std::vector<uint8_t>> tunneled()
{
    FrameReceiver receiver;
    std::vector<std::vector<uint8_t>> out;
    for (const auto &d : mesh.data) {
        if (d.size() < 3 || (d[2] & 0x0f) != 3)
            continue;
        Bytes frame;
        if (receiver.accept({d.data(), d.size()}, millis(), frame))
            out.emplace_back(frame.data, frame.data + frame.size);
    }
    return out;
}

static void tunnelChat(const Phone &ana, const Phone &bia, const Phone &caio, const PeerId &bridgeId)
{
    Phone rui("rui", 44);
    const auto radioBefore = mesh.radio.size();
    // Off by default: nothing goes, nothing comes.
    CHECK(!bridge->tunnel());
    mesh.data.clear();
    fakeSent.clear();
    ana.write(ana.announce());
    run(1000);
    CHECK(tunneled().empty());
    {
        Frame announce = rui.announce({}, 6);
        fromOtherBridge(announce.bytes());
        run(200);
        for (const auto &frame : received(ana.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            CHECK(p.sender != rui.id);
        }
    }
    // The operator turns it on in the radio's app.
    mesh.app.clear();
    CHECK(fromApp("!ponte t\xc3\xbanel on"));
    run(100);
    CHECK(bridge->tunnel() && mesh.app.back().text.rfind("bridge: T\xc3\xbanel ligado", 0) == 0);

    // Ana, here, announces herself: her announce goes over LoRa, a hop older; again only after three minutes.
    mesh.data.clear();
    ana.write(ana.announce());
    run(3000);
    auto out = tunneled();
    CHECK(out.size() == 1);
    {
        Packet p;
        CHECK(decode({out[0].data(), out[0].size()}, p) == Result::Ok && p.type == 1 && p.sender == ana.id && p.ttl == 6);
    }
    mesh.data.clear();
    ana.write(ana.announce());
    run(3000);
    CHECK(tunneled().empty());

    // Rui, near the other bridge, shows up here.
    fakeSent.clear();
    fromOtherBridge(rui.announce({}, 6).bytes());
    run(200);
    bool listed = false;
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        listed |= p.type == 1 && p.sender == rui.id;
    }
    CHECK(listed);

    // With someone near the other bridge, presence goes every 50 s (the app forgets a relayed phone 60 s after its
    // last announce), for the first two phones here by ID; the third keeps three minutes.
    {
        const auto presences = [&]() {
            size_t count = 0;
            for (const auto &frame : tunneled()) {
                Packet p;
                CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
                count += p.type == 1;
            }
            return count;
        };
        // Bia and Caio, here too, are announced once.
        mesh.data.clear();
        bia.write(bia.announce());
        caio.write(caio.announce());
        run(6000);
        CHECK(presences() == 2);
        run(30000);
        mesh.data.clear();
        ana.write(ana.announce());
        run(1000);
        CHECK(presences() == 0);
        run(20000);
        fromOtherBridge(rui.announce({}, 6).bytes());
        run(200);
        mesh.data.clear();
        for (const auto *phone : {&ana, &bia, &caio})
            phone->write(phone->announce());
        run(6000);
        CHECK(presences() == 2);
    }

    // Ana opens a private chat with Rui: the handshake crosses both ways, and so do the messages; the bridges only
    // carry them.
    NoiseHandshake anaSide, ruiSide;
    uint8_t e1[32], e2[32];
    for (int i = 0; i < 32; ++i) {
        e1[i] = uint8_t(11 * i + 3);
        e2[i] = uint8_t(13 * i + 5);
    }
    anaSide.start(true, e1);
    ruiSide.start(false, e2);
    uint8_t m1[NOISE_MESSAGE1], m2[NOISE_MESSAGE2], m3[NOISE_MESSAGE3];
    CHECK(anaSide.writeMessage1(m1));
    // The opening does not wait for the spacing between sends: just after a public text went on air, it goes
    // as soon as the radio is free (the app gives up after ten seconds).
    {
        const auto before = mesh.radio.size();
        ana.write(ana.say("antes do privado"));
        for (int i = 0; i < 100 && mesh.radio.size() == before; ++i)
            run(20);
        CHECK(mesh.radio.size() == before + 1);
    }
    mesh.data.clear();
    ana.write(directedFrame(ana, rui.id, 0x10, {m1, sizeof(m1)}));
    run(300);
    out = tunneled();
    CHECK(out.size() == 1);
    {
        Packet p;
        CHECK(decode({out[0].data(), out[0].size()}, p) == Result::Ok && p.type == 0x10 && p.recipient == rui.id &&
              p.payload.size == NOISE_MESSAGE1 && out[0].size() == p.naturalSize);
        CHECK(ruiSide.readMessage1(p.payload));
    }
    CHECK(ruiSide.writeMessage2(rui.noiseKey.data(), rui.noisePublic.data(), m2));
    fakeSent.clear();
    fromOtherBridge(directedFrame(rui, ana.id, 0x10, {m2, sizeof(m2)}, 6).bytes());
    run(200);
    std::vector<uint8_t> second;
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        if (p.type == 0x10 && p.sender == rui.id && p.recipient == ana.id) {
            // padded again, as the app reads it
            CHECK(frame.size() == paddedSize(p.naturalSize));
            second.assign(p.payload.data, p.payload.data + p.payload.size);
        }
    }
    CHECK(anaSide.readMessage2({second.data(), second.size()}));
    CHECK(anaSide.writeMessage3(ana.noiseKey.data(), ana.noisePublic.data(), m3));
    mesh.data.clear();
    ana.write(directedFrame(ana, rui.id, 0x10, {m3, sizeof(m3)}));
    run(3000);
    out = tunneled();
    CHECK(out.size() == 1);
    {
        Packet p;
        CHECK(decode({out[0].data(), out[0].size()}, p) == Result::Ok && ruiSide.readMessage3(p.payload));
    }
    NoiseSender anaSend, ruiSend;
    NoiseReceiver anaReceive, ruiReceive;
    anaSide.split(anaSend, anaReceive);
    ruiSide.split(ruiSend, ruiReceive);
    const std::string hello = "\x01\x00\x04M-01\x01\x0bOi Rui! \xf0\x9f\x91\x8b";
    std::vector<uint8_t> sealed(hello.size() + NOISE_TRANSPORT_OVERHEAD);
    CHECK(noiseEncrypt(anaSend, {reinterpret_cast<const uint8_t *>(hello.data()), hello.size()}, sealed.data()));
    mesh.data.clear();
    ana.write(directedFrame(ana, rui.id, 0x11, {sealed.data(), sealed.size()}));
    run(3000);
    out = tunneled();
    CHECK(out.size() == 1);
    {
        Packet p;
        CHECK(decode({out[0].data(), out[0].size()}, p) == Result::Ok && p.type == 0x11);
        std::vector<uint8_t> plain(p.payload.size);
        CHECK(noiseDecrypt(ruiReceive, p.payload, plain.data()));
        CHECK(std::string(plain.begin(), plain.begin() + hello.size()) == hello);
    }
    // Rui's delivered ack comes back.
    const std::string ack = "\x03M-01";
    std::vector<uint8_t> ackSealed(ack.size() + NOISE_TRANSPORT_OVERHEAD);
    CHECK(noiseEncrypt(ruiSend, {reinterpret_cast<const uint8_t *>(ack.data()), ack.size()}, ackSealed.data()));
    fakeSent.clear();
    const Frame ackFrame = directedFrame(rui, ana.id, 0x11, {ackSealed.data(), ackSealed.size()}, 6);
    fromOtherBridge(ackFrame.bytes());
    run(200);
    size_t acks = 0;
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        if (p.type == 0x11 && p.sender == rui.id) {
            std::vector<uint8_t> plain(p.payload.size);
            CHECK(noiseDecrypt(anaReceive, p.payload, plain.data()) && plain[0] == 3);
            ++acks;
        }
    }
    CHECK(acks == 1);
    // The same frame once more over LoRa is not given to the phones again.
    fakeSent.clear();
    fromOtherBridge(ackFrame.bytes());
    run(200);
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(!(p.type == 0x11 && p.sender == rui.id));
    }
    // A remote phone cannot reach this bridge's own private chat, nor a phone nobody here knows; and a phone here
    // writing to another phone here does not go over LoRa.
    fakeSent.clear();
    PeerId stranger{};
    stranger.fill(0x5a);
    fromOtherBridge(directedFrame(rui, bridgeId, 0x10, {m1, sizeof(m1)}, 6).bytes());
    fromOtherBridge(directedFrame(rui, stranger, 0x10, {m1, sizeof(m1)}, 6).bytes());
    run(200);
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(p.sender != rui.id);
    }
    mesh.data.clear();
    ana.write(directedFrame(ana, bia.id, 0x11, {sealed.data(), sealed.size()}));
    run(3000);
    CHECK(tunneled().empty());
    // A packet claiming to be Ana's, written on Bia's link, does not go over LoRa.
    mesh.data.clear();
    bia.write(directedFrame(ana, rui.id, 0x11, {sealed.data(), sealed.size()}));
    run(3000);
    CHECK(tunneled().empty());
    // From LoRa, a packet claiming to come from a phone here is not given to the phones.
    fakeSent.clear();
    fromOtherBridge(directedFrame(bia, ana.id, 0x11, {sealed.data(), sealed.size()}, 6).bytes());
    run(200);
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(!(p.type == 0x11 && p.sender == bia.id && p.payload.size == sealed.size()));
    }
    // A crowd of phones over LoRa takes only a few places: a new phone here still gets in.
    std::vector<Phone> crowd;
    for (uint8_t i = 0; i < 30; ++i)
        crowd.emplace_back("far", uint8_t(150 + i));
    for (const auto &far : crowd)
        fromOtherBridge(far.announce({}, 6).bytes());
    run(200);
    // (three Bluetooth links: Bia steps out for the newcomer)
    bleDisconnect(bia.handle);
    Phone eli("eli", 99, 21);
    eli.connect();
    eli.write(eli.announce());
    run(300);
    mesh.radio.clear();
    eli.write(eli.say("oi da casa"));
    run(3000);
    CHECK(texts(mesh.radio) == std::vector<std::string>{"eli: oi da casa"});
    bleDisconnect(eli.handle);
    bia.connect();
    bia.write(bia.announce());
    run(300);
    // Nothing of it on the LoRa text channel.
    CHECK(mesh.radio.size() == radioBefore);
    // The report in the app counts what crossed the tunnel.
    mesh.app.clear();
    CHECK(fromApp("!bridge"));
    run(100);
    {
        bool counted = false;
        for (const auto &line : mesh.app)
            counted |= line.text.rfind("bridge: Tunnel: out ", 0) == 0 && line.text.find(", far phones ") != std::string::npos;
        CHECK(counted);
    }
    // Another bridge heard over Bluetooth, with a lower node number, puts this one on standby for the public text
    // (both would relay it). The tunnel goes on: its frames are each phone's own, and the two sides of a chat
    // need both bridges.
    {
        Phone other("LoRa bridge 0001", 77);
        uint8_t proof[GATEWAY_PROOF];
        makeGatewayProof(crypto, other.id, 1, bridge->gatewayKey(), proof);
        std::vector<uint8_t> extra{GATEWAY_TLV, uint8_t(GATEWAY_PROOF)};
        extra.insert(extra.end(), proof, proof + GATEWAY_PROOF);
        bia.write(other.announce({extra.data(), extra.size()}, 6));
        // (Rui shows again: the crowd above took the places of the oldest phones from afar.)
        fromOtherBridge(rui.announce({}, 6).bytes());
        run(300);
        CHECK(std::string(bridge->status()).rfind("Standby", 0) == 0);
        const std::string again = "\x01\x00\x04M-02\x01\x03Oi?";
        std::vector<uint8_t> second(again.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(anaSend, {reinterpret_cast<const uint8_t *>(again.data()), again.size()}, second.data()));
        mesh.data.clear();
        ana.write(directedFrame(ana, rui.id, 0x11, {second.data(), second.size()}));
        run(3000);
        // Ana's private packet goes (after her announce, when that is due).
        size_t privatePackets = 0;
        for (const auto &frame : tunneled()) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            privatePackets += p.type == 0x11 && p.sender == ana.id && p.recipient == rui.id;
        }
        CHECK(privatePackets == 1);
        // The standby ends by itself.
        run(91000);
        CHECK(std::string(bridge->status()).rfind("Standby", 0) != 0);
    }
    // Off again: frames from LoRa are ignored.
    CHECK(fromApp("!bridge tunnel off"));
    run(100);
    CHECK(!bridge->tunnel());
    fakeSent.clear();
    fromOtherBridge(directedFrame(rui, ana.id, 0x11, {ackSealed.data(), ackSealed.size()}, 6).bytes());
    run(200);
    for (const auto &frame : received(ana.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(p.sender != rui.id);
    }
}

// Cashu over LoRa: the payer's frames go whole to the other bridges, and come out whole there.
static void cashuTunnel(const Phone &ana, const Phone &bia, const PeerId &bridgeId)
{
    const auto bridgeSaid = [&]() { return chat(bia.handle, &bridgeId); };
    const auto lastSaid = [&]() {
        const auto lines = bridgeSaid();
        return lines.empty() ? std::string() : lines.back();
    };
    for (size_t i = 0; i < 32; ++i)
        mesh.nodeSecret[i] = uint8_t(5 + 3 * i);
    Ed25519::derivePublicKey(mesh.nodePublic.data(), mesh.nodeSecret.data());
    mesh.identity = true;
    // A token locked to a key goes, on the public #bitchat channel too.
    const std::string locked = token("B p2pk"), open = token("B plain");
    mesh.radio.clear();
    mesh.data.clear();
    fakeSent.clear();
    run(30000); // airtime for the parts
    const Frame announce = ana.announce();
    ana.write(announce);
    run(200);
    const Frame payment = ana.sayCompressed(locked);
    ana.write(payment);
    run(30000);
    CHECK(mesh.radio.empty());
    CHECK(lastSaid().rfind("[bridge] Levando 10 sat de ana pela LoRa (", 0) == 0);
    // Bia got Ana's message as Ana sent it, as before.
    size_t relayed = 0;
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        relayed += p.type == 2 && p.sender == ana.id && p.compressed();
    }
    CHECK(relayed == 1);
    // The parts, put together by anyone, are exactly the two frames.
    const size_t parts = tunnelParts(4 + announce.size + payment.size);
    CHECK(mesh.data.size() == parts && parts > 1);
    TunnelReceiver far;
    TunnelHeard heard;
    unsigned complete = 0;
    for (const auto &d : mesh.data) {
        CHECK(d.size() <= TUNNEL_DATAGRAM);
        if (far.accept({d.data(), d.size()}, 0, heard) && heard.complete) {
            ++complete;
            CHECK(
                frameEquals(std::vector<uint8_t>(announce.data.begin(), announce.data.begin() + announce.size), heard.announce));
            CHECK(frameEquals(std::vector<uint8_t>(payment.data.begin(), payment.data.begin() + payment.size), heard.message));
        }
    }
    CHECK(complete == 1);
    // The other bridge's word comes back. Its radio is a contact here and signed it: the payer is told at once,
    // with the contact's name. A forged answer with that radio's key, or one from a stranger, does not win.
    std::array<uint8_t, 32> farSecret{}, farPublic{};
    for (size_t i = 0; i < 32; ++i)
        farSecret[i] = uint8_t(77 + i);
    Ed25519::derivePublicKey(farPublic.data(), farSecret.data());
    mesh.contacts.push_back({farPublic, "Casa do Pai"});
    Digest messageHash;
    crypto.hash(payment.bytes(), messageHash);
    const auto signedAck = [&](uint32_t id, TunnelOutcome outcome, uint8_t phones, const uint8_t *key, const uint8_t *secret,
                               const uint8_t *pub, TunnelDatagram &out) {
        uint8_t message[TUNNEL_ACK_SIGNED], signature[64];
        tunnelAckSigned(id, outcome, phones, key, messageHash.data(), message);
        Ed25519::sign(signature, secret, pub, message, sizeof(message));
        makeTunnelAck(id, outcome, phones, key, signature, out);
    };
    TunnelDatagram ack;
    const uint8_t stranger[8] = {0xbe, 0xef, 0x12, 0x34, 1, 2, 3, 4};
    makeTunnelAck(heard.id, TunnelOutcome::Delivered, 9, stranger, nullptr, ack);
    CHECK(toBridge(ack));
    signedAck(heard.id, TunnelOutcome::Delivered, 9, farPublic.data(), farSecret.data(), farPublic.data(), ack);
    ack.data[8] = 7; // altered after signing
    CHECK(toBridge(ack));
    run(3000);
    CHECK(lastSaid().rfind("[bridge] Levando", 0) == 0);
    signedAck(heard.id, TunnelOutcome::Delivered, 2, farPublic.data(), farSecret.data(), farPublic.data(), ack);
    CHECK(ack.size == TUNNEL_ACK_SIZE && toBridge(ack));
    run(300);
    CHECK(lastSaid() == "[bridge] ✓ 10 sat entregue à ponte Casa do Pai (2 cel.). Confirme com quem recebe.");
    // Other channel data is the app's.
    const uint8_t other[4] = {1, 2, 3, 4};
    CHECK(!bridge->onChannelData(1, 0x1234, other, sizeof(other)));
    CHECK(!bridge->onChannelData(1, TUNNEL_DATA_TYPE, other, sizeof(other)));

    // A token anyone may spend stays here: the channel's key is public.
    mesh.data.clear();
    fakeSent.clear();
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(open));
    run(5000);
    CHECK(mesh.data.empty() && mesh.radio.empty());
    CHECK(lastSaid() == "[bridge] Token sem trava P2PK: num canal aberto outros poderiam gastá-lo. Não foi pela LoRa.");
    // The same public key saved under another name ("BitChat", no "#") is still public.
    mesh.channels[1].name = "BitChat";
    run(4000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(open));
    run(5000);
    CHECK(mesh.data.empty() && mesh.radio.empty());
    CHECK(lastSaid() == "[bridge] Token sem trava P2PK: num canal aberto outros poderiam gastá-lo. Não foi pela LoRa.");
    mesh.channels[1].name = PUBLIC_CHANNEL_NAME;
    // Uncompressed text naming a token is not text for LoRa either, nor a token cut short.
    ana.write(ana.say("cashuB" + std::string(50, 'Q')));
    run(5000);
    CHECK(mesh.radio.empty() && mesh.data.empty());
    CHECK(lastSaid() == "[bridge] Token Cashu ilegível: não foi levado pela LoRa.");
    // A locked token with an unlocked one glued to a word: nothing goes.
    run(4000);
    ana.write(ana.sayCompressed(locked + " e x" + open));
    run(5000);
    CHECK(mesh.radio.empty() && mesh.data.empty());
    CHECK(lastSaid() == "[bridge] Um token por mensagem: nada foi levado pela LoRa.");
    // A token in a link is read as one; an unlocked one stays here.
    run(4000);
    ana.write(ana.say("https://wallet.example/?token=cashuB" + std::string(60, 'Q')));
    run(5000);
    CHECK(mesh.radio.empty() && mesh.data.empty());

    // No answer at all: the payer hears so, with what it means.
    fakeSent.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(token("B p2pk refund")));
    run(2000);
    CHECK(lastSaid().rfind("[bridge] Levando 10 sat", 0) == 0);
    // A second payment meanwhile waits its turn.
    ana.write(ana.sayCompressed(token("B usd")));
    run(1000);
    CHECK(lastSaid() == "[bridge] A ponte já leva outro pagamento: reenvie em 2 min.");
    run(TUNNEL_WAIT_MS);
    CHECK(lastSaid() == "[bridge] ✗ 10 sat sem confirmação. Se não chegou, o token segue com você.");

    // A refusal waits a little for another bridge, then is told.
    fakeSent.clear();
    mesh.data.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(locked));
    run(30000);
    CHECK(!mesh.data.empty());
    {
        TunnelReceiver r;
        TunnelHeard h;
        for (const auto &d : mesh.data)
            r.accept({d.data(), d.size()}, 0, h);
        const uint8_t old[8] = {0x0a, 0x0b, 0, 0};
        makeTunnelAck(read32(mesh.data[0].data() + 3), TunnelOutcome::NoPhones, 0, old, nullptr, ack);
    }
    CHECK(toBridge(ack));
    run(5000);
    CHECK(lastSaid().rfind("[bridge] Levando", 0) == 0);
    run(11000);
    CHECK(lastSaid() == "[bridge] ✗ 10 sat: ponte 0a0b sem celular perto. O token segue com você.");
    // A refusal the far radio signed is not undone by an unchecked "delivered" from anyone else.
    fakeSent.clear();
    mesh.data.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    {
        const Frame again = ana.sayCompressed(locked);
        ana.write(again);
        run(30000);
        CHECK(!mesh.data.empty());
        const uint32_t id = read32(mesh.data[0].data() + 3);
        Digest h;
        crypto.hash(again.bytes(), h);
        uint8_t message[TUNNEL_ACK_SIGNED], signature[64];
        tunnelAckSigned(id, TunnelOutcome::NoPhones, 0, farPublic.data(), h.data(), message);
        Ed25519::sign(signature, farSecret.data(), farPublic.data(), message, sizeof(message));
        makeTunnelAck(id, TunnelOutcome::NoPhones, 0, farPublic.data(), signature, ack);
        CHECK(toBridge(ack));
        makeTunnelAck(id, TunnelOutcome::Delivered, 3, stranger, nullptr, ack);
        CHECK(toBridge(ack));
        run(16000);
        CHECK(lastSaid() ==
              "[bridge] ✗ 10 sat: ponte Casa do Pai sem celular perto. O token segue com você.");
    }
    // A delivery only a stranger claims is told as such, after the wait for a better answer.
    fakeSent.clear();
    mesh.data.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    {
        const Frame again = ana.sayCompressed(locked);
        ana.write(again);
        run(30000);
        CHECK(!mesh.data.empty());
        makeTunnelAck(read32(mesh.data[0].data() + 3), TunnelOutcome::Delivered, 1, stranger, nullptr, ack);
        CHECK(toBridge(ack));
        run(5000);
        CHECK(lastSaid().rfind("[bridge] Levando", 0) == 0);
        run(11000);
        CHECK(lastSaid() == "[bridge] ✓? 10 sat entregue, diz a ponte beef (não é contato). Confirme com quem recebe.");
    }

    // With Ana's last announce too old for the far phones, the payment waits for her next one.
    fakeSent.clear();
    mesh.data.clear();
    run(30000);
    ana.write(ana.announce());
    run(31000);
    ana.write(ana.sayCompressed(token("B usd")));
    run(5000);
    CHECK(mesh.data.empty() && bridgeSaid().empty());
    const Frame fresh = ana.announce();
    ana.write(fresh);
    run(30000);
    CHECK(lastSaid().rfind("[bridge] Levando 150 usd de ana pela LoRa (", 0) == 0 && !mesh.data.empty());
    {
        TunnelReceiver r;
        TunnelHeard h;
        bool whole = false;
        for (const auto &part : mesh.data)
            if (r.accept({part.data(), part.size()}, 0, h) && h.complete)
                whole = frameEquals(std::vector<uint8_t>(fresh.data.begin(), fresh.data.begin() + fresh.size), h.announce);
        CHECK(whole);
    }
    run(TUNNEL_WAIT_MS);
    // And if it never comes, nothing goes and she is told.
    fakeSent.clear();
    mesh.data.clear();
    run(31000);
    ana.write(ana.sayCompressed(locked));
    run(ANNOUNCE_WAIT + 1000);
    CHECK(mesh.data.empty());
    CHECK(lastSaid() == "[bridge] Seu celular não se anunciou a tempo: nada foi levado. Reenvie o token.");

    // The other end: a payer this radio never met, heard only through LoRa.
    Phone rui("rui", 9);
    fakeSent.clear();
    mesh.data.clear();
    const Frame ruiAnnounce = rui.announce(), ruiPayment = rui.sayCompressed(locked);
    TunnelSender sender;
    CHECK(sender.start(0x55667788, ruiAnnounce.bytes(), ruiPayment.bytes(), 0));
    std::vector<TunnelDatagram> ruiParts;
    TunnelDatagram d;
    while (sender.next(d))
        ruiParts.push_back(d);
    // Nothing shows while a part is missing.
    for (size_t i = 1; i < ruiParts.size(); ++i)
        CHECK(toBridge(ruiParts[i]));
    run(1000);
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok && p.sender != rui.id);
    }
    // Quiet for a while, the bridge asks for what it lacks.
    run(TUNNEL_QUIET_MS + 3000);
    CHECK(!mesh.data.empty());
    TunnelHeard want;
    CHECK(readTunnelAnswer({mesh.data.back().data(), mesh.data.back().size()}, want) && want.kind == TunnelKind::Want &&
          want.id == 0x55667788 && want.missing == 1);
    mesh.data.clear();
    CHECK(toBridge(ruiParts[0]));
    run(3000);
    std::vector<Packet> shown;
    std::vector<std::vector<uint8_t>> frames = received(bia.handle);
    for (const auto &frame : frames) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        if (p.sender == rui.id)
            shown.push_back(p);
    }
    // The announce first, then the payment, as Rui signed them; one hop older.
    CHECK(shown.size() == 2 && shown[0].type == 1 && shown[1].type == 2 && shown[1].compressed());
    CHECK(shown[0].ttl == 6 && shown[1].ttl == 6 && shown[1].timestamp == read64(ruiPayment.data.data() + 3));
    CHECK(lastSaid() == "[bridge] 10 sat de rui chegaram pela LoRa. Toque no token para resgatar.");
    // And the payer's bridge is told.
    CHECK(mesh.data.size() == 1);
    TunnelHeard told;
    CHECK(readTunnelAnswer({mesh.data[0].data(), mesh.data[0].size()}, told) && told.kind == TunnelKind::Ack &&
          told.id == 0x55667788 && told.outcome == TunnelOutcome::Delivered && told.phones >= 2 && told.signedAck &&
          std::equal(told.key.begin(), told.key.end(), mesh.nodePublic.begin()));
    {
        // Signed with this radio's MeshCore identity, over the message Rui's bridge sent.
        Digest h;
        crypto.hash(ruiPayment.bytes(), h);
        uint8_t message[TUNNEL_ACK_SIGNED];
        tunnelAckSigned(told.id, told.outcome, told.phones, told.key.data(), h.data(), message);
        CHECK(Ed25519::verify(told.signature.data(), mesh.nodePublic.data(), message, sizeof(message)));
    }
    // A copy of a part changes nothing; a phone relaying the payment back is a copy too.
    fakeSent.clear();
    mesh.data.clear();
    CHECK(toBridge(ruiParts[2]));
    Frame back = ruiPayment;
    --back.data[2];
    bia.write(back);
    run(3000);
    CHECK(bridgeSaid().empty() && mesh.data.empty() && mesh.radio.empty());

    // Too late for the phones: refused, and the payer's bridge hears why.
    Phone eva("eva", 10);
    const Frame evaAnnounce = eva.announce(), evaPayment = eva.sayCompressed(locked);
    run(125000);
    TunnelSender late;
    CHECK(late.start(0x11, evaAnnounce.bytes(), evaPayment.bytes(), 0));
    mesh.data.clear();
    fakeSent.clear();
    while (late.next(d))
        toBridge(d);
    run(3000);
    CHECK(bridgeSaid().empty() && mesh.data.size() == 1);
    CHECK(readTunnelAnswer({mesh.data[0].data(), mesh.data[0].size()}, told) && told.outcome == TunnelOutcome::Late);
    // Text that is no payment, or forged, is refused the same way.
    Phone ivo("ivo", 11);
    const Frame ivoAnnounce = ivo.announce(), ivoText = ivo.sayCompressed(std::string(300, 'x'));
    Frame forged = ivo.sayCompressed(locked);
    forged.data[forged.size - 1] ^= 1;
    const Frame *refused[] = {&ivoText, &forged};
    for (const Frame *message : refused) {
        TunnelSender s;
        CHECK(s.start(message == &ivoText ? 0x21 : 0x22, ivoAnnounce.bytes(), message->bytes(), 0));
        mesh.data.clear();
        fakeSent.clear();
        while (s.next(d))
            toBridge(d);
        run(3000);
        CHECK(bridgeSaid().empty() && mesh.data.size() == 1);
        CHECK(readTunnelAnswer({mesh.data[0].data(), mesh.data[0].size()}, told) && told.outcome == TunnelOutcome::Invalid);
        // A refusal is signed over the message as it came, so the payer's bridge can believe it.
        Digest h;
        crypto.hash(message->bytes(), h);
        uint8_t signedBytes[TUNNEL_ACK_SIGNED];
        tunnelAckSigned(told.id, told.outcome, told.phones, told.key.data(), h.data(), signedBytes);
        CHECK(told.signedAck && Ed25519::verify(told.signature.data(), mesh.nodePublic.data(), signedBytes, sizeof(signedBytes)));
    }
    // On another channel the parts are hidden from the app but not taken.
    mesh.data.clear();
    CHECK(toBridge(ruiParts[0], 2));
    run(1000);
    CHECK(mesh.data.empty());
    // A token typed in the MeshCore app is not sent to Bitchat as text.
    mesh.app.clear();
    fakeSent.clear();
    CHECK(fromApp(open)); // kept from the air
    run(1000);
    CHECK(bridgeSaid().empty());
    CHECK(mesh.app.size() == 1 &&
          mesh.app[0].text == "bridge: Não enviado: tokens Cashu vão pelo Bitchat (chat público), não pelo canal do MeshCore.");
    mesh.app.clear();
    fromLoRa("Jo\xc3\xa3o: " + open.substr(0, 120));
    run(1000);
    CHECK(bridgeSaid().empty());
    // A later piece of a token cut to fit LoRa text no longer names it: still kept out.
    fromLoRa("Jo\xc3\xa3o (2/3): " + open.substr(120, 150));
    run(1000);
    CHECK(bridgeSaid().empty());
    // Ordinary text with short words still goes through.
    fromLoRa("Jo\xc3\xa3o: chego em 10 min, https://exemplo.com.br/rota?id=12");
    run(1000);
    CHECK(bridgeSaid() == std::vector<std::string>{"[MC Jo\xc3\xa3o] chego em 10 min, https://exemplo.com.br/rota?id=12"});
    fakeSent.clear();
    mesh.radio.clear();
    mesh.data.clear();
}

int main()
{
    fakeFree = 200 * 1024;
    fakeLargest = 64 * 1024;
    fakeNow = 1000;
    mesh.channels[0].name = "Public";
    std::copy(std::begin(PUBLIC_KEY), std::end(PUBLIC_KEY), mesh.channels[0].secret.begin());

    bleAttach(&server);
    bleName("MeshCore-1A2B3C4D");
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    // The tunnel is on by default; most of this test runs without it, and the tunnel's own part turns it on.
    CHECK(bridge->tunnel());
    CHECK(bridge->setTunnel(false) && !bridge->tunnel());
    CHECK(Preferences::space == "bc-meshcore");
    // Nothing happens before the companion's Bluetooth is up.
    run(3000);
    CHECK(std::string(bridge->status()) == "Bluetooth is off" && mesh.saves == 0);
    platformBluetooth.up = true;
    run(1500);

    // First boot: the bridge is on and its public channel exists, with no step by the operator.
    CHECK(bridge->enabled() && bridge->textChannel() == 1 && mesh.saves == 1);
    CHECK(mesh.channels[1].name == PUBLIC_CHANNEL_NAME);
    CHECK(std::equal(std::begin(BITCHAT_KEY), std::end(BITCHAT_KEY), mesh.channels[1].secret.begin()));
    CHECK(std::all_of(mesh.channels[1].secret.begin() + 16, mesh.channels[1].secret.end(), [](uint8_t b) { return b == 0; }));
    // The companion clock was never set, and a date guessed at start is not used.
    CHECK(std::string(bridge->status()) == "Clock not set" && bridge->hint()[0]);
    // For the first minute the radio advertises as the stock firmware, so its app finds it after a restart.
    CHECK(refreshes >= 1 && fakeScanName.empty() && !bleAdvertise());
    run(60000);
    // Then the advertisement carries the Bitchat service; the scan response the companion service and a short name.
    CHECK(fakeScanName == "MeshCore-1A" && fakeScanShort);

    // The self-test answers in the app even in this state, and is never transmitted.
    CHECK(fromApp("!bridge"));
    run(100);
    CHECK(mesh.radio.empty() && mesh.app.size() >= 4);
    CHECK(mesh.app[0].channel == 1 && mesh.app[0].text.rfind("bridge: Clock not set. Bluetooth 0, Bitchat 0, people 0.", 0) == 0);
    CHECK(mesh.app[1].text.find("UTC 00:00:00 none") != std::string::npos);
    CHECK(mesh.app.back().text == "bridge: Open Bitchat nearby, or connect this app again");
    for (const auto &line : mesh.app)
        CHECK(line.text.size() <= LORA_TEXT_LIMIT && line.text.rfind("bridge: ", 0) == 0);
    mesh.app.clear();

    // LoRa adverts: with no time at all, one radio gives a weak time; another radio alone that disagrees does not
    // move it; two that agree within a minute replace it with the earlier of the two.
    {
        const uint8_t sul[4] = {1, 2, 3, 4}, norte[4] = {5, 6, 7, 8}, torto[4] = {9, 9, 9, 9};
        const uint32_t real = uint32_t(nowUtc() / 1000);
        char diag[200];
        bridge->onRadioTime(sul, real - 3);
        bridge->onRadioTime(sul, real);
        run(100);
        CHECK(std::string(bridge->status()) != "Clock not set");
        {
            // The first advert set it; the same radio again does not move a time it gave.
            const uint32_t first = real - 3;
            char single[40];
            snprintf(single, sizeof(single), "UTC %02u:%02u:%02u lora", unsigned(first / 3600 % 24), unsigned(first / 60 % 60),
                     unsigned(first % 60));
            bridge->onRadioTime(torto, real + 3600);
            bridge->diagnostics(diag, sizeof(diag));
            CHECK(std::string(diag).rfind(single, 0) == 0);
        }
        bridge->onRadioTime(norte, real - 30);
        run(100);
        CHECK(std::string(bridge->status()) != "Clock not set");
        bridge->diagnostics(diag, sizeof(diag));
        const uint32_t expected = real - 30;
        char wanted[40];
        snprintf(wanted, sizeof(wanted), "UTC %02u:%02u:%02u lora", unsigned(expected / 3600 % 24), unsigned(expected / 60 % 60),
                 unsigned(expected % 60));
        CHECK(std::string(diag).rfind(wanted, 0) == 0);
        // An old advert heard again is a replay: it counts for nothing.
        bridge->onRadioTime(torto, real - 600);
        bridge->onRadioTime(sul, real - 620);
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).rfind(wanted, 0) == 0);
        // One transmitter with two keys claiming seven hours later is outvoted by nothing, and changes nothing:
        // a group no larger than the one behind the current time does not move it.
        const uint8_t fakeA[4] = {0xa1, 0, 0, 1}, fakeB[4] = {0xa1, 0, 0, 2}, leste[4] = {7, 7, 7, 7};
        bridge->onRadioTime(fakeA, real + 7 * 3600);
        bridge->onRadioTime(fakeB, real + 7 * 3600 + 5);
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).rfind(wanted, 0) == 0);
        // A third honest radio only adds support.
        bridge->onRadioTime(leste, real - 10);
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).rfind(wanted, 0) == 0);
    }

    // A phone's own announcement supplies the time, and replaces what LoRa gave.
    Phone ana("ana", 1, 11), bia("bia", 2, 12), caio("caio", 3, 13);
    for (const auto *phone : {&ana, &bia, &caio})
        phone->connect();
    run(100);
    for (const auto *phone : {&ana, &bia, &caio})
        CHECK(received(phone->handle).empty());
    for (const auto *phone : {&ana, &bia, &caio})
        phone->write(phone->announce());
    run(400);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa" && !bridge->hint()[0]);
    {
        char diag[200];
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).find(" phone.") != std::string::npos);
    }
    // The companion clock itself is left alone: its date decides whether other nodes accept this one.
    CHECK(mesh.clockSeconds() < 1716000000);

    // Each phone learns the bridge identity, with the marker that keeps other bridges from echoing it.
    PeerId bridgeId{};
    for (const auto *phone : {&ana, &bia, &caio}) {
        size_t own = 0;
        for (const auto &frame : received(phone->handle)) {
            Packet packet;
            Announcement identity;
            CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok && packet.type == 1);
            CHECK(decodeAnnouncement(packet.payload, identity) == Result::Ok);
            if (!identity.gateway)
                continue;
            CHECK(frame.size() <= 182 && packet.payload.size < 100);
            CHECK(std::string(identity.nickname.data()) == "LoRa bridge 00c0");
            bridgeId = packet.sender;
            ++own;
        }
        CHECK(own >= 1);
    }

    // Bitchat to MeshCore: relayed to the other phones, sent once to LoRa as "nickname: text".
    fakeSent.clear();
    const Frame hello = ana.say("oi pessoal");
    ana.write(hello);
    run(200);
    CHECK(chat(bia.handle) == std::vector<std::string>{"oi pessoal"} && chat(caio.handle) == chat(bia.handle));
    CHECK(chat(ana.handle).empty());
    const uint32_t lostBefore = bridge->drops();
    Frame copy = hello;
    --copy.data[2];
    bia.write(copy);
    caio.write(copy);
    run(2000);
    CHECK(texts(mesh.radio) == std::vector<std::string>{"ana: oi pessoal"} && mesh.radio[0].channel == 1);
    // Dated by the bridge's clock, and shown to the app attached to this radio as a received message.
    CHECK(mesh.radio[0].timestamp >= EPOCH && mesh.radio[0].timestamp < EPOCH + 3600);
    CHECK(texts(mesh.app) == texts(mesh.radio) && mesh.app[0].channel == 1);
    CHECK(chat(bia.handle).size() == 1 && bridge->drops() == lostBefore);

    // A conversation; the old "!lora " prefix is accepted and hidden.
    mesh.radio.clear();
    bia.write(bia.say("tudo bem?"));
    caio.write(caio.say("!lora cheguei"));
    ana.write(ana.say("vamos"));
    ana.write(ana.say("agora"));
    run(12000);
    auto talk = texts(mesh.radio);
    CHECK(talk.size() == 4 && std::count(talk.begin(), talk.end(), "bia: tudo bem?") == 1);
    CHECK(std::count(talk.begin(), talk.end(), "caio: cheguei") == 1);
    talk.erase(std::remove_if(talk.begin(), talk.end(), [](const std::string &m) { return m.rfind("ana: ", 0) != 0; }),
               talk.end());
    CHECK(talk == (std::vector<std::string>{"ana: vamos", "ana: agora"}));

    // Long text is split to fit a MeshCore message, never truncated; imported text is never sent back.
    mesh.radio.clear();
    std::string longText;
    while (longText.size() < 330)
        longText += "mensagem longa ";
    bia.write(bia.say(longText));
    bia.write(bia.say("[MC x] eco"));
    bia.write(bia.say("[bridge] eco"));
    run(12000);
    CHECK(mesh.radio.size() == 3 && mesh.radio[0].text.rfind("bia (1/3): mensagem", 0) == 0);
    std::string joined;
    for (const auto &part : mesh.radio) {
        CHECK(part.text.size() <= 160);
        joined += part.text.substr(11);
    }
    CHECK(joined == longText);

    // MeshCore to Bitchat: "sender: text" from LoRa reaches every phone, signed by the bridge.
    fakeSent.clear();
    mesh.app.clear();
    for (int i = 1; i <= 4; ++i) {
        fromLoRa("Jo\xc3\xa3o: resposta " + std::to_string(i));
        run(300);
    }
    fromLoRa("sem remetente");
    fromLoRa("Jo\xc3\xa3o: outro canal", 2);
    fromLoRa("Jo\xc3\xa3o: !bridge off");
    run(500);
    for (const auto *phone : {&ana, &bia, &caio}) {
        const auto messages = chat(phone->handle, &bridgeId);
        CHECK(messages.size() == 5 && messages[0] == "[MC Jo\xc3\xa3o] resposta 1" && messages[3] == "[MC Jo\xc3\xa3o] resposta 4");
        CHECK(messages[4] == "[MC mesh] sem remetente");
    }
    // A command heard on the air changes nothing, and what came from LoRa is not sent back to it.
    CHECK(bridge->enabled() && mesh.radio.size() == 3 && mesh.app.empty());

    // The same packet heard again is not delivered twice.
    fakeSent.clear();
    {
        uint8_t hash[8] = {9, 9, 9, 9, 9, 9, 9, 9};
        bridge->onChannelText(1, hash, "Lia: uma vez");
        run(300);
        bridge->onChannelText(1, hash, "Lia: uma vez");
        run(300);
        CHECK(chat(ana.handle, &bridgeId) == std::vector<std::string>{"[MC Lia] uma vez"});
    }

    // Text typed in the app attached to this radio goes out as usual and also reaches Bitchat.
    fakeSent.clear();
    CHECK(!fromApp("do aplicativo"));
    CHECK(!fromApp("em outro canal", 0));
    run(300);
    CHECK(chat(bia.handle, &bridgeId) == std::vector<std::string>{"[MC R\xc3\xa1"
                                                                   "dio Sul] do aplicativo"});

    // The self-test from Bitchat is answered over Bluetooth only.
    fakeSent.clear();
    mesh.radio.clear();
    ana.write(ana.say("!bridge"));
    run(600);
    {
        const auto answer = chat(ana.handle, &bridgeId);
        CHECK(answer.size() == 1 && answer[0].rfind("[bridge] Ready: Bitchat + LoRa. Phones 3, people 3.", 0) == 0);
        CHECK(mesh.radio.empty());
    }

    // The guide is answered in Bitchat too, in the language of the word used, and not more than twice a minute.
    fakeSent.clear();
    ana.write(ana.say("!ponte guia"));
    run(600);
    {
        const auto guide = chat(bia.handle, &bridgeId);
        CHECK(guide.size() == GUIDE_LINES && guide[0].rfind("[bridge] Guia 1/4:", 0) == 0 &&
              guide[3].rfind("[bridge] 4/4: no privado comigo, escreva ajuda. No #mesh: !ponte quem", 0) == 0);
        for (const auto &line : guide)
            CHECK(line.size() <= BITCHAT_TEXT_LIMIT);
        CHECK(mesh.radio.empty());
    }
    fakeSent.clear();
    bia.write(bia.say("!bridge guide"));
    run(600);
    CHECK(chat(ana.handle, &bridgeId).empty());
    run(30000);
    fakeSent.clear();
    bia.write(bia.say("!bridge guide "));
    run(600);
    {
        const auto guide = chat(ana.handle, &bridgeId);
        CHECK(guide.size() == GUIDE_LINES && guide[0].rfind("[bridge] Guide 1/4:", 0) == 0);
        CHECK(mesh.radio.empty());
    }

    // The full report in the app: state, counters and one line per Bluetooth link, each a short message.
    mesh.app.clear();
    CHECK(fromApp("!Bridge "));
    run(100);
    CHECK(mesh.app.size() == 7 && mesh.radio.empty());
    CHECK(mesh.app[0].text.rfind("bridge: Ready: Bitchat + LoRa. Bluetooth 3, Bitchat 3, people 3. LoRa #bitchat out ", 0) == 0);
    CHECK(mesh.app[1].text.find(" phone. rx ") != std::string::npos);
    CHECK(mesh.app[2].text.rfind("bridge: con 3 dis 0 sub 3 wr ", 0) == 0);
    CHECK(mesh.app[3].text.rfind("bridge: link 11 mtu 185 ", 0) == 0 && mesh.app[5].text.rfind("bridge: link 13 ", 0) == 0);
    CHECK(mesh.app[6].text == "bridge: Bitchat link OK. Send !bridge in Bitchat #mesh to test the way back.");
    for (const auto &line : mesh.app)
        CHECK(line.text.size() <= LORA_TEXT_LIMIT);

    // The app sets the companion clock: from then on that is the time the bridge uses.
    mesh.setClock(EPOCH + millis() / 1000 + 1);
    run(1000);
    char details[160];
    bridge->diagnostics(details, sizeof(details));
    CHECK(std::string(details).find(" app. rx ") != std::string::npos);
    mesh.radio.clear();
    caio.write(caio.say("com hora do app"));
    run(3000);
    CHECK(texts(mesh.radio) == std::vector<std::string>{"caio: com hora do app"});
    CHECK(mesh.radio[0].timestamp == mesh.clockSeconds() || mesh.radio[0].timestamp + 3 >= mesh.clockSeconds());

    // Off and on from the app, for radios without screen or menu.
    mesh.app.clear();
    mesh.radio.clear();
    CHECK(fromApp("!bridge off"));
    run(100);
    CHECK(!bridge->enabled() && mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: Bridge turned off.", 0) == 0);
    ana.write(ana.say("ninguem ouve"));
    run(3000);
    CHECK(mesh.radio.empty() && std::string(bridge->status()) == "Bridge is off");
    CHECK(fromApp("!bridge on"));
    run(1100);
    CHECK(bridge->enabled() && mesh.app.back().text.rfind("bridge: Bridge turned on.", 0) == 0);
    // The phones stayed connected; they announce again as the app does periodically.
    for (const auto *phone : {&ana, &bia, &caio}) {
        phone->connect();
        phone->write(phone->announce());
    }
    run(32000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");

    // After the counts of people above: the payments bring people from afar.
    {
        const auto before = mesh.radio;
        cashuTunnel(ana, bia, bridgeId);
        mesh.radio = before;
    }
    {
        const auto before = mesh.radio;
        const auto battery = mesh.battery;
        privateChat(ana, bia, caio, bridgeId);
        directChat(ana, bia, bridgeId);
        tunnelChat(ana, bia, caio, bridgeId);
        mesh.radio = before;
        mesh.battery = battery;
    }

    // Another channel is chosen by sending the command in it; the Public channel is refused.
    mesh.app.clear();
    CHECK(fromApp("!bridge lora", 0));
    run(100);
    CHECK(bridge->textChannel() == 1 && mesh.app.back().text.find("Public channel cannot be used") != std::string::npos);
    mesh.channels[3].name = "amigos";
    mesh.channels[3].secret.fill(0x42);
    CHECK(fromApp("!bridge lora", 3));
    run(100);
    CHECK(bridge->textChannel() == 3 && mesh.app.back().text == "bridge: Bitchat + LoRa on this channel." &&
          mesh.app.back().channel == 3);
    // A new channel is listened to for half a minute first, in case another bridge already serves it.
    CHECK(std::string(bridge->status()).rfind("Starting LoRa: ", 0) == 0);
    run(31000);
    mesh.radio.clear();
    fakeSent.clear();
    bia.write(bia.say("no canal novo"));
    fromLoRa("Jo\xc3\xa3o: pelo canal novo", 3);
    fromLoRa("Jo\xc3\xa3o: pelo canal antigo", 1);
    run(3000);
    CHECK(texts(mesh.radio) == std::vector<std::string>{"bia: no canal novo"} && mesh.radio[0].channel == 3);
    CHECK(chat(ana.handle, &bridgeId) == std::vector<std::string>{"[MC Jo\xc3\xa3o] pelo canal novo"});

    // Bluetooth only: phones still relay to each other, nothing goes to LoRa, and the choice is kept.
    CHECK(fromApp("!bridge local", 3));
    run(100);
    CHECK(bridge->textChannel() == NO_CHANNEL && std::string(bridge->status()) == "Ready: Bitchat only");
    mesh.radio.clear();
    fakeSent.clear();
    ana.write(ana.say("s\xc3\xb3 por perto"));
    run(15000);
    CHECK(mesh.radio.empty() && chat(bia.handle) == std::vector<std::string>{"s\xc3\xb3 por perto"});
    CHECK(bridge->textChannel() == NO_CHANNEL);
    // In this mode the guide says that nothing goes to LoRa.
    mesh.app.clear();
    CHECK(fromApp("!bridge guide", 3));
    run(100);
    CHECK(mesh.app.size() == GUIDE_LINES && mesh.app[0].text.find("LoRa is off") != std::string::npos);

    // Back to the public channel, which still exists.
    CHECK(fromApp("!bridge lora", 1));
    run(31000);
    CHECK(bridge->textChannel() == 1 && std::string(bridge->status()) == "Ready: Bitchat + LoRa");

    // Text another bridge brought from LoRa is not sent back to it.
    mesh.radio.clear();
    Phone other("LoRa bridge 0001", 9, 13);
    const uint8_t marker[] = {GATEWAY_TLV, 8, 1, 2, 3, 4, 5, 6, 7, 8};
    caio.write(other.announce({marker, sizeof(marker)}, 6));
    run(300);
    caio.write(other.packet(2, {reinterpret_cast<const uint8_t *>("de outra ponte"), 14}, 6));
    run(3000);
    CHECK(mesh.radio.empty());

    // What a Meshtastic bridge brought to Bitchat crosses to this network once, under the name it had there.
    const auto bridged = [&](const std::string &message) {
        caio.write(other.packet(2, {reinterpret_cast<const uint8_t *>(message.data()), message.size()}, 6));
        run(3000);
    };
    CHECK(bridge->linked());
    bridged("[MT Node Fixo CJ] Oi");
    CHECK(texts(mesh.radio) == std::vector<std::string>{"MT Node Fixo CJ: Oi"} && mesh.radio[0].channel == 1);
    mesh.radio.clear();
    bridged("[MT Node Fixo CJ 2/3] segunda parte");
    CHECK(texts(mesh.radio) == std::vector<std::string>{"MT Node Fixo CJ 2/3: segunda parte"});
    mesh.radio.clear();
    // Text that already crossed, in either marking, and text of this network's own bridges stay where they are.
    bridged("[MT MC ana] de volta");
    bridged("[MT Node Fixo CJ] MC ana: de volta");
    bridged("[MC Jo\xc3\xa3o] daqui mesmo");
    bridged("[MT ] sem nome");
    bridged("[MT sem fim");
    CHECK(mesh.radio.empty());
    // On this side it reaches other phones marked, so that no bridge takes it back.
    fakeSent.clear();
    fromLoRa("MT Node Fixo CJ: Oi");
    run(300);
    CHECK(chat(ana.handle, &bridgeId) == std::vector<std::string>{"[MC MT Node Fixo CJ] Oi"});
    // The crossing can be turned off, and the choice is kept.
    mesh.app.clear();
    CHECK(fromApp("!bridge link off"));
    run(100);
    CHECK(!bridge->linked() && mesh.app.back().text.rfind("bridge: Link off", 0) == 0);
    bridged("[MT Node Fixo CJ] com a liga\xc3\xa7\xc3\xa3o desligada");
    CHECK(mesh.radio.empty());
    CHECK(fromApp("!bridge"));
    run(100);
    CHECK(mesh.app.size() > 2 && mesh.app[1].text.find(". Link off") != std::string::npos);
    CHECK(fromApp("!bridge link on"));
    run(100);
    CHECK(bridge->linked() && mesh.app.back().text.rfind("bridge: Link on", 0) == 0);
    bridged("[MT Node Fixo CJ] ligada de novo");
    CHECK(texts(mesh.radio) == std::vector<std::string>{"MT Node Fixo CJ: ligada de novo"});
    mesh.radio.clear();

    // The radio holds text while it is busy and gives it up after two minutes.
    mesh.busy = true;
    bia.write(bia.say("espera"));
    run(5000);
    CHECK(mesh.radio.empty());
    mesh.busy = false;
    run(2000);
    CHECK(texts(mesh.radio) == std::vector<std::string>{"bia: espera"});
    mesh.radio.clear();
    mesh.busy = true;
    const uint32_t lostHeld = bridge->drops();
    bia.write(bia.say("tarde demais"));
    run(125000);
    mesh.busy = false;
    run(2000);
    CHECK(mesh.radio.empty() && bridge->drops() == lostHeld + 1);

    // A wiped channel list heals by itself while the public channel is the one in use.
    for (const auto *phone : {&ana, &bia, &caio})
        phone->write(phone->announce());
    mesh.channels[1] = {};
    run(12000);
    CHECK(mesh.channels[1].name == PUBLIC_CHANNEL_NAME && bridge->textChannel() == 1);
    // With every slot taken there is nowhere to put it, and the state says so.
    mesh.channels[1] = {};
    for (size_t i = 1; i < mesh.channels.size(); ++i) {
        mesh.channels[i].name = "cheio";
        mesh.channels[i].secret.fill(uint8_t(0x50 + i));
    }
    run(12000);
    CHECK(std::string(bridge->status()) == "LoRa channel changed");
    for (size_t i = 1; i < mesh.channels.size(); ++i)
        mesh.channels[i] = {};
    run(12000);
    CHECK(mesh.channels[1].name == PUBLIC_CHANNEL_NAME);
    run(31000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");

    // Restart: choices, identity and the record of delivered packets are kept.
    {
        uint8_t hash[8] = {7, 7, 7, 7, 7, 7, 7, 7};
        fakeSent.clear();
        bridge->onChannelText(1, hash, "Lia: antes do rein\xc3\xad"
                                       "cio");
        run(300);
        CHECK(chat(ana.handle, &bridgeId).size() == 1);
        const unsigned savesBefore = mesh.saves;
        delete bridge;
        bridge = new BitchatBridge;
        bridge->begin(mesh);
        run(1500);
        CHECK(bridge->enabled() && bridge->textChannel() == 1 && mesh.saves == savesBefore);
        for (const auto *phone : {&ana, &bia, &caio}) {
            phone->connect();
            phone->write(phone->announce());
        }
        run(32000);
        CHECK(std::string(bridge->status()).rfind("Bitchat starts: ", 0) == 0);
        run(30000);
        CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
        fakeSent.clear();
        bridge->onChannelText(1, hash, "Lia: antes do rein\xc3\xad"
                                       "cio");
        run(300);
        CHECK(chat(ana.handle).empty());
        bool same = false;
        ana.write(ana.say("!bridge"));
        run(600);
        for (const auto &frame : received(ana.handle)) {
            Packet packet;
            CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
            same = same || packet.sender == bridgeId;
        }
        CHECK(same);
    }

    // The radio's own button walks through the three modes, and the public channel comes back by itself.
    CHECK(std::string(bridge->modeName()) == "Bitchat + LoRa");
    CHECK(bridge->nextMode() && std::string(bridge->modeName()) == "Bitchat only" && bridge->textChannel() == NO_CHANNEL);
    CHECK(bridge->nextMode() && std::string(bridge->modeName()) == "Off" && !bridge->enabled());
    run(1500);
    CHECK(std::string(bridge->status()) == "Bridge is off");
    CHECK(bridge->nextMode() && std::string(bridge->modeName()) == "Bitchat + LoRa");
    run(1500);
    CHECK(bridge->enabled() && bridge->textChannel() == 1 && mesh.channels[1].name == PUBLIC_CHANNEL_NAME);
    {
        unsigned phones = 9, people = 9;
        uint32_t out = 0, in = 0;
        bridge->counters(phones, people, out, in);
        CHECK(phones == blePhones() && people == 0);
    }

    // Bluetooth switched off at the radio stops the bridge; on again, it resumes.
    platformBluetooth.up = false;
    run(1500);
    CHECK(std::string(bridge->status()) == "Bluetooth is off");
    platformBluetooth.up = true;
    run(1500);
    CHECK(std::string(bridge->status()) != "Bluetooth is off" && bridge->enabled());

    // The economy mode is the operator's choice and starts off: with no Bitchat phone the radio keeps
    // announcing itself at full speed.
    run(61000);
    CHECK(bleAdvertise() && fakeAdvertisingInterval == 244 && blePhones() == 3 && !bridge->saving());
    for (const auto *phone : {&ana, &bia, &caio})
        bleDisconnect(phone->handle);
    run(200000);
    CHECK(blePhones() == 0 && fakeAdvertisingInterval == 244);
    mesh.app.clear();
    CHECK(fromApp("!bridge"));
    run(100);
    CHECK(mesh.app.size() > 2 && mesh.app[0].text.find(". Economy off") != std::string::npos);
    // Turned on with every phone long gone, it takes effect at once and shows in the state.
    CHECK(fromApp("!bridge economy on"));
    run(300);
    CHECK(bridge->saving() && mesh.app.back().text.rfind("bridge: Economy on", 0) == 0);
    CHECK(fakeAdvertisingInterval == 1636 && bridge->enabled());
    mesh.app.clear();
    CHECK(fromApp("!bridge"));
    run(300);
    CHECK(mesh.app.size() > 2 && mesh.app[0].text.find(". Economy active") != std::string::npos);
    // The first phone that talks brings the fast rhythm back; three minutes after it leaves, the slow
    // one returns.
    {
        Phone dora("dora", 4, 21);
        dora.connect();
        run(300);
        dora.write(dora.announce());
        run(600);
        CHECK(blePhones() == 1 && fakeAdvertisingInterval == 244);
        run(200000);
        CHECK(fakeAdvertisingInterval == 244);   // the phone is still talking
        bleDisconnect(dora.handle);
        run(1000);
        CHECK(blePhones() == 0 && fakeAdvertisingInterval == 244);
        run(175000);
        CHECK(fakeAdvertisingInterval == 244);
        run(8000);
        CHECK(fakeAdvertisingInterval == 1636);
    }
    // The choice survives a restart, and turning it off restores full speed without waiting.
    delete bridge;
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    run(1500);
    CHECK(bridge->saving() && bridge->enabled());
    run(185000);
    CHECK(fakeAdvertisingInterval == 1636);
    CHECK(fromApp("!bridge economy off"));
    run(300);
    CHECK(!bridge->saving() && mesh.app.back().text.rfind("bridge: Economy off", 0) == 0);
    CHECK(fakeAdvertisingInterval == 244);
    run(200000);
    CHECK(fakeAdvertisingInterval == 244);
    // Help: five whole messages in the language asked for, naming every command once.
    for (const bool portuguese : {false, true}) {
        mesh.app.clear();
        CHECK(fromApp(portuguese ? "!ponte ajuda" : "!bridge help"));
        run(100);
        CHECK(mesh.app.size() == 5);
        std::string all;
        for (const auto &line : mesh.app) {
            CHECK(line.text.back() == '.' && line.text.size() <= LORA_TEXT_LIMIT);
            all += line.text + "\n";
        }
        CHECK(mesh.app[0].text.rfind(portuguese ? "bridge: Ajuda 1/5:" : "bridge: Help 1/5:", 0) == 0);
        for (const char *word :
             portuguese ? std::vector<const char *>{"!ponte guia", "quem", "túnel on/off", "privado on/off", "recado <texto>",
                                                    "economia on/off", "prefixo on/off", "posicao on/off", "link on/off",
                                                    "som on/off", "luz on/off", "!bridge help"}
                        : std::vector<const char *>{"!bridge guide", "who", "tunnel on/off", "dm on/off", "note <text>",
                                                    "economy on/off", "prefix on/off", "position on/off", "link on/off",
                                                    "sound on/off", "light on/off", "!ponte ajuda"})
            CHECK(all.find(word) != std::string::npos);
    }
    // The radio's buzzer and light are set from the app and kept by the radio; Bitchat only gets the state.
    mesh.app.clear();
    CHECK(fromApp("!ponte som off"));
    run(100);
    CHECK(fromApp("!bridge light off"));
    run(100);
    CHECK(mesh.sound == 0 && mesh.light == 0 && mesh.app.size() == 2);
    CHECK(mesh.app[0].text == "bridge: Som desligado: o rádio não apita mais. Vale também depois de reiniciar.");
    CHECK(mesh.app[1].text == "bridge: Light off: the status light stays dark. It stays so after a restart.");
    mesh.app.clear();
    CHECK(fromApp("!ponte luz on"));
    run(100);
    CHECK(mesh.light == 1 && mesh.app.size() == 1 && mesh.app[0].text == "bridge: Luz ligada: a luz de aviso pisca como de costume.");
    mesh.hasAlerts = false;
    mesh.app.clear();
    CHECK(fromApp("!bridge sound on"));
    run(100);
    CHECK(mesh.sound == 0 && mesh.app.size() == 1 && mesh.app[0].text == "bridge: This radio has no buzzer.");
    mesh.hasAlerts = true;
    mesh.app.clear();
    // The guide in the app attached to the radio ends with the commands, in either language.
    mesh.app.clear();
    CHECK(fromApp("!bridge guide"));
    run(100);
    CHECK(mesh.app.size() == GUIDE_LINES && mesh.app[0].text.rfind("bridge: Guide 1/4:", 0) == 0 &&
          mesh.app[3].text.find("!bridge help") != std::string::npos);
    mesh.app.clear();
    CHECK(fromApp("!Ponte Guia"));
    run(100);
    CHECK(mesh.app.size() == GUIDE_LINES && mesh.app[0].text.rfind("bridge: Guia 1/4:", 0) == 0 &&
          mesh.app[3].text.find("!ponte ajuda") != std::string::npos);
    for (const auto &line : mesh.app)
        CHECK(line.text.size() <= 160);

    // Public history: text from LoRa with no phone around is signed and kept, and the phone that arrives
    // later asks for what it lacks, the way the app does with every neighbour.
    // Restarted with nobody around, the bridge has no clock and cannot sign: such text is still lost.
    CHECK(blePhones() == 0 && std::string(bridge->status()) == "Clock not set");
    fromLoRa("Rita: sem hora");
    run(300);
    {
        Phone gil("gil", 6, 23);
        gil.connect();
        run(300);
        gil.write(gil.announce());
        run(600);
        CHECK(blePhones() == 1 && std::string(bridge->status()) == "Ready: Bitchat + LoRa");
        bleDisconnect(gil.handle);
        run(1000);
    }
    CHECK(blePhones() == 0 && std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    // Nobody spoke on LoRa since this start.
    mesh.app.clear();
    CHECK(fromApp("!ponte quem"));
    run(100);
    CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Ninguém foi ouvido no LoRa nas últimas 6 h.");
    fakeSent.clear();
    for (const char *text : {"Rita: primeira", "Rita: segunda", "Rita: terceira", "Rita: quarta"}) {
        fromLoRa(text);
        run(300);
    }
    mesh.app.clear();
    CHECK(fromApp("!bridge"));
    run(100);
    CHECK(mesh.app.size() > 2 && mesh.app[0].text.find(". Kept 4") != std::string::npos);
    {
        Phone eva("eva", 5, 22);
        eva.connect();
        run(300);
        eva.write(eva.announce());
        run(600);
        // Nothing is pushed to a phone that did not ask.
        CHECK(blePhones() == 1 && chat(eva.handle, &bridgeId).empty());
        const auto held = [&]() {
            std::vector<SyncId> ids;
            for (const auto &frame : received(eva.handle)) {
                Packet packet;
                CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
                if (packet.type != 2 || !(packet.sender == bridgeId))
                    continue;
                // An answer to a request: for this phone only, and marked so the app accepts its age.
                CHECK(packet.ttl == 0 && packet.replay() && packet.signedPacket());
                SyncId id;
                syncId(crypto, packet, id);
                ids.push_back(id);
            }
            return ids;
        };
        // The phone holds nothing: it gets the three newest.
        auto wire = syncRequest(crypto, {});
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK((chat(eva.handle, &bridgeId) ==
               std::vector<std::string>{"[MC Rita] quarta", "[MC Rita] terceira", "[MC Rita] segunda"}));
        const auto first = held();
        CHECK(first.size() == 3);
        // Asked again at once, nothing more; five seconds later, holding those three, it gets the last.
        fakeSent.clear();
        wire = syncRequest(crypto, first);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK(chat(eva.handle, &bridgeId).empty());
        run(5000);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{"[MC Rita] primeira"}));
        auto all = first;
        for (const auto &id : held())
            all.push_back(id);
        // Holding everything, or asking for another kind of packet, it gets nothing.
        fakeSent.clear();
        run(5000);
        wire = syncRequest(crypto, all);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK(chat(eva.handle, &bridgeId).empty());
        run(5000);
        wire = syncRequest(crypto, {}, 7, 0, 1);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK(chat(eva.handle, &bridgeId).empty());
        // Live text is delivered as before, and kept as well; the answers of the bridge itself are not.
        fromLoRa("Rita: ao vivo");
        run(300);
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{"[MC Rita] ao vivo"}));
        eva.write(eva.say("!bridge"));
        run(600);
        mesh.app.clear();
        CHECK(fromApp("!bridge"));
        run(100);
        CHECK(mesh.app[0].text.find(". Kept 5") != std::string::npos);
        CHECK(mesh.radio.empty() || texts(mesh.radio).empty());
        // Who spoke on LoRa, the latest first: asked from Bitchat or from the app, in either language.
        fromLoRa("Zeca: oi");
        run(5000);
        fakeSent.clear();
        eva.write(eva.say("!ponte quem"));
        run(600);
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{"[bridge] No LoRa, últimas 6 h: Zeca, Rita"}));
        mesh.app.clear();
        CHECK(fromApp("!bridge who"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: On LoRa, last 6 h: Zeca, Rita");
        // More names than fit one Bitchat message: whole names, then how many were left out.
        for (int i = 0; i < 14; ++i) {
            fromLoRa("Pessoa de nome longo " + std::to_string(10 + i) + ": oi");
            run(300);
        }
        run(5000);
        fakeSent.clear();
        eva.write(eva.say("!bridge who"));
        run(600);
        {
            const auto who = chat(eva.handle, &bridgeId);
            CHECK(who.size() == 1 && who[0].size() <= BITCHAT_TEXT_LIMIT &&
                  who[0].rfind("[bridge] On LoRa, last 6 h: Pessoa de nome longo 23, Pessoa de nome longo 22", 0) == 0);
            CHECK(who[0].substr(who[0].size() - 4) == " +10");
        }
        // What a phone wrote is kept too, under its own signature, for the phone that arrives after it left.
        fakeSent.clear();
        eva.write(eva.say("Eva: volto as 18h"));
        run(600);
        {
            Phone fia("fia", 7, 24);
            bleDisconnect(eva.handle);
            run(1000);
            fia.connect();
            run(300);
            fia.write(fia.announce());
            run(600);
            run(5000);
            fakeSent.clear();
            const auto wire = syncRequest(crypto, {});
            fia.write(fia.packet(0x21, {wire.data(), wire.size()}, 0));
            run(600);
            bool found = false;
            for (const auto &frame : received(fia.handle)) {
                Packet packet;
                CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
                if (packet.type != 2 || !(packet.sender == eva.id))
                    continue;
                // the phone's own signature, still valid with the answer's time to live and flag
                Frame canonical;
                CHECK(packet.ttl == 0 && packet.replay() && signingBytes({frame.data(), frame.size()}, canonical) == Result::Ok);
                CHECK(crypto.verify(canonical.bytes(), packet.signature, eva.signingPublic.data()));
                found = text(packet.payload) == "Eva: volto as 18h";
            }
            CHECK(found);
            bleDisconnect(fia.handle);
            run(1000);
            eva.connect();
            run(300);
            eva.write(eva.announce());
            run(600);
        }
        // Six hours later nobody is listed.
        run(6 * 60 * 60 * 1000 + 1000);
        mesh.app.clear();
        CHECK(fromApp("!bridge who"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Nobody was heard on LoRa in the last 6 h.");

        // The newer commands, on a radio that knows its battery, its neighbours and its position.
        mesh.battery = 3920;
        mesh.heard = {radio("Casa Ana", 0, 300), radio("RPT Sul", 2, 4000), radio("Velho", 1, 90000)};
        mesh.located = true;
        mesh.latitude = -23550520;
        mesh.longitude = -46633310;
        eva.write(eva.announce());
        run(600);
        mesh.app.clear();
        CHECK(fromApp("!ponte bateria"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Bateria: 3,92 V, cerca de 69%.");
        mesh.app.clear();
        CHECK(fromApp("!bridge radios"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Radios heard: Casa Ana (direct, 5 min), RPT Sul (2 hops, 1 h)");
        fakeSent.clear();
        eva.write(eva.say("!ponte radios"));
        run(600);
        // Who the radio heard, and so who it talks to, is not told to strangers in Bitchat.
        CHECK((chat(eva.handle, &bridgeId) ==
               std::vector<std::string>{"[bridge] R\xc3\xa1" "dios: s\xc3\xb3 no aplicativo do pr\xc3\xb3prio r\xc3\xa1" "dio, por privacidade."}));
        mesh.app.clear();
        CHECK(fromApp("!ponte radios"));
        run(100);
        CHECK(mesh.app.size() == 1 &&
              mesh.app[0].text == "bridge: R\xc3\xa1" "dios ouvidos: Casa Ana (direto, 5 min), RPT Sul (2 saltos, 1 h)");
        // Many radios: whole entries, then how many were left out.
        for (int i = 0; i < 12; ++i)
            mesh.heard.push_back(radio(("Vizinho " + std::to_string(10 + i)).c_str(), -1, 60 + i, int16_t(-30 - i)));
        run(5000);
        fakeSent.clear();
        eva.write(eva.say("!bridge battery"));
        eva.write(eva.say("!bridge radios"));
        run(600);
        // one answer every five seconds: the second question waits for nobody
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{"[bridge] Battery: 3.92 V, about 69%."}));
        run(5000);
        mesh.app.clear();
        CHECK(fromApp("!bridge radios"));
        run(100);
        {
            CHECK(mesh.app.size() == 1);
            const std::string list = mesh.app[0].text;
            CHECK(list.size() <= LORA_TEXT_LIMIT &&
                  list.rfind("bridge: Radios heard: Vizinho 10 (1 min, -3.0 dB), Vizinho 11 (1 min, -3.1 dB)", 0) == 0);
            CHECK(list.find(" +") != std::string::npos);
        }
        mesh.heard.clear();
        mesh.app.clear();
        CHECK(fromApp("!ponte radios"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Nenhum r\xc3\xa1" "dio foi ouvido no LoRa nas \xc3\xba" "ltimas 24 h.");

        // The house note: left in the app, shown on request, given once to each phone that arrives.
        mesh.radio.clear();
        mesh.app.clear();
        CHECK(fromApp("!bridge note"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: No note. To leave one: !bridge note <text>");
        mesh.app.clear();
        CHECK(fromApp("!ponte recado Jantar \xc3\xa0s 20h, chave com a Rita"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: Recado guardado", 0) == 0 && mesh.radio.empty());
        mesh.app.clear();
        CHECK(fromApp("!bridge note"));
        run(100);
        CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Note from R\xc3\xa1" "dio Sul: Jantar \xc3\xa0s 20h, chave com a Rita");
        {
            Phone gal("gal", 9, 26);
            gal.connect();
            run(300);
            fakeSent.clear();
            gal.write(gal.announce());
            run(600);
            const std::string note = "[bridge] Recado de R\xc3\xa1" "dio Sul: Jantar \xc3\xa0s 20h, chave com a Rita";
            CHECK(contains(chat(gal.handle, &bridgeId), note));
            // only to the phone that arrived, and not to be relayed by it
            CHECK(!contains(chat(eva.handle, &bridgeId), note));
            for (const auto &frame : received(gal.handle)) {
                Packet packet;
                CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
                if (packet.type == 2 && packet.sender == bridgeId && text(packet.payload) == note)
                    CHECK(packet.ttl == 1);
            }
            // Once: a later announcement of the same phone gets nothing.
            run(30000);
            fakeSent.clear();
            gal.write(gal.announce());
            run(600);
            CHECK(!contains(chat(gal.handle, &bridgeId), note));
            // A phone may leave its own note; the next change waits a minute.
            run(5000);
            fakeSent.clear();
            gal.write(gal.say("!bridge note Back at 8, key under the mat"));
            run(600);
            CHECK((chat(gal.handle, &bridgeId) == std::vector<std::string>{"[bridge] Note kept: whoever arrives with Bitchat gets it."}));
            mesh.app.clear();
            CHECK(fromApp("!ponte recado"));
            run(100);
            CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Recado de gal: Back at 8, key under the mat");
            run(5000);
            fakeSent.clear();
            gal.write(gal.say("!bridge note off"));
            run(600);
            CHECK((chat(gal.handle, &bridgeId) ==
                   std::vector<std::string>{"[bridge] The note changed a moment ago; it can change again in 10 min."}));
            run(600000);
            gal.write(gal.announce());
            run(600);
            fakeSent.clear();
            gal.write(gal.say("!bridge note off"));
            run(600);
            CHECK((chat(gal.handle, &bridgeId) == std::vector<std::string>{"[bridge] Note cleared."}));
            // A phone further along the Bitchat mesh, whose message was relayed, does not change the note.
            run(600000);
            gal.write(gal.announce());
            run(5600);
            fakeSent.clear();
            {
                const std::string relayed = "!bridge note de longe";
                gal.write(gal.packet(2, {reinterpret_cast<const uint8_t *>(relayed.data()), relayed.size()}, 6));
            }
            run(600);
            CHECK((chat(gal.handle, &bridgeId) == std::vector<std::string>{"[bridge] To change the note, be next to the radio."}));
            mesh.app.clear();
            CHECK(fromApp("!bridge note"));
            run(100);
            CHECK(mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: No note.", 0) == 0);
            // Commands are not text: none of them went to LoRa.
            CHECK(mesh.radio.empty());

            // Prefix: only text marked with !lora goes to LoRa; the choice shows in the state and the guide.
            mesh.app.clear();
            CHECK(fromApp("!bridge prefix on"));
            run(100);
            CHECK(bridge->prefixed() && mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: Prefix on: only #mesh text", 0) == 0);
            gal.write(gal.say("fica no Bitchat"));
            run(2000);
            CHECK(mesh.radio.empty());
            gal.write(gal.say("!lora vai ao LoRa"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"gal: vai ao LoRa"});
            mesh.radio.clear();
            mesh.app.clear();
            CHECK(fromApp("!bridge"));
            run(100);
            CHECK(mesh.app[0].text.find("LoRa #bitchat (!lora) out") != std::string::npos);
            run(30000);
            fakeSent.clear();
            gal.write(gal.say("!ponte guia"));
            run(600);
            {
                const auto guide = chat(gal.handle, &bridgeId);
                CHECK(guide.size() == GUIDE_LINES && guide[0].find("!lora") != std::string::npos);
            }
            mesh.app.clear();
            CHECK(fromApp("!ponte prefix off"));
            run(100);
            CHECK(!bridge->prefixed() && mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: Prefixo desligado", 0) == 0);
            gal.write(gal.say("tudo de novo"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"gal: tudo de novo"});
            mesh.radio.clear();

            // A help request goes to LoRa with the radio's position, once every five minutes.
            run(5000);
            fakeSent.clear();
            // Asked from Bitchat, the position goes only where the radio shares it in its adverts.
            gal.write(gal.say("!ponte sos ca\xc3\xad na trilha"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"gal: SOS: ca\xc3\xad na trilha"});
            CHECK((chat(gal.handle, &bridgeId) ==
                   std::vector<std::string>{"[bridge] SOS enviado ao LoRa, sem posi\xc3\xa7\xc3\xa3o: este r\xc3\xa1" "dio n\xc3\xa3o a divulga."}));
            mesh.radio.clear();
            run(300000);
            mesh.shared = true;
            fakeSent.clear();
            gal.write(gal.announce());
            run(600);
            gal.write(gal.say("!ponte sos ca\xc3\xad na trilha"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"gal: SOS: ca\xc3\xad na trilha (-23.55052, -46.63331)"});
            CHECK(contains(chat(gal.handle, &bridgeId), "[bridge] SOS enviado ao LoRa, com a posi\xc3\xa7\xc3\xa3o do r\xc3\xa1" "dio."));
            mesh.shared = false;
            mesh.radio.clear();
            // On a trail the operator lets an SOS from Bitchat carry the position, without sharing it in adverts.
            mesh.app.clear();
            CHECK(fromApp("!ponte posi\xc3\xa7\xc3\xa3o on"));
            run(100);
            CHECK(bridge->sosPositioned() && mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: Posi\xc3\xa7\xc3\xa3o ligada", 0) == 0);
            // from Bitchat the setting is not changed: the phone is told that only the owner changes it
            run(5000);
            fakeSent.clear();
            gal.write(gal.say("!bridge position off"));
            run(600);
            CHECK(bridge->sosPositioned());
            {
                bool told = false;
                for (const auto &line : chat(gal.handle, &bridgeId))
                    told |= line.find("Only the radio's owner changes this") != std::string::npos;
                CHECK(told);
            }
            run(300000);
            gal.write(gal.announce());
            run(600);
            gal.write(gal.say("!ponte sos na trilha"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"gal: SOS: na trilha (-23.55052, -46.63331)"});
            // the choice is stored, as the other options are
            CHECK(Preferences::data.at("sospos") == std::vector<uint8_t>{1});
            CHECK(fromApp("!bridge position off"));
            run(100);
            CHECK(!bridge->sosPositioned() && Preferences::data.at("sospos") == std::vector<uint8_t>{0});
            mesh.radio.clear();
            run(6000);
            fakeSent.clear();
            gal.write(gal.say("!bridge sos"));
            run(2000);
            CHECK(mesh.radio.empty());
            CHECK((chat(gal.handle, &bridgeId) == std::vector<std::string>{"[bridge] SOS sent a moment ago; it can be repeated in 5 min."}));
            run(300000);
            mesh.located = false;
            mesh.app.clear();
            CHECK(fromApp("!bridge sos"));
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"R\xc3\xa1" "dio Sul: SOS: help needed"});
            CHECK(mesh.app.size() == 2 && mesh.app[1].text == "R\xc3\xa1" "dio Sul: SOS: help needed" &&
                  mesh.app[0].text == "bridge: SOS sent to LoRa, without a position: the radio does not know it.");
            mesh.radio.clear();

            // Range test: this radio's probe, answered by another bridge with how it arrived.
            mesh.app.clear();
            CHECK(fromApp("!ponte teste"));
            run(2000);
            CHECK(mesh.radio.size() == 1 && mesh.radio[0].text.rfind("R\xc3\xa1" "dio Sul: #ponte-teste ", 0) == 0);
            const std::string id = mesh.radio[0].text.substr(mesh.radio[0].text.size() - 4);
            CHECK(id >= "1000" && id <= "9999");
            CHECK(mesh.app.size() == 2 && mesh.app[0].text.rfind("bridge: Teste " + id + " enviado ao LoRa", 0) == 0);
            mesh.radio.clear();
            mesh.app.clear();
            CHECK(fromApp("!ponte teste"));
            run(100);
            CHECK(mesh.radio.empty() && mesh.app.size() == 1 && mesh.app[0].text.rfind("bridge: J\xc3\xa1 h\xc3\xa1 um teste", 0) == 0);
            mesh.app.clear();
            fakeSent.clear();
            fromLoRa("Casa Ana: #ponte-teste " + id + " ouvido -4.5 dB", 1, -60);
            run(100);
            CHECK(mesh.app.size() == 1 &&
                  mesh.app[0].text == "bridge: Teste " + id + ": Casa Ana ouviu com -4,5 dB; a volta chegou com -6,0 dB.");
            // an answer to some other probe, or this one after the wait, is not reported
            fromLoRa("Casa Ana: #ponte-teste 0001 ouvido -4.5 dB", 1, -60);
            run(121000);
            CHECK(mesh.app.size() == 1);
            CHECK(chat(gal.handle, &bridgeId).empty());
            // Another bridge's probe is answered once, with the signal it came in; it is not text for Bitchat.
            fakeSent.clear();
            fromLoRa("Casa Ana: #ponte-teste 4321", 1, 55);
            run(2000);
            fromLoRa("Casa Ana: #ponte-teste 4321", 1, 55);
            run(2000);
            CHECK(texts(mesh.radio) == std::vector<std::string>{"R\xc3\xa1" "dio Sul: #ponte-teste 4321 ouvido 5.5 dB"});
            CHECK(chat(gal.handle).empty());
            mesh.radio.clear();
            // Asked from Bitchat, with nobody answering.
            run(5000);
            fakeSent.clear();
            gal.write(gal.say("!bridge test"));
            run(2000);
            CHECK(mesh.radio.size() == 1 && mesh.radio[0].text.find(": #ponte-teste ") != std::string::npos);
            {
                const std::string probe = mesh.radio[0].text.substr(mesh.radio[0].text.size() - 4);
                CHECK(chat(gal.handle, &bridgeId).size() == 1);
                run(121000);
                const auto report = chat(gal.handle, &bridgeId);
                CHECK(report.size() == 2 && report[1] == "[bridge] Test " + probe + ": no other bridge answered within 2 min.");
            }
            mesh.radio.clear();
            // A long name is shortened so the whole result fits one Bitchat message.
            run(5000);
            fakeSent.clear();
            gal.write(gal.say("!bridge test"));
            run(2000);
            {
                const std::string probe = mesh.radio.at(0).text.substr(mesh.radio[0].text.size() - 4);
                fromLoRa("Ponte Comunitaria do Bairro 12: #ponte-teste " + probe + " ouvido -12.5 dB", 1, -125);
                run(600);
                const auto report = chat(gal.handle, &bridgeId);
                CHECK(report.size() == 2 && report[1].size() <= BITCHAT_TEXT_LIMIT &&
                      report[1] == "[bridge] Test " + probe +
                                       ": Ponte Comunitaria do heard it at -12.5 dB; the answer came at -12.5 dB.");
                run(121000);
            }
            mesh.radio.clear();
            // A help request typed on another radio is that person's text, and reaches Bitchat.
            fakeSent.clear();
            fromLoRa("Sul: !ponte sos preciso de ajuda");
            run(600);
            CHECK(chat(gal.handle, &bridgeId) == std::vector<std::string>{"[MC Sul] !ponte sos preciso de ajuda"});
            CHECK(mesh.radio.empty());
            // Where the request cannot go out, the refusal waits like any answer: a burst gets one reply.
            CHECK(fromApp("!bridge local"));
            run(300);
            run(6000);
            fakeSent.clear();
            for (int i = 0; i < 5; ++i)
                gal.write(gal.say("!bridge sos " + std::to_string(i)));
            run(1000);
            CHECK(chat(gal.handle, &bridgeId) == std::vector<std::string>{"[bridge] SOS not sent: this bridge is not on LoRa."});
            CHECK(fromApp("!bridge lora"));
            run(31000);
            mesh.app.clear();

            // Low battery is told to the phones once, and again only after a charge.
            fakeSent.clear();
            mesh.battery = 3400;
            run(61000);
            const std::string low = "[bridge] Bateria baixa / Battery low: 3,40 V";
            CHECK(chat(gal.handle, &bridgeId) == std::vector<std::string>{low});
            run(61000);
            CHECK(chat(gal.handle, &bridgeId) == std::vector<std::string>{low});
            mesh.battery = 3550;
            run(61000);
            mesh.battery = 3400;
            run(61000);
            CHECK(chat(gal.handle, &bridgeId).size() == 1);
            mesh.battery = 3800;
            run(61000);
            mesh.battery = 3400;
            run(61000);
            CHECK((chat(gal.handle, &bridgeId) == std::vector<std::string>{low, low}));
            mesh.battery = 0;
            mesh.app.clear();
            CHECK(fromApp("!bridge battery"));
            run(100);
            CHECK(mesh.app.size() == 1 && mesh.app[0].text == "bridge: Battery: this radio does not report it.");
        }
    }

    // A new node identity gives the bridge a new one too, kept across a restart.
    {
        const auto before = Preferences::data.at("identity");
        bridge->renewIdentity();
        CHECK(std::all_of(Preferences::data.at("identity").begin(), Preferences::data.at("identity").end(),
                          [](uint8_t b) { return b == 0; }));
        delete bridge;
        bridge = new BitchatBridge;
        bridge->begin(mesh);
        run(3000);
        const auto after = Preferences::data.at("identity");
        CHECK(after.size() == 64 && after != before &&
              !std::all_of(after.begin(), after.end(), [](uint8_t b) { return b == 0; }));
        run(3000);
        CHECK(Preferences::data.at("identity") == after);
    }

    // A radio with a GPS turns it on when nothing gave it the time, and back off after ten minutes without a
    // fix or as soon as the clock is set.
    mesh.gps = true;
    mesh.gpsCalls.clear();
    delete bridge;
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    run(1500);
    CHECK(mesh.gpsCalls == std::vector<bool>{true});
    run(590000);
    CHECK(mesh.gpsCalls.size() == 1);
    run(15000);
    CHECK((mesh.gpsCalls == std::vector<bool>{true, false}));
    delete bridge;
    mesh.gpsCalls.clear();
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    run(1500);
    CHECK(mesh.gpsCalls == std::vector<bool>{true});
    mesh.setClock(mesh.clockSeconds() + 100);
    run(3000);
    CHECK((mesh.gpsCalls == std::vector<bool>{true, false}));
    run(60000);
    CHECK(mesh.gpsCalls.size() == 2);

    // The Bluetooth stack restarted between two ticks (WadaMesh does it around an update over Wi-Fi): the
    // bridge starts over on the new server instead of staying silent with its old state.
    CHECK(bridge->enabled() && bleAdvertise());
    bleDetach();
    static NimBLEServer restarted;
    bleAttach(&restarted);
    CHECK(!bleAdvertise());
    run(1500);
    CHECK(bridge->enabled() && bleAdvertise());

    // A time from LoRa adverts is not a trusted one: the GPS is still turned on for the real time.
    delete bridge;
    mesh.gpsCalls.clear();
    mesh.clockBase = 1715770351;
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    {
        const uint8_t one[4] = {3, 1, 4, 1}, two[4] = {5, 9, 2, 6};
        // Later than any time this test stored as trusted, so the floor does not refuse it.
        const uint32_t later = uint32_t(nowUtc() / 1000) + 30 * 86400;
        bridge->onRadioTime(one, later);
        bridge->onRadioTime(two, later + 2);
        char diag[200];
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).find(" lora.") != std::string::npos);
    }
    run(1500);
    CHECK(mesh.gpsCalls == std::vector<bool>{true});

    // A time from one radio alone is replaced by a phone's, even an earlier one.
    delete bridge;
    mesh.gpsCalls.clear();
    bridge = new BitchatBridge;
    bridge->begin(mesh);
    {
        const uint8_t lone[4] = {2, 7, 1, 8};
        // Later than any time this test stored as trusted, so the floor refuses neither the radio nor the phone.
        const uint32_t base = uint32_t(nowUtc() / 1000) + 40 * 86400;
        bridge->onRadioTime(lone, base + 2 * 86400);
        char diag[200];
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).find(" lora.") != std::string::npos);
        // The bridge starts its Bluetooth side about half a minute after begin().
        Phone dora("dora", 9, 19);
        run(32000);
        dora.connect();
        run(100);
        stamp = uint64_t(base) * 1000;
        dora.write(dora.announce());
        run(400);
        bridge->diagnostics(diag, sizeof(diag));
        CHECK(std::string(diag).find(" phone.") != std::string::npos);
    }
    std::cout << "PASS: " << checks << " flow checks (host fakes, no hardware)\n";
    return 0;
}

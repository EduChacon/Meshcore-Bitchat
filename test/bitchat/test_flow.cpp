// End-to-end host test: the production bridge and Bluetooth adapter, simulated phones and a simulated MeshCore companion.
#include "HeapModel.h"
#include <NimBLEDevice.h>
#include "helpers/bitchat/BitchatBridge.h"
#include "helpers/bitchat/BridgePlatform.h"
#include "gcs_reference.h"
#include <Curve25519.h>
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
};
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
static void fromLoRa(const std::string &message, uint8_t channel = 1)
{
    static uint64_t counter = 5000;
    uint8_t hash[8];
    write64(hash, ++counter);
    bridge->onChannelText(channel, hash, message.c_str());
}
static bool fromApp(const std::string &message, uint8_t channel = 1)
{
    return bridge->onAppText(channel, message.data(), message.size());
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

    // A phone's own announcement supplies the time.
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
              guide[3].rfind("[bridge] 4/4: envie !bridge", 0) == 0);
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
    // Help fits in three messages and names the option and the guide.
    mesh.app.clear();
    CHECK(fromApp("!bridge help"));
    run(100);
    CHECK(mesh.app.size() == 3 && mesh.app[0].text.back() == '.' && mesh.app[1].text.back() == '.' &&
          mesh.app[1].text.find("economy on") != std::string::npos && mesh.app[2].text.find("!ponte guia") != std::string::npos);
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
          mesh.app[3].text.find("lista os comandos") != std::string::npos);
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
    }

    std::cout << "PASS: " << checks << " flow checks (host fakes, no hardware)\n";
    return 0;
}

// What the Bitchat bridge asks of the companion, for the WadaMesh touch firmware. Copied into WadaMesh's
// src/ by tools/bitchat/build_wadamesh.py; the same services as examples/companion_radio/MyMesh.cpp of
// MeshCore-Bitchat, adapted to WadaMesh's MyMesh: its history ring for the companion apps and its touch UI.
#ifdef WITH_BITCHAT_BRIDGE
#include "MyMesh.h"
#include "helpers/esp32/TouchPrefsStore.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <target.h>

// UITask.cpp: the last event decides whether a new message goes to a channel's thread or to a DM
extern UIEventType g_last_event;

#define BRIDGE_RESP_CODE_CHANNEL_MSG_RECV_V3 17
#define BRIDGE_PUSH_CODE_MSG_WAITING 0x83

static bool bridge_text_on_air = false;

// The channel's own region scope, as WadaMesh's composer and its app command use (its #614).
struct ChannelScope {
    MyMesh &mesh;
    bool pushed;
    ChannelScope(MyMesh &m, uint8_t index) : mesh(m), pushed(false)
    {
        char region[TOUCH_REGION_SCOPE_MAXLEN] = {0};
        touchPrefsGetChannelScope(index, region, sizeof(region));
        pushed = mesh.pushChannelScope(region);
    }
    ~ChannelScope()
    {
        if (pushed)
            mesh.popChannelScope();
    }
};

uint32_t MyMesh::clockSeconds()
{
    return getRTCClock()->getCurrentTime();
}

uint32_t MyMesh::nodeNumber()
{
    // first four bytes of the public key, in the order the default node name shows them
    return ((uint32_t)self_id.pub_key[0] << 24) | ((uint32_t)self_id.pub_key[1] << 16) | ((uint32_t)self_id.pub_key[2] << 8) |
           self_id.pub_key[3];
}

const char *MyMesh::nodeName()
{
    return _prefs.node_name;
}

uint8_t MyMesh::provideChannel(const char *name, const uint8_t secret[32])
{
    ChannelDetails ch;
    int free_idx = -1;
    for (int i = 0; i < MAX_GROUP_CHANNELS && getChannel(i, ch); i++) {
        if (ch.name[0] == 0) {
            if (free_idx < 0 && i > 0)
                free_idx = i; // slot 0 is left for the Public channel
        } else if (memcmp(ch.channel.secret, secret, sizeof(ch.channel.secret)) == 0) {
            return i;
        }
    }
    if (free_idx < 0)
        return bitchat::NO_CHANNEL;
    memset(&ch, 0, sizeof(ch));
    StrHelper::strncpy(ch.name, name, sizeof(ch.name));
    memcpy(ch.channel.secret, secret, sizeof(ch.channel.secret)); // setChannel() derives its hash
    if (!setChannel(free_idx, ch))
        return bitchat::NO_CHANNEL;
    saveChannels();
#ifdef DISPLAY_CLASS
    // the new channel shows in the conversations at once, not at the next periodic poll
    if (_ui)
        _ui->onThreadsChanged();
#endif
    return free_idx;
}

bool MyMesh::readChannel(uint8_t index, char name[32], uint8_t secret[32])
{
    ChannelDetails ch;
    if (index >= MAX_GROUP_CHANNELS || !getChannel(index, ch) || ch.name[0] == 0)
        return false;
    memcpy(name, ch.name, sizeof(ch.name));
    name[sizeof(ch.name) - 1] = 0;
    memcpy(secret, ch.channel.secret, sizeof(ch.channel.secret));
    return true;
}

uint32_t MyMesh::airtimeMs(size_t textSize)
{
    // header, transport codes, path length, channel hash and MAC, then the text in whole cipher blocks
    return _radio->getEstAirtimeFor(9 + ((5 + textSize + CIPHER_BLOCK_SIZE - 1) / CIPHER_BLOCK_SIZE) * CIPHER_BLOCK_SIZE);
}

bool MyMesh::radioBusy()
{
    return _mgr->getFreeCount() < 6 || _mgr->getOutboundTotal() >= 3;
}

bool MyMesh::sendChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size)
{
    ChannelDetails ch;
    if (size == 0 || size > MAX_TEXT_LEN || !getChannel(index, ch) || ch.name[0] == 0)
        return false;
    uint8_t temp[5 + MAX_TEXT_LEN + 1];
    memcpy(temp, &timestamp, 4);
    temp[4] = TXT_TYPE_PLAIN;
    memcpy(&temp[5], text, size); // already "<sender>: <msg>"
    temp[5 + size] = 0;
    auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, ch.channel, temp, 5 + size);
    if (pkt == NULL)
        return false;
    {
        ChannelScope scope(*this, index);
        sendFloodScoped(ch.channel, pkt);
    }
    // The apps and the screen see it as a message received on the channel, under its sender's name.
    bridge_text_on_air = true;
    showChannelText(index, timestamp, text, size);
    bridge_text_on_air = false;
    return true;
}

bool MyMesh::sendingBridgeText()
{
    return bridge_text_on_air;
}

void MyMesh::showChannelText(uint8_t index, uint32_t timestamp, const char *text, size_t size)
{
    int i = 0;
    out_frame[i++] = BRIDGE_RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = 0; // SNR: not heard on the air
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
    out_frame[i++] = index;
    out_frame[i++] = 0xFF; // no path
    out_frame[i++] = TXT_TYPE_PLAIN;
    memcpy(&out_frame[i], &timestamp, 4);
    i += 4;
    int tlen = (int)size;
    if (i + tlen > MAX_FRAME_SIZE)
        tlen = MAX_FRAME_SIZE - i;
    memcpy(&out_frame[i], text, tlen);
    i += tlen;
    const uint32_t hist_seq = addToHistoryRing(out_frame, i);
    if (_serial && _serial->isConnected()) {
        if (_serial->writeFrameToAll(out_frame, i) == (size_t)i && hist_seq != 0 &&
            _serial->companionUnsolicitedPushesBroadcastToAll())
            advanceHistoryClientsAfterV3Broadcast(hist_seq);
        uint8_t frame[1] = {BRIDGE_PUSH_CODE_MSG_WAITING};
        _serial->writeFrameToAll(frame, 1);
    }
#ifdef DISPLAY_CLASS
    if (_ui) {
        char line[MAX_TEXT_LEN + 1];
        const size_t n = size < MAX_TEXT_LEN ? size : MAX_TEXT_LEN;
        memcpy(line, text, n);
        line[n] = 0;
        ChannelDetails ch;
        const char *channel_name = getChannel(index, ch) && ch.name[0] ? ch.name : "Bitchat";
        // as onChannelMessageRecv does: the event first, so the text lands in the channel's thread. Text
        // this radio has just sent for a Bitchat phone gets no alert, but still the channel's event.
        if (bridge_text_on_air) {
            const UIEventType previous = g_last_event;
            g_last_event = UIEventType::channelMessage;
            _ui->newMsgFromPubWithMeta(0xFF, false, nullptr, channel_name, line, history_count, 0, 0);
            g_last_event = previous;
        } else {
            _ui->notify(UIEventType::channelMessage);
            _ui->newMsgFromPubWithMeta(0xFF, false, nullptr, channel_name, line, history_count, 0, 0);
        }
    }
#endif
}

bool MyMesh::sendChannelData(uint8_t index, uint16_t dataType, const uint8_t *data, size_t size)
{
    ChannelDetails ch;
    if (size == 0 || size > MAX_GROUP_DATA_LENGTH || !getChannel(index, ch) || ch.name[0] == 0)
        return false;
    ChannelScope scope(*this, index);
    return sendGroupData(ch.channel, NULL, OUT_PATH_UNKNOWN, dataType, data, (int)size);
}

bool MyMesh::nodeKeyPrefix(uint8_t keyPrefix[8])
{
    memcpy(keyPrefix, self_id.pub_key, 8);
    return true;
}

bool MyMesh::signAsNode(const uint8_t *data, size_t size, uint8_t signature[64])
{
    self_id.sign(signature, data, (int)size);
    return true;
}

// A radio heard in an advert is a contact with its whole public key: that key checks the signature.
bitchat::MeshHost::NodeCheck MyMesh::verifyNode(const uint8_t keyPrefix[8], const uint8_t *data, size_t size,
                                                const uint8_t signature[64], char name[32])
{
    ContactInfo *contact = lookupContactByPubKey(keyPrefix, 8);
    if (contact == NULL)
        return NodeCheck::Unknown;
    if (!contact->id.verify(signature, data, (int)size))
        return NodeCheck::Invalid;
    StrHelper::strncpy(name, contact->name, 32);
    return NodeCheck::Valid;
}

uint16_t MyMesh::batteryMilliVolts()
{
    return board.getBattMilliVolts();
}

// The contacts heard most recently, by this radio's clock; the route length when one is known.
size_t MyMesh::heardRadios(HeardRadio *out, size_t capacity)
{
    const uint32_t now = getRTCClock()->getCurrentTime();
    size_t count = 0;
    ContactInfo c;
    ContactsIterator contacts = startContactsIterator();
    while (capacity && contacts.hasNext(this, c)) {
        if (c.lastmod == 0 || c.lastmod > now)
            continue;
        const uint32_t ago = now - c.lastmod;
        size_t slot = count;
        if (count == capacity) {
            // full: the one heard longest ago gives way
            slot = 0;
            for (size_t j = 1; j < count; j++)
                if (out[j].secondsAgo > out[slot].secondsAgo)
                    slot = j;
            if (out[slot].secondsAgo <= ago)
                continue;
        } else {
            count++;
        }
        StrHelper::strncpy(out[slot].name, c.name, sizeof(out[slot].name));
        out[slot].hops = c.out_path_len == OUT_PATH_UNKNOWN ? -1 : (int8_t)(c.out_path_len & 63);
        out[slot].secondsAgo = ago;
        out[slot].snrTenths = bitchat::SNR_UNKNOWN;
    }
    return count;
}

// The position the radio uses for itself: set in the app, or from the GPS.
bool MyMesh::position(int32_t &latitude, int32_t &longitude)
{
    if (sensors.node_lat == 0 && sensors.node_lon == 0)
        return false;
    latitude = (int32_t)(sensors.node_lat * 1000000.0);
    longitude = (int32_t)(sensors.node_lon * 1000000.0);
    return true;
}

bool MyMesh::positionShared()
{
    return _prefs.advert_loc_policy != ADVERT_LOC_NONE;
}
#endif

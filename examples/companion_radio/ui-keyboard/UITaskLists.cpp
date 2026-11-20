// The three list screens of the keyboard UI (contacts, channels, the radios around) and what feeds
// them: the contact table, adverts heard, repeater discovery answers and message acknowledgements.
#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <M5Cardputer.h>
#include <climits>

extern MyMesh the_mesh;

namespace {

lgfx::LovyanGFX& surface(DisplayDriver* display) {
    return static_cast<M5CardputerDisplay*>(display)->gfx();
}

// Layout, in pixels, of the 240 x 135 screen
const int HEADER_H = 19;      // settings icon, title, PIN, battery and Bluetooth
const int ROW_TOP = 20;
const int ROW_H = 22;         // a name in large letters with its details in small ones
const int HINT_Y = 110;       // one line of help, or the search text
const int TABS_Y = 121;       // Contacts | Channels | Mesh
const uint16_t GREY = 0x8410;
const uint16_t DIM = 0x4208;

const int MESH_ACTIONS = 3;   // advert nearby, advert to the whole mesh, find repeaters
const uint32_t DISCOVER_MS = 15000;

bool matchesFilter(const char* name, const char* filter) {
    if (!filter[0]) return true;
    char lower_name[33];
    char lower_filter[33];
    int i = 0;
    for (; i < 32 && name[i]; i++) lower_name[i] = tolower((unsigned char)name[i]);
    lower_name[i] = '\0';
    for (i = 0; i < 32 && filter[i]; i++) lower_filter[i] = tolower((unsigned char)filter[i]);
    lower_filter[i] = '\0';
    return strstr(lower_name, lower_filter) != nullptr;
}

const char* typeLabel(uint8_t type) {
    switch (type) {
        case ADV_TYPE_REPEATER: return "rpt";
        case ADV_TYPE_ROOM: return "room";
        case ADV_TYPE_SENSOR: return "sens";
        default: return "";
    }
}

int batteryPercent(uint16_t mv) {
#ifdef WITH_BITCHAT_BRIDGE
    // the lithium curve "!bridge battery" uses, not a straight line from 3.0 to 4.2 V
    const int percent = bitchat::batteryPercent(mv);
#else
    const int percent = ((int)mv - 3000) * 100 / (4200 - 3000);
#endif
    return percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

} // namespace

void UITask::formatAge(uint32_t seconds, char* out, size_t size) {
    if (seconds < 60) snprintf(out, size, "now");
    else if (seconds < 3600) snprintf(out, size, "%lum", (unsigned long)(seconds / 60));
    else if (seconds < 86400) snprintf(out, size, "%luh", (unsigned long)(seconds / 3600));
    else if (seconds < 30UL * 86400) snprintf(out, size, "%lud", (unsigned long)(seconds / 86400));
    else out[0] = '\0';
}

// ---------------------------------------------------------------- shared drawing

void UITask::renderListHeader(const char* title) {
    uint16_t main_color = COLORS[_main_color_idx].rgb565;
    uint16_t secondary_color = COLORS[_secondary_color_idx].rgb565;

    // settings icon (three lines); highlighted when selected
    if (_settings_selected) {
        surface(_display).fillRect(0, 0, 22, HEADER_H - 1, main_color);
    }
    const uint16_t lines = _settings_selected ? secondary_color : main_color;
    surface(_display).fillRect(4, 4, 14, 2, lines);
    surface(_display).fillRect(4, 8, 14, 2, lines);
    surface(_display).fillRect(4, 12, 14, 2, lines);

    _display->setColor(CardColor::LIGHT);
    _display->setTextSize(2);
    _display->setCursor(28, 2);
    _display->print(title);

    // right: the Bluetooth PIN, then the battery and the Bluetooth state
    _display->setTextSize(1);
    char text[24];
    const uint32_t pin = the_mesh.getBLEPin();
    if (pin != 0) snprintf(text, sizeof(text), "PIN %06lu", (unsigned long)pin);
    else snprintf(text, sizeof(text), "no PIN");
    _display->setCursor(238 - _display->getTextWidth(text), 1);
    _display->print(text);

    const bool bt_on = isBluetoothEnabled();
    const bool app = hasConnection();
    const uint16_t mv = getBattMilliVolts();
    char battery[8] = "";
    if (mv > 0) snprintf(battery, sizeof(battery), "%d%%", batteryPercent(mv));
    const char* bt = !bt_on ? "BT off" : app ? "app" : "BT";
    const int bt_w = _display->getTextWidth(bt);
    const int battery_w = _display->getTextWidth(battery);
    int x = 238 - bt_w;
    _display->setColor(!bt_on ? CardColor::RED : app ? CardColor::GREEN : CardColor::LIGHT);
    _display->setCursor(x, 10);
    _display->print(bt);
    if (battery[0]) {
        _display->setColor(mv < 3450 ? CardColor::RED : CardColor::LIGHT);
        _display->setCursor(x - 6 - battery_w, 10);
        _display->print(battery);
    }

    surface(_display).drawFastHLine(0, HEADER_H - 1, 240, DIM);
}

void UITask::renderListRow(int slot, bool selected, const char* name, const char* right, bool unread) {
    const int y = ROW_TOP + slot * ROW_H;
    if (selected) {
        _display->setColor(CardColor::LIGHT);
        _display->fillRect(0, y, 240, ROW_H - 1);
    } else {
        surface(_display).drawFastHLine(6, y + ROW_H - 1, 228, DIM);
    }
    if (unread) {
        surface(_display).fillRect(0, y + 3, 4, ROW_H - 7, 0xFFE0);
    }

    // details at the right, in small letters
    _display->setTextSize(1);
    int right_w = right && right[0] ? _display->getTextWidth(right) : 0;
    if (right_w) {
        if (selected) _display->setColor(CardColor::DARK);
        else surface(_display).setTextColor(GREY);
        _display->setCursor(236 - right_w, y + 7);
        _display->print(right);
    }

    // the name, as long as it fits
    char filtered[32];
    filterDisplayText(name, filtered, sizeof(filtered));
    int max_chars = (236 - (right_w ? right_w + 6 : 0) - 8) / 12;
    if (max_chars < 1) max_chars = 1;
    if (max_chars > 31) max_chars = 31;
    if ((int)strlen(filtered) > max_chars) {
        filtered[max_chars] = '\0';
        if (max_chars > 1) filtered[max_chars - 1] = '~';
    }
    _display->setColor(selected ? CardColor::DARK : CardColor::LIGHT);
    _display->setTextSize(2);
    _display->setCursor(8, y + 3);
    _display->print(filtered);
}

void UITask::renderListFooter(const char* hint) {
    _display->setTextSize(1);
    if (_search_filter_length > 0) {
        char filtered[32];
        filterDisplayText(_search_filter, filtered, sizeof(filtered));
        const int len = strlen(filtered);
        char line[48];
        snprintf(line, sizeof(line), "Search: %s_  (Opt clears)", filtered + (len > 14 ? len - 14 : 0));
        _display->setColor(CardColor::YELLOW);
        _display->setCursor(4, HINT_Y);
        _display->print(line);
    } else if (_delete_armed_until && millis() < _delete_armed_until) {
        _display->setColor(CardColor::RED);
        _display->setCursor(4, HINT_Y);
        _display->print("Fn+Del again to delete");
    } else if (hint && hint[0]) {
        surface(_display).setTextColor(GREY);
        _display->setCursor(4, HINT_Y);
        _display->print(hint);
    }

    // tabs
    static const char* const labels[3] = { "Contacts", "Channels", "Mesh" };
    const int active = _menu_state == MenuScreen::CONTACTS ? 0 : _menu_state == MenuScreen::CHANNELS ? 1 : 2;
    surface(_display).drawFastHLine(0, TABS_Y - 1, 240, DIM);
    for (int i = 0; i < 3; i++) {
        const int x = i * 80;
        if (i == active) {
            _display->setColor(CardColor::LIGHT);
            _display->fillRect(x, TABS_Y, 80, 135 - TABS_Y);
            _display->setColor(CardColor::DARK);
        } else {
            _display->setColor(CardColor::LIGHT);
        }
        _display->setCursor(x + (80 - _display->getTextWidth(labels[i])) / 2, TABS_Y + 3);
        _display->print(labels[i]);
    }
}

// ---------------------------------------------------------------- data for the lists

// Saved contacts, favourites first and then the most recently heard. The first slots of the contact
// table are reserved by MeshCore for anonymous requests and are never listed.
int UITask::buildContactRows(int* out, int max) {
    static uint32_t keys[MAX_CONTACTS + 8];
    int count = 0;
    const int total = the_mesh.getTotalContactSlots();
    for (int i = 8; i < total && count < max; i++) {   // 8 = MAX_ANON_CONTACTS
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(i, contact) || contact.name[0] == '\0') continue;
        if (!matchesFilter(contact.name, _search_filter)) continue;
        const uint32_t key = (contact.isFav() ? 0x80000000UL : 0) | (contact.lastmod >> 1);
        int at = count++;
        while (at > 0 && keys[at - 1] < key) {   // insertion sort, newest first
            keys[at] = keys[at - 1];
            out[at] = out[at - 1];
            at--;
        }
        keys[at] = key;
        out[at] = i;
    }
    return count;
}

int UITask::buildChannelRows(uint8_t* out, int max) {
    int count = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS && count < max; i++) {
        ChannelDetails channel;
        if (the_mesh.getChannel(i, channel) && channel.name[0] != '\0' && matchesFilter(channel.name, _search_filter)) {
            out[count++] = (uint8_t)i;
        }
    }
    return count;
}

bool UITask::hasUnread(const char* name, bool is_channel) const {
    for (int i = 0; i < _chat_history_count; i++) {
        const ChatMessage& msg = _chat_history[i];
        if (msg.unread && msg.is_channel == is_channel && strcmp(msg.contact_or_channel, name) == 0) return true;
    }
    return false;
}

void UITask::markConversationRead(const char* name, bool is_channel) {
    for (int i = 0; i < _chat_history_count; i++) {
        ChatMessage& msg = _chat_history[i];
        if (msg.is_channel == is_channel && strcmp(msg.contact_or_channel, name) == 0) msg.unread = false;
    }
}

void UITask::rememberHeard(const uint8_t* pub_key, const char* name, uint8_t type, uint8_t hops, int8_t snr4) {
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < _heard_count; i++) {
        if (memcmp(_heard[i].prefix, pub_key, sizeof(_heard[i].prefix)) == 0) {
            slot = i;
            break;
        }
        if ((int32_t)(_heard[i].at - _heard[oldest].at) < 0) oldest = i;
    }
    bool fresh = slot < 0;
    if (fresh) {
        slot = _heard_count < MAX_HEARD_NODES ? _heard_count++ : oldest;
        memset(&_heard[slot], 0, sizeof(_heard[slot]));
        memcpy(_heard[slot].prefix, pub_key, sizeof(_heard[slot].prefix));
        _heard[slot].hops = 0xFF;
        _heard[slot].snr4 = INT8_MIN;
    }
    HeardNode& node = _heard[slot];
    if (name && name[0]) {
        strncpy(node.name, name, sizeof(node.name) - 1);
        node.name[sizeof(node.name) - 1] = '\0';
    } else if (!node.name[0]) {
        snprintf(node.name, sizeof(node.name), "%02X%02X%02X%02X", pub_key[0], pub_key[1], pub_key[2], pub_key[3]);
    }
    if (type != ADV_TYPE_NONE) node.type = type;
    if (hops != 0xFF) node.hops = hops;
    if (snr4 != INT8_MIN) node.snr4 = snr4;
    node.at = millis();
    if (_menu_state == MenuScreen::MESH) _need_refresh = true;
}

int UITask::sortedHeard(int* out) {
    int count = 0;
    for (int i = 0; i < _heard_count; i++) {
        int at = count++;
        while (at > 0 && (int32_t)(_heard[out[at - 1]].at - _heard[i].at) < 0) {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = i;
    }
    return count;
}

// ---------------------------------------------------------------- screens

void UITask::renderContactList() {
    renderListHeader("Contacts");
    static int rows[MAX_CONTACTS];
    const int count = buildContactRows(rows, MAX_CONTACTS);
    if (_selected_idx >= count) _selected_idx = count - 1;
    if (_scroll_pos > _selected_idx && _selected_idx >= 0) _scroll_pos = _selected_idx;
    if (_scroll_pos < 0 || count <= LIST_ROWS) _scroll_pos = 0;

    if (count == 0) {
        _display->setColor(CardColor::LIGHT);
        _display->setTextSize(2);
        const char* msg = _search_filter_length > 0 ? "No match" : "No contacts yet";
        _display->setCursor((240 - _display->getTextWidth(msg)) / 2, 44);
        _display->print(msg);
        _display->setTextSize(1);
        if (_search_filter_length == 0) {
            const char* tip = "Mesh tab: send an advert";
            _display->setCursor((240 - _display->getTextWidth(tip)) / 2, 70);
            _display->print(tip);
        }
    }

    const uint32_t now = rtc_clock.getCurrentTime();
    for (int slot = 0; slot < LIST_ROWS; slot++) {
        const int idx = _scroll_pos + slot;
        if (idx >= count) break;
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(rows[idx], contact)) continue;
        char age[8] = "";
        if (contact.lastmod && now >= contact.lastmod) formatAge(now - contact.lastmod, age, sizeof(age));
        char right[20];
        const char* type = typeLabel(contact.type);
        snprintf(right, sizeof(right), "%s%s%s%s", contact.isFav() ? "* " : "", type, type[0] && age[0] ? " " : "", age);
        renderListRow(slot, idx == _selected_idx && !_settings_selected, contact.name, right,
                      hasUnread(contact.name, false));
    }

    char hint[48];
    if (count > LIST_ROWS) snprintf(hint, sizeof(hint), "Enter: message  Fn+Del: delete  %d/%d",
                                    (_selected_idx < 0 ? 0 : _selected_idx) + 1, count);
    else snprintf(hint, sizeof(hint), "Enter: message  Fn+Del: delete  a-z: find");
    renderListFooter(hint);
}

void UITask::renderChannelList() {
    renderListHeader("Channels");
    uint8_t rows[MAX_GROUP_CHANNELS];
    const int channels = buildChannelRows(rows, MAX_GROUP_CHANNELS);
    const bool join_row = _search_filter_length == 0;
    const int count = channels + (join_row ? 1 : 0);
    if (_selected_idx >= count) _selected_idx = count - 1;
    if (_scroll_pos > _selected_idx && _selected_idx >= 0) _scroll_pos = _selected_idx;
    if (_scroll_pos < 0 || count <= LIST_ROWS) _scroll_pos = 0;

    if (count == 0) {
        _display->setColor(CardColor::LIGHT);
        _display->setTextSize(2);
        const char* msg = "No match";
        _display->setCursor((240 - _display->getTextWidth(msg)) / 2, 50);
        _display->print(msg);
    }

    for (int slot = 0; slot < LIST_ROWS; slot++) {
        const int idx = _scroll_pos + slot;
        if (idx >= count) break;
        const bool selected = idx == _selected_idx && !_settings_selected;
        if (idx == channels) {
            renderListRow(slot, selected, "+ Join channel", "#name", false);
            continue;
        }
        ChannelDetails channel;
        if (!the_mesh.getChannel(rows[idx], channel)) continue;
        renderListRow(slot, selected, channel.name, rows[idx] == 0 ? "public" : "", hasUnread(channel.name, true));
    }

    const bool on_join = join_row && _selected_idx == channels;
    renderListFooter(on_join ? "Enter: join a #channel by its name" : "Enter: open  Fn+Del: leave  a-z: find");
    if (_join_mode) renderJoinBox();
}

void UITask::renderJoinBox() {
    _display->setColor(CardColor::DARK);
    _display->fillRect(8, 30, 224, 70);
    _display->setColor(CardColor::LIGHT);
    _display->drawRect(8, 30, 224, 70);
    _display->setTextSize(1);
    _display->setCursor(16, 36);
    _display->print("Join a channel (hashtag)");

    char shown[24];
    const int start = _join_length > 16 ? _join_length - 16 : 0;
    snprintf(shown, sizeof(shown), "#%s", _join_name + start);
    _display->setTextSize(2);
    _display->setCursor(16, 54);
    _display->print(shown);
    if ((millis() / 350) % 2 == 0) {
        _display->fillRect(16 + _display->getTextWidth(shown), 54, 3, 14);
    }
    _display->setTextSize(1);
    surface(_display).setTextColor(GREY);
    _display->setCursor(16, 84);
    _display->print("Enter: join   Opt: cancel");
}

void UITask::renderMeshScreen() {
    renderListHeader("Mesh");
    int heard[MAX_HEARD_NODES];
    const int heard_count = sortedHeard(heard);
    const int count = MESH_ACTIONS + heard_count;
    if (_selected_idx >= count) _selected_idx = count - 1;
    if (_scroll_pos > _selected_idx && _selected_idx >= 0) _scroll_pos = _selected_idx;
    if (_scroll_pos < 0 || count <= LIST_ROWS) _scroll_pos = 0;

    const bool discovering = _discover_until && (int32_t)(millis() - _discover_until) < 0;
    for (int slot = 0; slot < LIST_ROWS; slot++) {
        const int idx = _scroll_pos + slot;
        if (idx >= count) break;
        const bool selected = idx == _selected_idx && !_settings_selected;
        char right[24] = "";
        if (idx == 0) {
            renderListRow(slot, selected, "Advert nearby", "0 hop", false);
        } else if (idx == 1) {
            renderListRow(slot, selected, "Advert to all", "flood", false);
        } else if (idx == 2) {
            if (discovering) {
                snprintf(right, sizeof(right), "%lus", (unsigned long)((_discover_until - millis() + 999) / 1000));
            } else if (_discover_tag) {
                snprintf(right, sizeof(right), "%d found", _discover_answers);
            }
            renderListRow(slot, selected, "Find repeaters", right, false);
        } else {
            const HeardNode& node = _heard[heard[idx - MESH_ACTIONS]];
            char age[8];
            formatAge((millis() - node.at) / 1000, age, sizeof(age));
            const char* type = typeLabel(node.type);
            char signal[10] = "";
            if (node.snr4 != INT8_MIN) snprintf(signal, sizeof(signal), " %ddB", node.snr4 / 4);
            else if (node.hops != 0xFF) snprintf(signal, sizeof(signal), " %uh", (unsigned)node.hops);
            snprintf(right, sizeof(right), "%s%s %s", type[0] ? type : "chat", signal, age);
            renderListRow(slot, selected, node.name, right, false);
        }
    }

    const char* hint;
    if (_selected_idx >= MESH_ACTIONS) hint = "Heard here. Enter: message or details";
    else if (heard_count == 0) hint = "Enter: run. Radios heard appear below";
    else hint = "Enter: run. Radios heard are below";
    renderListFooter(hint);
}

// ---------------------------------------------------------------- keys

void UITask::moveSelection(int count, bool up) {
    if (_settings_selected) {
        if (count == 0) return;
        _settings_selected = false;
        _selected_idx = up ? count - 1 : 0;
    } else if (up) {
        if (_selected_idx <= 0) {
            _settings_selected = true;   // above the first row: the settings icon
            _selected_idx = -1;
        } else {
            _selected_idx--;
        }
    } else {
        if (count == 0) {
            _settings_selected = true;
            return;
        }
        _selected_idx = _selected_idx + 1 >= count ? 0 : _selected_idx + 1;
    }
    if (_selected_idx >= 0) {
        if (_selected_idx < _scroll_pos) _scroll_pos = _selected_idx;
        if (_selected_idx >= _scroll_pos + LIST_ROWS) _scroll_pos = _selected_idx - LIST_ROWS + 1;
    }
}

void UITask::switchListScreen(bool next) {
    static const MenuScreen order[3] = { MenuScreen::CONTACTS, MenuScreen::CHANNELS, MenuScreen::MESH };
    int at = _menu_state == MenuScreen::CONTACTS ? 0 : _menu_state == MenuScreen::CHANNELS ? 1 : 2;
    at = (at + (next ? 1 : 2)) % 3;
    _menu_state = order[at];
    _scroll_pos = 0;
    _selected_idx = 0;
    _settings_selected = false;
    _search_filter_length = 0;
    _search_filter[0] = '\0';
    _delete_armed_until = 0;
}

void UITask::openContactChat(const ContactInfo& contact) {
    _chat_contact = contact;
    _menu_state = MenuScreen::CHAT;
    _chat_is_channel = false;
    _input_mode = false;
    _input_buffer[0] = '\0';
    _input_length = 0;
    _chat_scroll = 0;
    _chat_msg_scroll_index = 0;
    _search_filter_length = 0;
    _search_filter[0] = '\0';
    markConversationRead(contact.name, false);
}

void UITask::openChannelChat(uint8_t mesh_idx) {
    if (!the_mesh.getChannel(mesh_idx, _chat_channel)) return;
    _menu_state = MenuScreen::CHAT;
    _chat_is_channel = true;
    _input_mode = false;
    _input_buffer[0] = '\0';
    _input_length = 0;
    _chat_scroll = 0;
    _chat_msg_scroll_index = 0;
    _search_filter_length = 0;
    _search_filter[0] = '\0';
    _channel_has_unread[mesh_idx] = false;
    markConversationRead(_chat_channel.name, true);
}

void UITask::joinChannel() {
    if (_join_length == 0) return;
    char name[32];
    snprintf(name, sizeof(name), "#%s", _join_name);
    // a hashtag channel: the key is the first half of the SHA-256 of its name, as in the MeshCore app
    ChannelDetails channel;
    memset(&channel, 0, sizeof(channel));
    StrHelper::strncpy(channel.name, name, sizeof(channel.name));
    uint8_t hash[32];
    mesh::Utils::sha256(hash, sizeof(hash), (const uint8_t*)name, strlen(name));
    memcpy(channel.channel.secret, hash, 16);

    int free_slot = -1;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails existing;
        if (!the_mesh.getChannel(i, existing)) break;
        if (existing.name[0] == '\0') {
            if (free_slot < 0 && i > 0) free_slot = i;   // slot 0 is the Public channel's
        } else if (memcmp(existing.channel.secret, channel.channel.secret, sizeof(channel.channel.secret)) == 0) {
            _join_mode = false;
            char msg[64];
            snprintf(msg, sizeof(msg), "Already in %s", existing.name);
            showNotice(msg, 1500);
            return;
        }
    }
    if (free_slot < 0 || !the_mesh.setChannel(free_slot, channel)) {
        showNotice("No free channel slot", 2000);
        return;
    }
    the_mesh.uiSaveChannels();
    _join_mode = false;
    _join_length = 0;
    _join_name[0] = '\0';
    char msg[64];
    snprintf(msg, sizeof(msg), "Joined %s", name);
    showNotice(msg, 1500);
    // select the new channel
    uint8_t rows[MAX_GROUP_CHANNELS];
    const int count = buildChannelRows(rows, MAX_GROUP_CHANNELS);
    for (int i = 0; i < count; i++) {
        if (rows[i] == free_slot) {
            _selected_idx = i;
            _settings_selected = false;
            if (_selected_idx < _scroll_pos) _scroll_pos = _selected_idx;
            if (_selected_idx >= _scroll_pos + LIST_ROWS) _scroll_pos = _selected_idx - LIST_ROWS + 1;
        }
    }
}

void UITask::deleteSelected() {
    if (_settings_selected || _selected_idx < 0) return;
    if (!_delete_armed_until || (int32_t)(millis() - _delete_armed_until) >= 0) {
        _delete_armed_until = millis() + 3000;   // asks once; the hint line says how to confirm
        return;
    }
    _delete_armed_until = 0;
    char msg[64];
    if (_menu_state == MenuScreen::CONTACTS) {
        static int rows[MAX_CONTACTS];
        const int count = buildContactRows(rows, MAX_CONTACTS);
        if (_selected_idx >= count) return;
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(rows[_selected_idx], contact)) return;
        ContactInfo* live = the_mesh.lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
        if (!live || !the_mesh.removeContact(*live)) return;
        the_mesh.uiSaveContacts();
        snprintf(msg, sizeof(msg), "Deleted %s", contact.name);
    } else if (_menu_state == MenuScreen::CHANNELS) {
        uint8_t rows[MAX_GROUP_CHANNELS];
        const int count = buildChannelRows(rows, MAX_GROUP_CHANNELS);
        if (_selected_idx >= count) return;   // the join row
        ChannelDetails channel;
        if (!the_mesh.getChannel(rows[_selected_idx], channel)) return;
        ChannelDetails empty;
        memset(&empty, 0, sizeof(empty));
        the_mesh.setChannel(rows[_selected_idx], empty);
        the_mesh.uiSaveChannels();
        snprintf(msg, sizeof(msg), "Left %s", channel.name);
    } else {
        return;
    }
    if (_selected_idx > 0) _selected_idx--;
    if (_scroll_pos > _selected_idx) _scroll_pos = _selected_idx;
    showNotice(msg, 1500);
}

void UITask::runMeshAction(int row) {
    if (row == 0 || row == 1) {
        const bool flood = row == 1;
        if (the_mesh.uiAdvert(flood)) showNotice(flood ? "Advert sent to all" : "Advert sent nearby", 1200);
        else showNotice("Advert failed", 1500);
    } else if (row == 2) {
        uint32_t tag = 0;
        if (the_mesh.uiDiscoverRepeaters(tag)) {
            _discover_tag = tag;
            _discover_until = millis() + DISCOVER_MS;
            _discover_answers = 0;
        } else {
            showNotice("Radio busy, try again", 1500);
        }
    } else {
        int heard[MAX_HEARD_NODES];
        const int count = sortedHeard(heard);
        const int at = row - MESH_ACTIONS;
        if (at < 0 || at >= count) return;
        const HeardNode& node = _heard[heard[at]];
        ContactInfo* contact = the_mesh.lookupContactByPubKey(node.prefix, sizeof(node.prefix));
        if (contact && contact->type == ADV_TYPE_CHAT) {
            openContactChat(*contact);
            return;
        }
        char text[96];
        char age[8];
        formatAge((millis() - node.at) / 1000, age, sizeof(age));
        const char* type = node.type == ADV_TYPE_REPEATER ? "repeater" : node.type == ADV_TYPE_ROOM ? "room"
                         : node.type == ADV_TYPE_SENSOR ? "sensor" : "chat";
        char signal[16] = "";
        if (node.snr4 != INT8_MIN) snprintf(signal, sizeof(signal), " SNR %d dB", node.snr4 / 4);
        else if (node.hops != 0xFF) snprintf(signal, sizeof(signal), " %u hops", (unsigned)node.hops);
        snprintf(text, sizeof(text), "%s, %s%s, %s%s", node.name, type, signal, age, contact ? "" : " (not saved)");
        showNotice(text, 3000);
    }
}

void UITask::handleListKey(Keyboard_Class::KeysState& status) {
    if (_join_mode) {
        bool cancel = status.opt;
        if (status.fn) {
            for (auto key : status.word) if (key == '`') cancel = true;
        }
        if (cancel) {
            _join_mode = false;
        } else if (status.enter) {
            joinChannel();
        } else if (status.del) {
            if (_join_length > 0) _join_name[--_join_length] = '\0';
        } else {
            for (auto key : status.word) {
                const bool allowed = isalnum((unsigned char)key) || key == '-' || key == '_';
                if (allowed && _join_length < (int)sizeof(_join_name) - 1) {
                    _join_name[_join_length++] = key;
                    _join_name[_join_length] = '\0';
                }
            }
        }
        return;
    }

    bool up = false, down = false, left = false, right = false;
    for (auto key : status.word) {
        if (key == ';') up = true;
        if (key == '.') down = true;
        if (key == ',') left = true;
        if (key == '/') right = true;
    }

    if (status.fn && status.del) {
        deleteSelected();
        return;
    }
    if (status.opt) {
        _search_filter_length = 0;
        _search_filter[0] = '\0';
        _delete_armed_until = 0;
        return;
    }
    if (left || right) {
        switchListScreen(right);
        return;
    }

    int count = 0;
    if (_menu_state == MenuScreen::CONTACTS) {
        static int rows[MAX_CONTACTS];
        count = buildContactRows(rows, MAX_CONTACTS);
    } else if (_menu_state == MenuScreen::CHANNELS) {
        uint8_t rows[MAX_GROUP_CHANNELS];
        count = buildChannelRows(rows, MAX_GROUP_CHANNELS) + (_search_filter_length == 0 ? 1 : 0);
    } else {
        count = MESH_ACTIONS + _heard_count;
    }
    if (_selected_idx >= count) _selected_idx = count - 1;

    if (up || down) {
        _delete_armed_until = 0;
        moveSelection(count, up);
        return;
    }

    const bool select = status.enter || (status.space && _search_filter_length == 0);
    if (select) {
        _delete_armed_until = 0;
        if (_settings_selected) {
            _menu_state = MenuScreen::SETTINGS;
            _settings_category = SettingsCategory::MAIN_MENU;
            _settings_menu_idx = 0;
            _settings_item_idx = 0;
            _settings_scroll_pos = 0;
            _settings_selected = false;
            return;
        }
        if (_selected_idx < 0) return;
        if (_menu_state == MenuScreen::CONTACTS) {
            static int rows[MAX_CONTACTS];
            const int n = buildContactRows(rows, MAX_CONTACTS);
            ContactInfo contact;
            if (_selected_idx < n && the_mesh.getContactByIdx(rows[_selected_idx], contact)) openContactChat(contact);
        } else if (_menu_state == MenuScreen::CHANNELS) {
            uint8_t rows[MAX_GROUP_CHANNELS];
            const int n = buildChannelRows(rows, MAX_GROUP_CHANNELS);
            if (_selected_idx < n) {
                openChannelChat(rows[_selected_idx]);
            } else if (_search_filter_length == 0) {
                _join_mode = true;
                _join_length = 0;
                _join_name[0] = '\0';
            }
        } else {
            runMeshAction(_selected_idx);
        }
        return;
    }

    if (_menu_state == MenuScreen::MESH) return;   // nothing to search there

    if (status.del) {
        // Backspace: one character now; held, the loop keeps deleting
        if (_backspace_hold_start == 0) {
            _backspace_hold_start = millis();
            _backspace_was_held = false;
        }
        if (_search_filter_length > 0) {
            _search_filter[--_search_filter_length] = '\0';
            _scroll_pos = 0;
            _selected_idx = 0;
        }
        return;
    }
    _backspace_hold_start = 0;
    _backspace_was_held = false;
    if (status.fn) return;
    for (auto key : status.word) {
        if (key >= 32 && key < 127 && _search_filter_length < 30) {
            _search_filter[_search_filter_length++] = key;
            _search_filter[_search_filter_length] = '\0';
            _scroll_pos = 0;
            _selected_idx = 0;
            _settings_selected = false;
            _delete_armed_until = 0;
        }
    }
}

// ---------------------------------------------------------------- what the mesh tells the UI

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
    const uint8_t hops = mesh::Packet::isValidPathLen(path_len) ? (path_len & 63) : 0xFF;
    rememberHeard(contact.id.pub_key, contact.name, contact.type, hops, INT8_MIN);
    if (_menu_state == MenuScreen::CONTACTS) _need_refresh = true;
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
    // a repeater's answer to "Find repeaters": type | node type, SNR x 4, tag, key (or its first 8 bytes)
    if (!pkt || pkt->payload_len < 6 + 8 || (pkt->payload[0] & 0xF0) != 0x90) return;
    if (!_discover_tag || (int32_t)(millis() - _discover_until) >= 0) return;
    uint32_t tag;
    memcpy(&tag, &pkt->payload[2], 4);
    if (tag != _discover_tag) return;
    const uint8_t* key = &pkt->payload[6];
    if (memcmp(key, the_mesh.self_id.pub_key, 8) == 0) return;
    ContactInfo* known = the_mesh.lookupContactByPubKey(key, 8);
    rememberHeard(key, known ? known->name : nullptr, pkt->payload[0] & 0x0F, 0, (int8_t)pkt->payload[1]);
    _discover_answers++;
    _need_refresh = true;
}

void UITask::onACKRecv(uint32_t ack_crc) {
    for (int i = _chat_history_count - 1; i >= 0; i--) {
        ChatMessage& msg = _chat_history[i];
        if (msg.is_outgoing && !msg.is_channel && msg.delivery == 1 && msg.ack_crc == ack_crc) {
            msg.delivery = 2;
            _need_refresh = true;
            return;
        }
    }
}

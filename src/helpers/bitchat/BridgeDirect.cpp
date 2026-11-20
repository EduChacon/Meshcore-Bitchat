// Direct messages between a Bitchat phone and a LoRa contact of the radio, through the private chat with the bridge.
// "@Ana te encontro às 18h" in that chat goes to Ana as a direct message of this radio, marked with the phone's
// name, when Ana is one of the owner's favourites; Ana's answer comes back to the same chat within half an hour, or
// whenever it starts with "@nick". The bridge reads both: it is not end to end. The owner's app sees the answers, as
// any direct message to the radio.
#ifdef WITH_BITCHAT_BRIDGE
#include "BitchatBridge.h"
#include "BridgeDiagnostics.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>

using namespace bitchat;

// How the LoRa side's messages are marked in the chat; the Meshtastic bridge writes "MT".
static constexpr char FROM_LORA[] = "[MC %s] %.*s";
static constexpr char TO_LORA[] = "[Bitchat %s] %.*s";
static constexpr size_t TO_LORA_OVERHEAD = sizeof("[Bitchat ] ") - 1;
// An answer without "@nick" goes to the phone that wrote to the contact this recently.
static constexpr uint32_t THREAD_MS = 30UL * 60 * 1000;
static constexpr uint32_t MAIL_MS = 24UL * 3600 * 1000;
// A phone is taken as here when it announced itself directly this recently.
static constexpr uint32_t HERE_MS = 60000;
// A mail item sent and not acknowledged is sent again after this long.
static constexpr uint32_t RESEND_MS = 30000;
static constexpr size_t MAIL_PER_PHONE = 3, MAIL_PER_SENDER = 3;

static Bytes trimmedStart(Bytes text)
{
    while (text.size && (text.data[0] == ' ' || text.data[0] == ':')) {
        ++text.data;
        --text.size;
    }
    return text;
}

// Control characters and line breaks become spaces, so a text cannot push its label out of sight.
static void flatten(char *text, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        if (uint8_t(text[i]) < 0x20 || uint8_t(text[i]) == 0x7f)
            text[i] = ' ';
}

static uint32_t textHash(Bytes text)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < text.size; ++i)
        hash = (hash ^ text.data[i]) * 16777619u;
    return hash;
}

// The first word, for telling the phone which name was not found.
static size_t firstWord(Bytes text, char *out, size_t capacity)
{
    size_t n = 0;
    while (n < text.size && text.data[n] != ' ' && text.data[n] != ':' && n + 1 < capacity)
        ++n;
    // never half a character
    while (n && n < text.size && (text.data[n] & 0xc0) == 0x80)
        --n;
    std::memcpy(out, text.data, n);
    out[n] = 0;
    flatten(out, n);
    return n;
}

// The phone named at the start of `text`: by its nickname when no other phone shares it, or by its label
// ("nick#1a2b") when one does. Only a phone here, never one behind another bridge nor a bridge.
Peer *BitchatBridge::namedPhone(Bytes text, size_t &consumed)
{
    Peer *peer = runtime->peers->named(text, consumed);
    if (!peer) {
        char label[LABEL_LIMIT + 8];
        for (size_t i = 0; i < PeerTable::CAPACITY && !peer; ++i) {
            Peer &entry = runtime->peers->slot(i);
            if (!entry.used || !runtime->peers->nicknameShared(entry))
                continue;
            const size_t n = peerLabel(entry, label, sizeof(label));
            if (n && text.size > n && (text.data[n] == ' ' || text.data[n] == ':') && std::memcmp(text.data, label, n) == 0) {
                peer = &entry;
                consumed = n;
            }
        }
    }
    if (peer && (peer->remote || peer->announcement.gateway))
        return nullptr;
    return peer;
}

void BitchatBridge::privateDirect(PrivateSession &session, const Peer &peer, Bytes text, uint32_t now)
{
    char reply[160];
    const auto say = [this](const char *line) { replyPrivately(line, std::strlen(line)); };
    if (!dmEnabled) {
        say("Mensagens privadas pelo LoRa estão desligadas neste rádio. / Direct messages over LoRa are off here.");
        return;
    }
    const size_t limit = host->directTextLimit();
    if (!limit) {
        say("Este rádio não manda mensagens diretas pelo LoRa. / This radio sends no direct messages over LoRa.");
        return;
    }
    // Lookups are limited like the messages: a phone cannot probe the owner's list name by name.
    RateBucket probePhone = session.directs, probeAll = runtime->directs;
    if (!probePhone.admit(now, 2, 20000) || !probeAll.admit(now, 5, 180000)) {
        say("Aguarde um pouco: limite de mensagens pelo LoRa. / Please wait: LoRa message limit.");
        return;
    }
    const Bytes rest{text.data + 1, text.size - 1};
    MeshHost::LoraContact contact{};
    size_t consumed = 0;
    bool ambiguous = false;
    const bool found = host->findContact(reinterpret_cast<const char *>(rest.data), rest.size, contact, consumed, ambiguous);
    contact.name[sizeof(contact.name) - 1] = 0;
    if (!found && rest.size + 2 < 64) {
        // The name alone, with nothing after it.
        char probe[64];
        std::memcpy(probe, rest.data, rest.size);
        std::memcpy(probe + rest.size, " .", 2);
        size_t whole = 0;
        bool twice = false;
        if (host->findContact(probe, rest.size + 2, contact, whole, twice) && whole == rest.size) {
            contact.name[sizeof(contact.name) - 1] = 0;
            snprintf(reply, sizeof(reply), "Escreva a mensagem depois do nome: @%s texto.", contact.name);
            say(reply);
            return;
        }
    }
    if (!found) {
        // The same answer for a name that is not there and one that is not a favourite.
        char word[33];
        firstWord(rest, word, sizeof(word));
        snprintf(reply, sizeof(reply),
                 "Não é possível mandar a \"%s\": só aos favoritos do rádio. / Only the radio's favourites.", word);
        say(reply);
        return;
    }
    if (ambiguous) {
        snprintf(reply, sizeof(reply), "Há mais de um contato \"%s\"; mensagem não enviada.", contact.name);
        say(reply);
        return;
    }
    const Bytes body = trimmedStart({rest.data + consumed, rest.size - consumed});
    if (!body.size) {
        snprintf(reply, sizeof(reply), "Escreva a mensagem depois do nome: @%s texto.", contact.name);
        say(reply);
        return;
    }
    char label[LABEL_LIMIT + 8];
    peerLabel(peer, label, sizeof(label));
    char line[256];
    const int size = snprintf(line, sizeof(line), TO_LORA, label, int(body.size), reinterpret_cast<const char *>(body.data));
    if (size < 0 || size_t(size) >= sizeof(line) || size_t(size) > limit) {
        const int room = int(limit) - int(std::strlen(label)) - int(TO_LORA_OVERHEAD);
        snprintf(reply, sizeof(reply), "Mensagem longa demais: até %d bytes.", room > 0 ? room : 0);
        say(reply);
        return;
    }
    flatten(line, size_t(size));
    // Dated by the bridge's clock, unique to the second as MeshCore wants it.
    const uint32_t seconds = std::max<uint32_t>(uint32_t(utc() / 1000), runtime->directSeconds + 1);
    if (!host->sendDirectText(contact.id, seconds, line, size_t(size))) {
        say("Não foi possível enviar agora; tente de novo. / Could not send now; try again.");
        return;
    }
    runtime->directSeconds = seconds;
    session.directs.admit(now, 2, 20000);
    runtime->directs.admit(now, 5, 180000);
    // Remembered, so the contact's answer comes back to this phone.
    auto *slot = &runtime->threads[0];
    for (auto &thread : runtime->threads) {
        if (thread.used && thread.phone == session.peer && std::equal(thread.contact.begin(), thread.contact.end(), contact.id)) {
            slot = &thread;
            break;
        }
        if (slot->used && (!thread.used || int32_t(thread.at - slot->at) < 0))
            slot = &thread;
    }
    slot->phone = session.peer;
    std::copy_n(contact.id, slot->contact.size(), slot->contact.begin());
    slot->at = now;
    slot->used = true;
    snprintf(reply, sizeof(reply), "Enviado a %s pelo LoRa.", contact.name);
    say(reply);
}

void BitchatBridge::directText(const uint8_t id[8], const char *name, const char *text, size_t size)
{
    // Only the owner's favourites reach Bitchat.
    if (!runtime || !dmEnabled || !identityReady || !text || !size || !host->favouriteContact(id))
        return;
    const uint32_t now = millis();
    size = strnlen(text, size);
    Bytes body{reinterpret_cast<const uint8_t *>(text), size};
    // A resend of the same message (its ack was lost) is passed on once.
    const uint32_t hash = textHash(body);
    for (const auto &recent : runtime->recentDirects)
        if (recent.at && recent.hash == hash && std::equal(recent.from.begin(), recent.from.end(), id) &&
            Throttle::isWithinTimespanMsAt(recent.at, 120000, now))
            return;
    PeerId phone{};
    bool target = false;
    // "@ana ..." names a phone here.
    if (body.size > 1 && body.data[0] == '@') {
        size_t consumed = 0;
        if (Peer *peer = namedPhone({body.data + 1, body.size - 1}, consumed)) {
            phone = peer->id;
            target = true;
            body = trimmedStart({body.data + 1 + consumed, body.size - 1 - consumed});
        }
    }
    // Otherwise the phone that wrote to this contact last, within half an hour.
    if (!target) {
        const Runtime::DirectThread *latest = nullptr;
        for (const auto &thread : runtime->threads)
            if (thread.used && std::equal(thread.contact.begin(), thread.contact.end(), id) &&
                Throttle::isWithinTimespanMsAt(thread.at, THREAD_MS, now) && (!latest || int32_t(thread.at - latest->at) > 0))
                latest = &thread;
        if (latest) {
            phone = latest->phone;
            target = true;
        }
    }
    if (!target || !body.size || !validUtf8(body))
        return;
    auto &recent = runtime->recentDirects[runtime->nextRecentDirect];
    runtime->nextRecentDirect = uint8_t((runtime->nextRecentDirect + 1) % runtime->recentDirects.size());
    std::copy_n(id, recent.from.size(), recent.from.begin());
    recent.hash = hash;
    recent.at = now ? now : 1;
    char line[256];
    char sender[33];
    snprintf(sender, sizeof(sender), "%s", name && name[0] ? name : "?");
    const int length = snprintf(line, sizeof(line), FROM_LORA, sender, int(body.size), reinterpret_cast<const char *>(body.data));
    if (length < 0)
        return;
    // Cut on a character boundary, the cut position itself being the first byte left out.
    size_t cut = std::min<size_t>(size_t(length), sizeof(line) - 1);
    while (cut && cut < size_t(length) && (uint8_t(line[cut]) & 0xc0) == 0x80)
        --cut;
    flatten(line, cut);
    // Kept until the phone says it got it, the oldest of that phone or that sender giving way when full.
    if (!allocateBuffer(runtime->mailbox))
        return;
    size_t forPhone = 0, fromSender = 0;
    Mailbox::Item *oldestPhone = nullptr, *oldestSender = nullptr, *oldest = nullptr, *free = nullptr;
    for (auto &item : runtime->mailbox->items) {
        if (item.used && !Throttle::isWithinTimespanMsAt(item.at, MAIL_MS, now)) {
            noiseWipe(&item, sizeof(item));
            item = {};
        }
        if (!item.used) {
            if (!free)
                free = &item;
            continue;
        }
        if (item.phone == phone) {
            ++forPhone;
            if (!oldestPhone || int32_t(item.at - oldestPhone->at) < 0)
                oldestPhone = &item;
        }
        if (std::equal(item.from.begin(), item.from.end(), id)) {
            ++fromSender;
            if (!oldestSender || int32_t(item.at - oldestSender->at) < 0)
                oldestSender = &item;
        }
        if (!oldest || int32_t(item.at - oldest->at) < 0)
            oldest = &item;
    }
    Mailbox::Item *slot = forPhone >= MAIL_PER_PHONE      ? oldestPhone
                          : fromSender >= MAIL_PER_SENDER ? oldestSender
                          : free                          ? free
                                                          : oldest;
    noiseWipe(slot, sizeof(*slot));
    *slot = {};
    slot->phone = phone;
    std::copy_n(id, slot->from.size(), slot->from.begin());
    slot->at = now;
    slot->size = uint8_t(cut);
    std::memcpy(slot->text, line, cut);
    slot->used = true;
    // Straight away when the phone's chat is open and the phone is on its link now; else when it comes back.
    if (PrivateSession *session = privateSession(phone, now, false))
        if (session->ready) {
            deliverMail(*session, now);
            return;
        }
    offerMail(phone, now);
}

void BitchatBridge::deliverMail(PrivateSession &session, uint32_t now)
{
    if (!runtime || !runtime->mailbox || !session.ready)
        return;
    if (!dmEnabled) {
        noiseWipe(runtime->mailbox.get(), sizeof(Mailbox));
        runtime->mailbox.reset();
        return;
    }
    // Oldest first; each one sent only to the phone's own link, and again later if it is not acknowledged.
    for (;;) {
        Mailbox::Item *next = nullptr;
        for (auto &item : runtime->mailbox->items) {
            if (item.used && !Throttle::isWithinTimespanMsAt(item.at, MAIL_MS, now)) {
                noiseWipe(&item, sizeof(item));
                item = {};
            }
            if (item.used && item.phone == session.peer &&
                (!item.sentAt || !Throttle::isWithinTimespanMsAt(item.sentAt, RESEND_MS, now)) &&
                (!next || int32_t(item.at - next->at) < 0))
                next = &item;
        }
        if (!next || !sendPrivateText(session, next->text, next->size, next->id, true))
            return;
        next->sentAt = now ? now : 1;
    }
}

void BitchatBridge::mailAcknowledged(const PeerId &phone, Bytes id)
{
    if (!runtime || !runtime->mailbox || id.size != 36)
        return;
    for (auto &item : runtime->mailbox->items)
        if (item.used && item.sentAt && item.phone == phone && std::memcmp(item.id, id.data, 36) == 0) {
            noiseWipe(&item, sizeof(item));
            item = {};
        }
}

void BitchatBridge::offerMail(const PeerId &phone, uint32_t now)
{
    if (!runtime || !runtime->mailbox || !dmEnabled)
        return;
    const bool waiting = std::any_of(runtime->mailbox->items.begin(), runtime->mailbox->items.end(),
                                     [&](const Mailbox::Item &item) { return item.used && item.phone == phone; });
    if (!waiting)
        return;
    // Only a phone here, that announced itself to this radio a moment ago.
    const Peer *peer = runtime->peers->find(phone);
    if (!peer || peer->remote || peer->announcement.gateway || !peer->localAt ||
        !Throttle::isWithinTimespanMsAt(peer->localAt, HERE_MS, now))
        return;
    PrivateSession *session = privateSession(phone, now, true);
    if (!session)
        return;
    if (session->ready)
        deliverMail(*session, now);
    else
        initiatePrivate(*session, now);
}
#endif

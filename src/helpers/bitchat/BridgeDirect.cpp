// Direct messages between a Bitchat phone and a LoRa contact of the radio, through the private chat with the bridge.
// "@Ana te encontro às 18h" in that chat goes to the contact Ana as a direct message of this radio, marked with the
// phone's name; Ana's answer comes back to the same chat. The bridge reads both: it is not end to end.
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
static constexpr uint32_t THREAD_MS = 24UL * 3600 * 1000;
static constexpr uint32_t MAIL_MS = 24UL * 3600 * 1000;

static Bytes trimmedStart(Bytes text)
{
    while (text.size && (text.data[0] == ' ' || text.data[0] == ':')) {
        ++text.data;
        --text.size;
    }
    return text;
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
    return n;
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
    const Bytes rest{text.data + 1, text.size - 1};
    MeshHost::LoraContact contact{};
    size_t consumed = 0;
    bool ambiguous = false;
    const bool found = host->findContact(reinterpret_cast<const char *>(rest.data), rest.size, contact, consumed, ambiguous);
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
    if (!found || ambiguous) {
        char word[33];
        firstWord(rest, word, sizeof(word));
        snprintf(reply, sizeof(reply),
                 ambiguous ? "Há mais de um contato \"%s\"; mensagem não enviada."
                           : "Não conheço \"%s\" entre os contatos deste rádio. Escreva @Nome texto, com o nome do contato.",
                 found ? contact.name : word);
        say(reply);
        return;
    }
    contact.name[sizeof(contact.name) - 1] = 0;
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
        const int room = int(limit) - int(std::strlen(label)) - 10;
        snprintf(reply, sizeof(reply), "Mensagem longa demais: até %d caracteres.", room > 0 ? room : 0);
        say(reply);
        return;
    }
    // Two per phone, then one every 20 s; for all phones together, five, then one every three minutes.
    if (!session.directs.admit(now, 2, 20000) || !runtime->directs.admit(now, 5, 180000)) {
        say("Aguarde um pouco: limite de mensagens pelo LoRa. / Please wait: LoRa message limit.");
        return;
    }
    if (!host->sendDirectText(contact.id, line, size_t(size))) {
        say("Não foi possível enviar agora; tente de novo. / Could not send now; try again.");
        return;
    }
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
    if (!runtime || !dmEnabled || !identityReady || !text || !size)
        return;
    const uint32_t now = millis();
    size = strnlen(text, size);
    Bytes body{reinterpret_cast<const uint8_t *>(text), size};
    PeerId phone{};
    bool target = false;
    // "@ana ..." names the phone.
    if (body.size > 1 && body.data[0] == '@') {
        size_t consumed = 0;
        if (Peer *peer = runtime->peers->named({body.data + 1, body.size - 1}, consumed)) {
            phone = peer->id;
            target = true;
            body = trimmedStart({body.data + 1 + consumed, body.size - 1 - consumed});
        }
    }
    // Otherwise the phone that wrote to this contact last, within a day.
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
    char line[256];
    char sender[33];
    snprintf(sender, sizeof(sender), "%s", name && name[0] ? name : "?");
    int length = snprintf(line, sizeof(line), FROM_LORA, sender, int(body.size), reinterpret_cast<const char *>(body.data));
    if (length < 0)
        return;
    size_t cut = std::min<size_t>(size_t(length), sizeof(line) - 1);
    while (cut && cut < size_t(length) && (uint8_t(line[cut]) & 0xc0) == 0x80)
        --cut;
    // Straight to the phone when its chat is open...
    PrivateSession *session = privateSession(phone, now, false);
    if (session && session->ready && sendPrivateText(*session, line, cut))
        return;
    // ...else kept for when it comes back, the oldest giving way.
    if (!allocateBuffer(runtime->mailbox))
        return;
    auto *slot = &runtime->mailbox->items[0];
    for (auto &item : runtime->mailbox->items)
        if (slot->used && (!item.used || int32_t(item.at - slot->at) < 0))
            slot = &item;
    slot->phone = phone;
    slot->at = now;
    slot->size = uint8_t(cut);
    std::memcpy(slot->text, line, cut);
    slot->used = true;
    // A phone in reach right now gets a handshake from the bridge.
    if (runtime->peers->find(phone))
        offerMail(phone, now);
}

void BitchatBridge::deliverMail(PrivateSession &session, uint32_t now)
{
    if (!runtime || !runtime->mailbox || !session.ready)
        return;
    // Oldest first.
    for (;;) {
        Mailbox::Item *next = nullptr;
        for (auto &item : runtime->mailbox->items) {
            if (item.used && !Throttle::isWithinTimespanMsAt(item.at, MAIL_MS, now)) {
                noiseWipe(item.text, sizeof(item.text));
                item = {};
            }
            if (item.used && item.phone == session.peer && (!next || int32_t(item.at - next->at) < 0))
                next = &item;
        }
        if (!next || !sendPrivateText(session, next->text, next->size))
            return;
        noiseWipe(next->text, sizeof(next->text));
        *next = {};
    }
}

void BitchatBridge::offerMail(const PeerId &phone, uint32_t now)
{
    if (!runtime || !runtime->mailbox)
        return;
    const bool waiting = std::any_of(runtime->mailbox->items.begin(), runtime->mailbox->items.end(),
                                     [&](const Mailbox::Item &item) { return item.used && item.phone == phone; });
    if (!waiting)
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

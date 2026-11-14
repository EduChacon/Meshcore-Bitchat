#pragma once

#ifdef WITH_BITCHAT_BRIDGE
#include "BridgeCodec.h"
#include <cstdint>

class NimBLEServer;
namespace bitchat
{
constexpr uint16_t NO_LINK = 0xffff;
struct BleInput {
    Frame frame;
    uint16_t link = NO_LINK;
};
void bleAttach(NimBLEServer *server);
void bleDetach();
size_t bleBufferSize();
bool bleAllocateBuffers();
void bleReleaseBuffers();
void bleConnect(uint16_t handle);
void bleDisconnect(uint16_t handle);
void bleConfigure(bool enabled, bool nativeWindow, bool economy);
// The scan response has 11 bytes left beside the companion service; a longer name is cut and sent as a short name.
void bleName(const char *name);
bool bleAdvertise();
bool bleReceive(BleInput &output);
bool bleBroadcast(Bytes frame, uint16_t exclude = NO_LINK);
void blePoll();
size_t bleSubscribers();
size_t bleListeners();
size_t blePhones();
size_t bleConnections();
size_t bleDescribe(char *out, size_t size);
void bleServicesChanged();
void bleDropConnections();
uint32_t bleDrops();
bool bleNeedsAnnouncement();
} // namespace bitchat
#endif

#pragma once
#ifdef WITH_BITCHAT_BRIDGE
// The bridge asks one object whether Bluetooth is up and to redo its advertising. The companion's
// Bluetooth interface owns the stack and fills this in.
namespace bitchat
{
struct PlatformBluetooth {
    bool startupFailed = false, up = false;
    bool isActive() const { return up; }
    void refreshBridgeAdvertising();
};
extern PlatformBluetooth platformBluetooth;
} // namespace bitchat

#if defined(NRF52_PLATFORM)
#include <bluefruit.h>
namespace bitchat
{
// Implemented by the companion's Bluetooth interface, which owns its own service and advertisement.
void nativeAdvertising();
void nativeScanResponse();
// Adds the bridge's service and takes the stack's event callback; `chained` still sees every event.
void platformAttach(void (*chained)(ble_evt_t *));
// The main task's stack is too small for the bridge here. The work runs in a task with a larger one
// while the main task waits, so nothing runs at the same time.
bool onLargeStack(void (*work)(void *), void *argument);
} // namespace bitchat
#else
namespace bitchat
{
inline bool onLargeStack(void (*work)(void *), void *argument)
{
    work(argument);
    return true;
}
} // namespace bitchat
#endif
#endif

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
#endif

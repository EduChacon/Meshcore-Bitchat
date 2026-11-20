#pragma once
// MeshCore-Bitchat: in bridge builds WadaMesh's companion Bluetooth is the bridge's NimBLE interface, which
// shares the radio's Bluetooth with the Bitchat phones. tools/bitchat/build_wadamesh.py drops the core's own
// helpers/esp32/SerialBLEInterface.cpp from the build; this header takes the place of the core's.
#ifdef WITH_BITCHAT_BRIDGE
#include "../bitchat/esp32/SerialBLEInterface.h"
#else
#error "This WadaMesh tree was prepared for the Bitchat bridge: build the _bitchat environment"
#endif

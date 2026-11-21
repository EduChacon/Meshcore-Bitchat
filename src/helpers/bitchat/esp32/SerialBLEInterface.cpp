#ifdef WITH_BITCHAT_BRIDGE
#include "SerialBLEInterface.h"
#include "../BridgeBle.h"
#include "../BridgePlatform.h"
#include "esp_mac.h"

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis
#define NO_CONN  0xFFFF

static SerialBLEInterface* instance = NULL;
static volatile unsigned long adv_restart_time = 0;

namespace bitchat {
PlatformBluetooth platformBluetooth;
void PlatformBluetooth::refreshBridgeAdvertising() {
  if (instance) instance->advertise();
}
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;
  instance = this;
  // A second begin() comes after deinit() deleted the old server (WadaMesh restarts the stack): nothing
  // may point at it if this start fails.
  pServer = NULL;
  pTxCharacteristic = pRxCharacteristic = NULL;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  snprintf(dev_name, sizeof(dev_name), "%s%s", prefix, name);
  bitchat::bleName(dev_name);

  // A partial controller start needs a reboot, not a second attempt.
  bitchat::platformBluetooth.startupFailed = true;
  NimBLEDevice::init(dev_name);
  if (!NimBLEDevice::getInitialized()) {
    BLE_DEBUG_PRINTLN("ERROR: Bluetooth stack did not start");
    return;
  }
  bitchat::platformBluetooth.startupFailed = false;
  NimBLEDevice::setMTU(517);
#ifdef BITCHAT_BLE_POWER_MAX
  // boards with a small antenna (the Cardputer ADV) keep the phone link better at +9 dBm, the most
  // the ESP32-S3 allows, than at the default
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
#endif

  // Same pairing as the stock interface: bonded, with the PIN typed on the phone.
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(pin_code);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this, false);
  pServer->advertiseOnDisconnect(false);

  NimBLEService* service = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = service->createCharacteristic(CHARACTERISTIC_UUID_TX,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY);
  pRxCharacteristic = service->createCharacteristic(CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  pRxCharacteristic->setCallbacks(this);
  service->start();

  // The Bitchat service is open: its packets are signed and public, and the app does not pair.
  bitchat::bleAttach(pServer);
}

void SerialBLEInterface::setPin(uint32_t pin_code) {
  _pin_code = pin_code;
  if (pServer) NimBLEDevice::setSecurityPasskey(pin_code);
}

// -------- NimBLEServerCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pin) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pin);
  return true;
}

void SerialBLEInterface::onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
  BLE_DEBUG_PRINTLN("onConnect(), conn=%d", desc->conn_handle);
  // A phone that only runs Bitchat never pairs, so a link counts for the companion app once it is paired with
  // the PIN (see onAuthenticationComplete).
  bitchat::bleConnect(desc->conn_handle);
}

void SerialBLEInterface::onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
  BLE_DEBUG_PRINTLN("onDisconnect(), conn=%d", desc->conn_handle);
  bitchat::bleDisconnect(desc->conn_handle);
  if (desc->conn_handle == app_conn) {
    app_conn = NO_CONN;
    deviceConnected = false;
    app_addr_valid = false;
  }
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// The companion app pairs with the PIN: such a link is authenticated, not only encrypted. NimBLE also lets a
// device without a keyboard or screen pair with no PIN ("Just Works"): encrypted, not authenticated. That one
// must never become the app's link (it would receive the app's data, channel keys among it).
static bool pinPaired(uint16_t conn) {
  struct ble_gap_conn_desc desc;
  return conn != NO_CONN && ble_gap_conn_find(conn, &desc) == 0 && desc.sec_state.encrypted &&
         desc.sec_state.authenticated;
}

void SerialBLEInterface::onAuthenticationComplete(ble_gap_conn_desc* desc) {
  if (desc->sec_state.encrypted && desc->sec_state.authenticated) {
    BLE_DEBUG_PRINTLN(" - Authentication Success, conn=%d", desc->conn_handle);
    if (!deviceConnected) {
      app_conn = desc->conn_handle;
      deviceConnected = true;
      memcpy(app_addr, desc->peer_ota_addr.val, 6);
      app_addr_valid = true;
    }
  } else {
    BLE_DEBUG_PRINTLN(" - Authentication Failure*");
    // A bond made without the PIN is not kept: the store holds few, and would push out the app's.
    if (desc->sec_state.encrypted) NimBLEDevice::deleteBond(NimBLEAddress(desc->peer_id_addr));
    pServer->disconnect(desc->conn_handle);
  }
}

// -------- NimBLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(NimBLECharacteristic* characteristic, ble_gap_conn_desc* desc) {
  // the stack already refuses these (the PIN is required to write); never trust another link here
  if (!desc->sec_state.encrypted || !desc->sec_state.authenticated) return;

  // the link that writes is the one the companion app is on
  app_conn = desc->conn_handle;
  deviceConnected = true;

  auto value = characteristic->getValue();
  int len = value.length();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else {
    Frame frame = {};
    frame.len = len;
    memcpy(frame.buf, value.data(), len);

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
  }
}

// ---------- public methods

void SerialBLEInterface::clearBuffers() {
  xQueueReset(recv_queue);
  send_queue_len = 0;
}

void SerialBLEInterface::advertise() {
  if (!_isEnabled || !pServer) return;

  // bridge running: its service goes in the advertisement and this one in the scan response
  if (bitchat::bleAdvertise()) return;

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->stop();
  advertising->reset();
  NimBLEAdvertisementData primary, response;
  primary.setFlags(0x06);
  primary.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  if (strlen(dev_name) > 29) {
    response.setShortName(std::string(dev_name, 29));
  } else {
    response.setName(dev_name);
  }
  advertising->setAdvertisementData(primary);
  advertising->setScanResponseData(response);
  advertising->setScanResponse(true);
  if (!deviceConnected) {   // as the stock interface: not discoverable while the app is connected
    advertising->start();
  }
}

void SerialBLEInterface::enable() {
  if (_isEnabled || !pServer) return;

  _isEnabled = true;
  clearBuffers();
  bitchat::platformBluetooth.up = true;
  adv_restart_time = 0;
  advertise();
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  bitchat::platformBluetooth.up = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");
  if (!pServer) return;

  NimBLEDevice::getAdvertising()->stop();
  for (uint16_t conn : pServer->getPeerDevices()) {
    pServer->disconnect(conn);
  }
  oldDeviceConnected = deviceConnected = false;
  app_conn = NO_CONN;
  adv_restart_time = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

void SerialBLEInterface::drainSendQueue() {
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    // Only the app's own link, paired with the PIN, receives this: never the Bitchat phones, nor a device that
    // paired without the PIN (notify() would send it to every subscribed, encrypted link).
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    const uint16_t conn = app_conn;
    if (pinPaired(conn)) {
      os_mbuf* om = ble_hs_mbuf_from_flat(send_queue[0].buf, send_queue[0].len);
      if (om) ble_gattc_notify_custom(conn, pTxCharacteristic->getHandle(), om);
    }

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }
}

bool SerialBLEInterface::getConnectedPeerAddress(char* buf, size_t len) const {
  if (!buf || len == 0) return false;
  buf[0] = '\0';
  if (!app_addr_valid || !deviceConnected || len < 18) return false;
  // NimBLE keeps the address little-endian; shown most significant byte first
  snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", app_addr[5], app_addr[4], app_addr[3], app_addr[2], app_addr[1],
           app_addr[0]);
  return true;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  drainSendQueue();

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  bool connected = deviceConnected;
  if (connected != oldDeviceConnected) {
    if (!connected) {    // disconnecting
      clearBuffers();
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");
      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      advertise();
    }
    oldDeviceConnected = connected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    adv_restart_time = 0;
    BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
    advertise();
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;
}

#endif

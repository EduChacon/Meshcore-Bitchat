#pragma once
#ifdef WITH_BITCHAT_BRIDGE

// Companion Bluetooth interface on the NimBLE stack. It replaces helpers/esp32/SerialBLEInterface in
// bridge builds: the same Nordic UART service and PIN pairing for the MeshCore app, plus the open
// Bitchat service on the same links.

#include <helpers/BaseSerialInterface.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class SerialBLEInterface : public BaseSerialInterface, NimBLEServerCallbacks, NimBLECharacteristicCallbacks {
  NimBLEServer *pServer;
  NimBLECharacteristic *pTxCharacteristic;
  NimBLECharacteristic *pRxCharacteristic;
  volatile bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  volatile uint16_t app_conn;
  uint8_t app_addr[6];
  bool app_addr_valid;
  uint32_t _pin_code;
  unsigned long _last_write;
  char dev_name[32 + 16];

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  StaticQueue_t recv_queue_state;
  uint8_t recv_queue_storage[FRAME_QUEUE_SIZE * sizeof(Frame)];
  QueueHandle_t recv_queue;
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();

protected:
  // NimBLEServerCallbacks methods
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override;
  void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) override;
  uint32_t onPassKeyRequest() override;
  bool onConfirmPIN(uint32_t pin) override;
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override;

  // NimBLECharacteristicCallbacks methods
  void onWrite(NimBLECharacteristic* characteristic, ble_gap_conn_desc* desc) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pTxCharacteristic = pRxCharacteristic = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    _isEnabled = false;
    _last_write = 0;
    app_conn = 0xFFFF;
    app_addr_valid = false;
    _pin_code = 0;
    dev_name[0] = 0;
    recv_queue = xQueueCreateStatic(
      FRAME_QUEUE_SIZE, sizeof(Frame), recv_queue_storage, &recv_queue_state
    );
    send_queue_len = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // Sets up the advertising again: the bridge's when it is running, the companion's own otherwise.
  void advertise();
  const char* deviceName() const { return dev_name; }
  // A new pairing PIN for links made from now on.
  void setPin(uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // For firmware that drives the interface itself (WadaMesh): the next queued frame to the app, when
  // its turn comes, and the address of the app's link.
  void drainSendQueue();
  bool getConnectedPeerAddress(char* buf, size_t len) const;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif

#endif

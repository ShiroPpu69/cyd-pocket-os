#include "codex_buddy_ble.h"

#include <NimBLEDevice.h>
#include <atomic>
#include <esp_heap_caps.h>

namespace {

constexpr char NUS_SERVICE_UUID[] =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char NUS_RX_UUID[] =
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char NUS_TX_UUID[] =
    "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr size_t RX_CAPACITY = 1024;

uint8_t rxBuffer[RX_CAPACITY];
volatile size_t rxHead = 0;
volatile size_t rxTail = 0;
volatile bool connected = false;
volatile bool secure = false;
volatile uint32_t passkey = 0;
std::atomic<bool> shuttingDown{false};
std::atomic<bool> restartAdvertising{false};
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
bool discardLine = false;
enum class BleLifecycle : uint8_t { STOPPED, STARTING, RUNNING, STOPPING };
std::atomic<BleLifecycle> lifecycle{BleLifecycle::STOPPED};
std::atomic<bool> desiredRunning{false};
uint32_t retryAt = 0;
std::atomic<uint32_t> lifecycleStartedAt{0};
char deviceName[16] = "Codex-CYD";
std::atomic<uint16_t> connectionHandle{BLE_HS_CONN_HANDLE_NONE};

void pushBytes(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    portENTER_CRITICAL(&rxMux);
    if (discardLine) {
      if (data[i] == '\n') discardLine = false;
      portEXIT_CRITICAL(&rxMux);
      continue;
    }
    const size_t next = (rxHead + 1) % RX_CAPACITY;
    if (next == rxTail) {
      // Reject an overlong packet as a whole, never parse its JSON suffix.
      rxHead = rxTail = 0;
      discardLine = data[i] != '\n';
      portEXIT_CRITICAL(&rxMux);
      continue;
    }
    rxBuffer[rxHead] = data[i];
    rxHead = next;
    portEXIT_CRITICAL(&rxMux);
  }
}

class RxCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    const auto value = characteristic->getValue();
    if (value.size() > 0) {
      pushBytes(reinterpret_cast<const uint8_t*>(value.c_str()),
                value.size());
    }
  }
};

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    connectionHandle = info.getConnHandle();
    connected = true;
    Serial.println("[buddy] BLE connected");
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    connectionHandle = BLE_HS_CONN_HANDLE_NONE;
    connected = false;
    secure = false;
    passkey = 0;
    if (!shuttingDown) restartAdvertising = true;
  }
  uint32_t onPassKeyDisplay() override {
    const uint32_t value = 100000 + esp_random() % 900000;
    passkey = value;
    Serial.printf("[buddy] pairing code %06lu\n",
                  static_cast<unsigned long>(value));
    return value;
  }
  void onConfirmPassKey(NimBLEConnInfo& info, uint32_t) override {
    NimBLEDevice::injectConfirmPasskey(info, false);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    secure = info.isEncrypted() && info.isAuthenticated();
    passkey = 0;
    Serial.printf("[buddy] authentication %s\n",
                  secure ? "complete" : "failed");
  }
};

RxCallbacks rxCallbacks;
ServerCallbacks serverCallbacks;

void stopBleNow() {
  shuttingDown = true;
  restartAdvertising = false;
  // Close the connection while server/service objects still exist. Do not
  // call advertising APIs from a disconnect callback during teardown.
  NimBLEDevice::stopAdvertising();
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server && connected) {
    server->disconnect(connectionHandle.load());
    const uint32_t started = millis();
    while (connected && millis() - started < 1000) vTaskDelay(pdMS_TO_TICKS(10));
  }
  // NimBLE owns all service children. clearAll also releases characteristics
  // and descriptors, unlike the old Bluedroid wrapper's non-owning maps.
  NimBLEDevice::deinit(true);
  connectionHandle = BLE_HS_CONN_HANDLE_NONE;
  connected = false;
  secure = false;
  passkey = 0;
  portENTER_CRITICAL(&rxMux);
  rxHead = rxTail = 0;
  discardLine = false;
  portEXIT_CRITICAL(&rxMux);
  Serial.println("[buddy] BLE stopped");
}

void startBleTask(void*) {
  shuttingDown = false;
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "Codex-%04X",
           static_cast<unsigned>((chipId >> 32) & 0xFFFF));
  if (!NimBLEDevice::init(deviceName)) {
    Serial.println("[buddy] BLE initialization failed");
    desiredRunning = false;
    lifecycle = BleLifecycle::STOPPED;
    vTaskDelete(nullptr);
    return;
  }
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(false);
  NimBLEService* service = server->createService(NUS_SERVICE_UUID);

  service->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* rx = service->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
          NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  rx->setCallbacks(&rxCallbacks);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(NUS_SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->setName(deviceName);
  advertising->setPreferredParams(0x06, 0x12);
  NimBLEDevice::startAdvertising();
  Serial.printf("[buddy] advertising as %s\n", deviceName);

  if (desiredRunning) {
    lifecycle = BleLifecycle::RUNNING;
  } else {
    lifecycle = BleLifecycle::STOPPING;
    stopBleNow();
    lifecycle = BleLifecycle::STOPPED;
  }
  vTaskDelete(nullptr);
}

void stopBleTask(void*) {
  stopBleNow();
  lifecycle = BleLifecycle::STOPPED;
  vTaskDelete(nullptr);
}

void scheduleStart() {
  if (lifecycle != BleLifecycle::STOPPED) return;
  if (static_cast<int32_t>(millis() - retryAt) < 0) return;
  if (ESP.getFreeHeap() < 60000 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16000) {
    retryAt = millis() + 10000;
    Serial.println("[buddy] BLE deferred: low heap");
    return;
  }
  lifecycle = BleLifecycle::STARTING;
  lifecycleStartedAt = millis();
  if (xTaskCreatePinnedToCore(startBleTask, "buddy-ble-start", 8192, nullptr,
                              1, nullptr, 0) != pdPASS) {
    retryAt = millis() + 10000;
    lifecycle = BleLifecycle::STOPPED;
    Serial.println("[buddy] BLE start task failed");
  }
}

void scheduleStop() {
  if (lifecycle != BleLifecycle::RUNNING) return;
  lifecycle = BleLifecycle::STOPPING;
  lifecycleStartedAt = millis();
  if (xTaskCreatePinnedToCore(stopBleTask, "buddy-ble-stop", 6144, nullptr,
                              1, nullptr, 0) != pdPASS) {
    lifecycle = BleLifecycle::RUNNING;
    Serial.println("[buddy] BLE stop task failed");
  }
}

}  // namespace

void codexBuddyBleBegin() {
  desiredRunning = true;
  scheduleStart();
}

void codexBuddyBleEnd() {
  desiredRunning = false;
  scheduleStop();
}

void codexBuddyBleLoop() {
  const BleLifecycle state = lifecycle.load();
  if ((state == BleLifecycle::STARTING || state == BleLifecycle::STOPPING) &&
      millis() - lifecycleStartedAt.load() > 20000) {
    // A wedged vendor stack cannot safely be force-deleted while holding locks.
    Serial.println("[buddy] BLE lifecycle timeout; restarting safely");
    ESP.restart();
  }
  if (restartAdvertising.exchange(false) && state == BleLifecycle::RUNNING && desiredRunning) {
    NimBLEDevice::startAdvertising();
  }
  if (desiredRunning && lifecycle == BleLifecycle::STOPPED) {
    scheduleStart();
  } else if (!desiredRunning && lifecycle == BleLifecycle::RUNNING) {
    scheduleStop();
  }
}

bool codexBuddyBleConnected() { return connected; }
bool codexBuddyBleSecure() { return secure; }
bool codexBuddyBleIdle() { return lifecycle == BleLifecycle::STOPPED; }
uint32_t codexBuddyBlePasskey() { return passkey; }
const char* codexBuddyBleDeviceName() { return deviceName; }

bool codexBuddyBleTakeLine(char* destination, size_t capacity) {
  if (destination == nullptr || capacity < 2) return false;

  portENTER_CRITICAL(&rxMux);
  size_t cursor = rxTail;
  bool newlineFound = false;
  while (cursor != rxHead) {
    if (rxBuffer[cursor] == '\n') {
      newlineFound = true;
      break;
    }
    cursor = (cursor + 1) % RX_CAPACITY;
  }
  if (!newlineFound) {
    portEXIT_CRITICAL(&rxMux);
    return false;
  }

  size_t written = 0;
  bool tooLong = false;
  while (rxTail != rxHead) {
    const char c = static_cast<char>(rxBuffer[rxTail]);
    rxTail = (rxTail + 1) % RX_CAPACITY;
    if (c == '\n') break;
    if (c != '\r') {
      if (written + 1 < capacity) destination[written++] = c;
      else tooLong = true;
    }
  }
  destination[written] = '\0';
  portEXIT_CRITICAL(&rxMux);
  return written > 0 && !tooLong;
}

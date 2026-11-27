#include "ble.h"

#include <NimBLEDevice.h>

#include <string.h>

#include "cpr_json.h"

namespace {

// Nordic UART Service (NUS) UUIDs for Web Bluetooth compatibility.
constexpr const char* kServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kRxCharUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kTxCharUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// Engineering values, tunable: BLE MTU target and line buffers.
constexpr uint16_t kBlePreferredMtu = 256;
constexpr uint16_t kBleDefaultMtu = 23;
constexpr size_t kOutLineLen = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdLineLen = 64;

NimBLEServer* g_server = nullptr;
NimBLECharacteristic* g_txChar = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

portMUX_TYPE g_cfgMux = portMUX_INITIALIZER_UNLOCKED;
char g_cfgLine[kOutLineLen];

volatile bool g_connected = false;
volatile bool g_subscribed = false;
volatile bool g_needConfig = false;
volatile bool g_advPaused = false;
volatile uint16_t g_mtu = kBleDefaultMtu;

// Static RX line reassembly buffer (no heap allocation).
char g_rxBuf[kCmdLineLen];
size_t g_rxLen = 0;

void submitLine(const char* text, size_t len) {
  if (g_cmdQueue == nullptr || len == 0) return;

  size_t count = len;
  if (count > kCmdLineLen - 1) count = kCmdLineLen - 1;

  char line[kCmdLineLen];
  memcpy(line, text, count);
  line[count] = '\0';
  xQueueSend(g_cmdQueue, line, 0);
}

void startAdvIfAllowed() {
  if (g_advPaused || g_connected) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv != nullptr && !adv->isAdvertising()) {
    adv->start();
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    (void)server;
    g_connected = true;
    g_subscribed = false;
    g_needConfig = false;
    g_rxLen = 0;
    const uint16_t mtu = connInfo.getMTU();
    g_mtu = (mtu >= kBleDefaultMtu) ? mtu : kBleDefaultMtu;
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo,
                    int reason) override {
    (void)server;
    (void)connInfo;
    (void)reason;
    g_connected = false;
    g_subscribed = false;
    g_needConfig = false;
    g_rxLen = 0;
    g_mtu = kBleDefaultMtu;
    startAdvIfAllowed();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    if (mtu >= kBleDefaultMtu) {
      g_mtu = mtu;
    }
  }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    (void)chr;
    (void)connInfo;
    const bool sub = (subValue != 0);
    g_subscribed = sub;
    if (sub) {
      g_needConfig = true;
    }
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    const NimBLEAttValue val = chr->getValue();
    const uint8_t* data = val.data();
    const size_t len = val.size();
    if (data == nullptr || len == 0) return;

    bool sawNewline = false;
    for (size_t i = 0; i < len; ++i) {
      const char c = (char)data[i];
      if (c == '\n' || c == '\r') {
        sawNewline = true;
        if (g_rxLen > 0) {
          submitLine(g_rxBuf, g_rxLen);
          g_rxLen = 0;
        }
        continue;
      }
      if (c < 0x20 || c > 0x7E) {
        continue;
      }
      if (g_rxLen < sizeof(g_rxBuf) - 1) {
        g_rxBuf[g_rxLen++] = c;
      }
    }

    // Allow single-write commands without trailing newline when the write fits
    // within a single packet and does not look like a chunked prefix.
    if (!sawNewline && g_rxLen > 0 && len < 20) {
      submitLine(g_rxBuf, g_rxLen);
      g_rxLen = 0;
    }
  }
};

ServerCallbacks g_serverCb;
TxCallbacks g_txCb;
RxCallbacks g_rxCb;

} // namespace

namespace ble {

void begin(const char* deviceName, QueueHandle_t cmdQueue) {
  g_cmdQueue = cmdQueue;
  g_cfgLine[0] = '\0';
  g_connected = false;
  g_subscribed = false;
  g_needConfig = false;
  g_advPaused = false;
  g_mtu = kBleDefaultMtu;
  g_rxLen = 0;

  const char* name = (deviceName != nullptr && deviceName[0] != '\0')
                         ? deviceName
                         : "CPR-Trainer";

  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(kBlePreferredMtu);

  g_server = NimBLEDevice::createServer();
  if (g_server == nullptr) {
    Serial.println("# BLE server init failed");
    return;
  }
  g_server->setCallbacks(&g_serverCb);

  NimBLEService* service = g_server->createService(kServiceUuid);
  if (service == nullptr) {
    Serial.println("# BLE service init failed");
    return;
  }

  g_txChar = service->createCharacteristic(kTxCharUuid, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rxChar = service->createCharacteristic(
      kRxCharUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  if (g_txChar == nullptr || rxChar == nullptr) {
    Serial.println("# BLE characteristic init failed");
    return;
  }

  g_txChar->setCallbacks(&g_txCb);
  rxChar->setCallbacks(&g_rxCb);
  g_server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv != nullptr) {
    adv->addServiceUUID(kServiceUuid);
    adv->setName(name);
    adv->enableScanResponse(true);
    adv->start();
  }
}

void loop() {
  if (!g_connected || !g_subscribed || !g_needConfig) return;
  g_needConfig = false;

  char copy[kOutLineLen];
  portENTER_CRITICAL(&g_cfgMux);
  strncpy(copy, g_cfgLine, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';
  portEXIT_CRITICAL(&g_cfgMux);

  if (copy[0] != '\0') {
    notifyLine(copy);
  }
}

void setConfigLine(const char* line) {
  portENTER_CRITICAL(&g_cfgMux);
  strncpy(g_cfgLine, line != nullptr ? line : "", sizeof(g_cfgLine) - 1);
  g_cfgLine[sizeof(g_cfgLine) - 1] = '\0';
  portEXIT_CRITICAL(&g_cfgMux);
}

bool notifyLine(const char* line) {
  if (!g_connected || !g_subscribed || g_txChar == nullptr || line == nullptr) {
    return false;
  }

  const size_t len = strnlen(line, kOutLineLen - 2);
  if (len == 0) return false;

  uint8_t buf[kOutLineLen];
  memcpy(buf, line, len);
  buf[len] = '\n';
  const size_t total = len + 1;

  uint16_t mtu = g_mtu;
  if (mtu < kBleDefaultMtu) mtu = kBleDefaultMtu;
  size_t maxChunk = (size_t)(mtu - 3);
  if (maxChunk < 20) maxChunk = 20;
  if (maxChunk > 244) maxChunk = 244;

  size_t offset = 0;
  while (offset < total) {
    if (!g_connected || !g_subscribed) return false;
    size_t chunk = total - offset;
    if (chunk > maxChunk) chunk = maxChunk;
    g_txChar->setValue(buf + offset, chunk);
    if (!g_txChar->notify()) {
      return false;
    }
    offset += chunk;
  }
  return true;
}

bool isConnected() { return g_connected; }

bool isSubscribed() { return g_connected && g_subscribed; }

void pauseAdvertising() {
  g_advPaused = true;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv != nullptr && adv->isAdvertising()) {
    adv->stop();
  }
}

void resumeAdvertising() {
  g_advPaused = false;
  startAdvIfAllowed();
}

} // namespace ble

#include "net.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "ble.h"
#include "cpr_command.h"
#include "cpr_json.h"
#include "pins.h"
#include "storage.h"
#include "web_assets.h"

#ifndef CPR_CAPTIVE_DNS
#define CPR_CAPTIVE_DNS 1
#endif

#ifndef CPR_SERIAL_STREAM_DEFAULT
#define CPR_SERIAL_STREAM_DEFAULT 1
#endif

namespace {

enum class ConnMode : uint8_t { Idle = 0, Wifi = 1, Usb = 2, Ble = 3 };
enum class WifiState : uint8_t { Off = 0, StaConnecting = 1, StaConnected = 2, Ap = 3 };

constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kApChannel = 6;
constexpr uint8_t kMaxStations = 4;
constexpr uint16_t kMaxWsClients = 4;
constexpr size_t kOutLineLen = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdLineLen = 128;
constexpr const char* kDefaultWifiPass = "cprtrain123";
constexpr const char* kMdnsHost = "cpr-trainer";
constexpr uint32_t kFramePeriodMs = 50;
constexpr uint32_t kDiagPeriodMs = 5000;
constexpr uint32_t kRxWindowMs = 1000;
constexpr uint16_t kRxLinesPerWindow = 20;

// Engineering values, tunable: USB lease, STA timeouts, and status LED timings.
constexpr uint32_t kUsbWebTimeoutMs = 10000;
constexpr uint32_t kStaConnectTimeoutMs = 12000;
constexpr uint32_t kStaDropFallbackMs = 15000;
constexpr uint32_t kStaApplyDelayMs = 300;
constexpr uint32_t kLedIdleHalfPeriodMs = 500;
constexpr uint32_t kLedWifiPeriodMs = 3000;
constexpr uint32_t kLedWifiLowMs = 500;
constexpr uint32_t kLedBlePeriodMs = 2000;

AsyncWebServer g_server(kHttpPort);
AsyncWebSocket g_ws("/ws");
DNSServer g_dns;

QueueHandle_t g_outQueue = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

char g_configLine[kOutLineLen];
portMUX_TYPE g_configMux = portMUX_INITIALIZER_UNLOCKED;

char g_frameLine[kOutLineLen];
portMUX_TYPE g_frameMux = portMUX_INITIALIZER_UNLOCKED;
bool g_frameFresh = false;

bool g_serialStreaming = (CPR_SERIAL_STREAM_DEFAULT != 0);
ConnMode g_connMode = ConnMode::Idle;
WifiState g_wifiState = WifiState::Off;
bool g_wifiEnabled = false;
bool g_mdnsStarted = false;
uint32_t g_usbWebLastMs = 0;
uint32_t g_staStartMs = 0;
uint32_t g_staDropMs = 0;
int8_t g_ledState = -1;
char g_ssid[24];
char g_wifiPass[64];

portMUX_TYPE g_staMux = portMUX_INITIALIZER_UNLOCKED;
bool g_hasStaConfig = false;
char g_staSsid[33];
char g_staPass[64];
char g_staIp[20];
bool g_staActionPending = false;
uint32_t g_staActionAtMs = 0;

// Each counter has a single writer task; volatile is enough for a snapshot.
volatile uint32_t g_rxCount = 0;
volatile uint32_t g_rxBad = 0;
volatile uint32_t g_qDrop = 0;
volatile uint32_t g_sSkip = 0;
volatile uint32_t g_fDrop = 0;
volatile uint8_t g_diagSps = 0;
volatile bool g_diagHx = false;
portMUX_TYPE g_diagMux = portMUX_INITIALIZER_UNLOCKED;
long g_diagRawAdc = 0;
long g_diagOffset = 0;
float g_diagScale = 1.0f;
uint16_t g_diagHxReboots = 0;
uint8_t g_diagHxStatus = 1;

uint32_t g_rxWindowStart = 0;
uint16_t g_rxWindowCount = 0;

const WebAsset* findAsset(const char* path) {
  for (size_t i = 0; i < kWebAssetCount; ++i) {
    if (strcmp(kWebAssets[i].path, path) == 0) return &kWebAssets[i];
  }
  return nullptr;
}

void sendAsset(AsyncWebServerRequest* request, const WebAsset* asset) {
  AsyncWebServerResponse* response =
      request->beginResponse(200, asset->mime, asset->data, asset->len);
  if (response == nullptr) {
    request->send(500, "text/plain", "asset error");
    return;
  }

  response->addHeader("Content-Encoding", "gzip");
  if (strcmp(asset->path, "/index.html") == 0) {
    response->addHeader("Cache-Control", "no-cache");
  } else {
    response->addHeader("Cache-Control", "max-age=86400");
  }
  request->send(response);
}

void onRequest(AsyncWebServerRequest* request) {
  const char* path = request->url().c_str();

  if (strcmp(path, "/generate_204") == 0 ||
      strcmp(path, "/hotspot-detect.html") == 0 ||
      strcmp(path, "/connecttest.txt") == 0) {
    request->redirect("/");
    return;
  }

  const WebAsset* asset = nullptr;
  if (strcmp(path, "/") == 0) {
    asset = findAsset("/index.html");
  } else {
    asset = findAsset(path);
  }
  if (asset == nullptr) {
    asset = findAsset("/index.html");
  }

  if (asset == nullptr) {
    request->send(503, "text/plain", "no web assets");
    return;
  }
  sendAsset(request, asset);
}

void submitCommandLines(const char* text, size_t len) {
  if (g_cmdQueue == nullptr) return;

  size_t start = 0;
  for (size_t i = 0; i <= len; ++i) {
    if (i != len && text[i] != '\n' && text[i] != '\r') continue;

    size_t count = i - start;
    if (count > 0) {
      char line[kCmdLineLen];
      if (count > kCmdLineLen - 1) count = kCmdLineLen - 1;
      memcpy(line, text + start, count);
      line[count] = '\0';
      xQueueSend(g_cmdQueue, line, 0);
    }
    start = i + 1;
  }
}

void sendConfigTo(AsyncWebSocketClient* client) {
  char copy[kOutLineLen];
  portENTER_CRITICAL(&g_configMux);
  strncpy(copy, g_configLine, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';
  portEXIT_CRITICAL(&g_configMux);

  if (copy[0] != '\0') {
    g_ws.text(client->id(), copy);
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  (void)server;

  if (type == WS_EVT_CONNECT) {
    sendConfigTo(client);
    return;
  }
  if (type != WS_EVT_DATA || arg == nullptr) return;

  const AwsFrameInfo* info = static_cast<const AwsFrameInfo*>(arg);
  if (info->message_opcode != WS_TEXT) return;

  static char message[kCmdLineLen];
  static size_t messageLen = 0;

  const size_t index = (size_t)info->index;
  if (index == 0) messageLen = 0;
  if (index != messageLen) return;

  if (messageLen + len >= sizeof(message)) {
    messageLen = 0;
    return;
  }

  memcpy(message + messageLen, data, len);
  messageLen += len;

  if (!info->final) return;

  submitCommandLines(message, messageLen);
  messageLen = 0;
}

char* trimLine(char* text) {
  while (*text == ' ' || *text == '\t') text++;
  size_t len = strlen(text);
  while (len > 0) {
    const char c = text[len - 1];
    if (c != ' ' && c != '\t') break;
    text[--len] = '\0';
  }
  return text;
}

bool equalsIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

bool startsWithIgnoreCase(const char* text, const char* prefix) {
  while (*prefix) {
    if (*text == '\0') return false;
    char ca = *text++;
    char cb = *prefix++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

bool forwardSerialLine(const char* line) {
  const uint32_t now = millis();
  if ((uint32_t)(now - g_rxWindowStart) >= kRxWindowMs) {
    g_rxWindowStart = now;
    g_rxWindowCount = 0;
  }
  if (g_rxWindowCount >= kRxLinesPerWindow) {
    g_rxBad++;
    return false;
  }
  if (g_cmdQueue == nullptr) return false;
  g_rxWindowCount++;
  if (xQueueSend(g_cmdQueue, line, 0) != pdTRUE) {
    g_rxBad++;
    return false;
  }
  return true;
}

bool usbAllowed() {
  return g_connMode != ConnMode::Wifi && g_connMode != ConnMode::Ble;
}

void stopMdns() {
  if (!g_mdnsStarted) return;
  MDNS.end();
  g_mdnsStarted = false;
}

void startWifiAp() {
  stopMdns();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);
  IPAddress ip(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, mask);
  if (!WiFi.softAP(g_ssid, g_wifiPass, kApChannel, 0, kMaxStations)) {
    Serial.println("# AP start failed");
    g_wifiEnabled = false;
    g_wifiState = WifiState::Off;
    return;
  }

#if CPR_CAPTIVE_DNS
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  if (!g_dns.start(53, "*", WiFi.softAPIP())) {
    Serial.println("# DNS start failed");
  }
#endif

  portENTER_CRITICAL(&g_staMux);
  strncpy(g_staIp, "192.168.4.1", sizeof(g_staIp) - 1);
  g_staIp[sizeof(g_staIp) - 1] = '\0';
  portEXIT_CRITICAL(&g_staMux);

  g_wifiEnabled = true;
  g_wifiState = WifiState::Ap;
}

void startWifiSta() {
  char ssid[33];
  char pass[64];
  bool hasSta = false;
  portENTER_CRITICAL(&g_staMux);
  hasSta = g_hasStaConfig;
  strncpy(ssid, g_staSsid, sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  strncpy(pass, g_staPass, sizeof(pass) - 1);
  pass[sizeof(pass) - 1] = '\0';
  g_staIp[0] = '\0';
  portEXIT_CRITICAL(&g_staMux);

  if (!hasSta || ssid[0] == '\0') {
    startWifiAp();
    return;
  }

  stopMdns();
#if CPR_CAPTIVE_DNS
  g_dns.stop();
#endif
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kMdnsHost);
  WiFi.setAutoReconnect(true);
  if (pass[0] != '\0') {
    WiFi.begin(ssid, pass);
  } else {
    WiFi.begin(ssid);
  }

  g_wifiEnabled = true;
  g_wifiState = WifiState::StaConnecting;
  g_staStartMs = millis();
  g_staDropMs = 0;
  Serial.print("# Wi-Fi STA connecting: ");
  Serial.println(ssid);
}

void startWifi() {
  if (g_wifiEnabled) return;

  bool hasSta = false;
  portENTER_CRITICAL(&g_staMux);
  hasSta = g_hasStaConfig;
  portEXIT_CRITICAL(&g_staMux);

  if (hasSta) {
    startWifiSta();
  } else {
    startWifiAp();
  }
}

void stopWifi() {
  if (!g_wifiEnabled && g_wifiState == WifiState::Off) return;

  g_ws.closeAll();
  stopMdns();
#if CPR_CAPTIVE_DNS
  g_dns.stop();
#endif
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  portENTER_CRITICAL(&g_staMux);
  g_staIp[0] = '\0';
  portEXIT_CRITICAL(&g_staMux);

  g_wifiEnabled = false;
  g_wifiState = WifiState::Off;
  g_staDropMs = 0;
}

void serviceWifi(uint32_t now) {
  bool applyNow = false;
  portENTER_CRITICAL(&g_staMux);
  if (g_staActionPending && (int32_t)(now - g_staActionAtMs) >= 0) {
    g_staActionPending = false;
    applyNow = true;
  }
  portEXIT_CRITICAL(&g_staMux);

  if (applyNow && g_connMode != ConnMode::Usb && g_connMode != ConnMode::Ble) {
    stopWifi();
    startWifi();
    return;
  }

  if (!g_wifiEnabled) return;

  if (g_wifiState == WifiState::StaConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      g_wifiState = WifiState::StaConnected;
      g_staDropMs = 0;

      const IPAddress ip = WiFi.localIP();
      char ipBuf[20];
      snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      portENTER_CRITICAL(&g_staMux);
      strncpy(g_staIp, ipBuf, sizeof(g_staIp) - 1);
      g_staIp[sizeof(g_staIp) - 1] = '\0';
      portEXIT_CRITICAL(&g_staMux);

      if (MDNS.begin(kMdnsHost)) {
        MDNS.addService("http", "tcp", kHttpPort);
        g_mdnsStarted = true;
      }
      Serial.print("# STA IP: ");
      Serial.print(ipBuf);
      Serial.println(" (http://cpr-trainer.local)");
    } else if ((uint32_t)(now - g_staStartMs) >= kStaConnectTimeoutMs) {
      Serial.println("# Wi-Fi STA timeout, falling back to AP");
      startWifiAp();
    }
    return;
  }

  if (g_wifiState == WifiState::StaConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      if (g_staDropMs == 0) {
        g_staDropMs = now;
      } else if ((uint32_t)(now - g_staDropMs) >= kStaDropFallbackMs &&
                 g_ws.count() == 0) {
        Serial.println("# Wi-Fi STA lost, falling back to AP");
        startWifiAp();
      }
    } else {
      g_staDropMs = 0;
    }
  }
}

void updateStatusLed(uint32_t now) {
  uint8_t level = LOW;
  if (g_connMode == ConnMode::Usb) {
    level = HIGH;
  } else if (g_connMode == ConnMode::Wifi) {
    // Blink LOW for 0.5 s every 3.0 s (HIGH 2.5 s, LOW 0.5 s).
    level = ((now % kLedWifiPeriodMs) < kLedWifiLowMs) ? LOW : HIGH;
  } else if (g_connMode == ConnMode::Ble) {
    // Double short blink every 2.0 s (ON 0..100 ms and 200..300 ms).
    const uint32_t phase = now % kLedBlePeriodMs;
    level = (phase < 100UL || (phase >= 200UL && phase < 300UL)) ? HIGH : LOW;
  } else {
    // Idle: toggle every 0.5 s.
    level = (((now / kLedIdleHalfPeriodMs) % 2UL) == 0UL) ? HIGH : LOW;
  }

  if ((int8_t)level != g_ledState) {
    g_ledState = (int8_t)level;
    digitalWrite(pins::kLedBuiltin, level);
  }
}

void updateConnMode(uint32_t now) {
  if (g_connMode == ConnMode::Ble) {
    if (!ble::isConnected()) {
      g_connMode = ConnMode::Idle;
      startWifi();
    }
    return;
  }

  if (g_connMode == ConnMode::Usb) {
    if ((uint32_t)(now - g_usbWebLastMs) >= kUsbWebTimeoutMs) {
      g_connMode = ConnMode::Idle;
      startWifi();
      ble::resumeAdvertising();
    }
    return;
  }

  if (g_connMode == ConnMode::Idle && ble::isConnected()) {
    g_connMode = ConnMode::Ble;
    stopWifi();
    return;
  }

  const size_t wsClients = g_wifiEnabled ? g_ws.count() : 0;
  if (g_connMode == ConnMode::Idle && wsClients > 0) {
    g_connMode = ConnMode::Wifi;
    ble::pauseAdvertising();
  } else if (g_connMode == ConnMode::Wifi && wsClients == 0) {
    g_connMode = ConnMode::Idle;
    ble::resumeAdvertising();
  }
}

void handleSerialLine(char* text) {
  char* line = trimLine(text);
  if (*line == '\0') return;

  // Ignore USB commands while a Wi-Fi or BLE client owns the session.
  if (!usbAllowed()) return;

  if (equalsIgnoreCase(line, "usb active")) {
    g_usbWebLastMs = millis();
    net::setSerialStreaming(true);
    if (g_connMode != ConnMode::Usb) {
      g_connMode = ConnMode::Usb;
      stopWifi();
      ble::pauseAdvertising();
    }
    return;
  }

  if (equalsIgnoreCase(line, "usb idle")) {
    if (g_connMode == ConnMode::Usb) {
      g_connMode = ConnMode::Idle;
      startWifi();
      ble::resumeAdvertising();
    }
    return;
  }

  if (equalsIgnoreCase(line, "serial on") ||
      equalsIgnoreCase(line, "serial off")) {
    const bool on = equalsIgnoreCase(line, "serial on");
    net::setSerialStreaming(on);
    Serial.println(on
        ? "{\"ty\":\"ack\",\"cmd\":\"serial\",\"ok\":1,\"msg\":\"on\"}"
        : "{\"ty\":\"ack\",\"cmd\":\"serial\",\"ok\":1,\"msg\":\"off\"}");
    return;
  }

  if (equalsIgnoreCase(line, "hx reset") ||
      equalsIgnoreCase(line, "autostart on") ||
      equalsIgnoreCase(line, "autostart off") ||
      startsWithIgnoreCase(line, "wifi sta") ||
      equalsIgnoreCase(line, "wifi status")) {
    net::setSerialStreaming(true);
    forwardSerialLine(line);
    return;
  }

  cpr::Command command;
  const bool parsed = cpr::parseCommand(line, command);
  if (parsed) {
    net::setSerialStreaming(true);
    forwardSerialLine(line);
    return;
  }

  if (g_serialStreaming) {
    forwardSerialLine(line);
  } else {
    g_rxBad++;
  }
}

void drainSerial() {
  static char line[kCmdLineLen];
  static size_t length = 0;
  static bool bad = false;

  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    g_rxCount++;
    const char c = (char)value;

    if (c == '\n' || c == '\r') {
      if (length > 0 || bad) {
        line[length] = '\0';
        if (bad) {
          g_rxBad++;
        } else {
          handleSerialLine(line);
        }
      }
      length = 0;
      bad = false;
      continue;
    }

    if ((c < 0x20 || c > 0x7E) && c != '\t') {
      bad = true;
      continue;
    }
    if (length >= sizeof(line) - 1) {
      bad = true;
      continue;
    }
    line[length++] = c;
  }
}

void drainOutput() {
  if (g_outQueue == nullptr) return;

  char line[kOutLineLen];
  while (xQueueReceive(g_outQueue, line, 0) == pdTRUE) {
    line[kOutLineLen - 1] = '\0';
    if (g_connMode == ConnMode::Ble && ble::isSubscribed()) {
      ble::notifyLine(line);
    } else if (g_wifiEnabled && g_ws.count() > 0) {
      g_ws.textAll(line);
    }

    if (g_serialStreaming && usbAllowed()) {
      const size_t len = strlen(line);
      if (Serial.availableForWrite() >= (int)(len + 2)) {
        Serial.println(line);
      } else {
        g_sSkip++;
      }
    }
  }
}

void sendLatestFrame(uint32_t now) {
  static uint32_t lastFrameMs = 0;

  bool fresh = false;
  portENTER_CRITICAL(&g_frameMux);
  fresh = g_frameFresh;
  portEXIT_CRITICAL(&g_frameMux);
  if (!fresh) return;
  if ((uint32_t)(now - lastFrameMs) < kFramePeriodMs) return;

  char line[kOutLineLen];
  portENTER_CRITICAL(&g_frameMux);
  size_t length = strnlen(g_frameLine, sizeof(line) - 1);
  memcpy(line, g_frameLine, length + 1);
  line[length] = '\0';
  g_frameFresh = false;
  portEXIT_CRITICAL(&g_frameMux);
  lastFrameMs = now;

  if (g_connMode == ConnMode::Ble && ble::isSubscribed()) {
    if (!ble::notifyLine(line)) {
      g_fDrop = g_fDrop + 1;
    }
  } else if (g_wifiEnabled && g_ws.count() > 0) {
    if (g_ws.availableForWriteAll()) {
      g_ws.textAll(line);
    } else {
      g_fDrop = g_fDrop + 1;
    }
  }

  if (g_serialStreaming && usbAllowed()) {
    const size_t len = strlen(line);
    if (Serial.availableForWrite() >= (int)(len + 2)) {
      Serial.println(line);
    } else {
      g_sSkip++;
    }
  }
}

void sendDiag(uint32_t now) {
  const bool wantBle = (g_connMode == ConnMode::Ble && ble::isSubscribed());
  const bool wantWs = g_wifiEnabled && g_ws.count() > 0;
  const bool wantSerial = g_serialStreaming && usbAllowed();
  if (!wantBle && !wantWs && !wantSerial) return;

  long rawAdc = 0;
  long offset = 0;
  float scale = 1.0f;
  uint16_t hxReboots = 0;
  uint8_t hxStatus = 1;
  portENTER_CRITICAL(&g_diagMux);
  rawAdc = g_diagRawAdc;
  offset = g_diagOffset;
  scale = g_diagScale;
  hxReboots = g_diagHxReboots;
  hxStatus = g_diagHxStatus;
  portEXIT_CRITICAL(&g_diagMux);

  const int staConnected = (g_wifiState == WifiState::StaConnected) ? 1 : 0;
  const int bleConnected = ble::isConnected() ? 1 : 0;

  char buffer[288];
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"ty\":\"diag\",\"up\":%lu,\"rx\":%lu,\"rxBad\":%lu,\"qDrop\":%lu,"
      "\"sSkip\":%lu,\"fDrop\":%lu,\"sps\":%u,\"heap\":%lu,\"ser\":%d,"
      "\"hx\":%d,\"wifi\":%d,\"sta\":%d,\"ble\":%d,\"hxs\":%u,\"hxRaw\":%ld,"
      "\"hxOff\":%ld,\"hxSc\":%.1f,\"hxRst\":%u}",
      (unsigned long)(now / 1000UL), (unsigned long)g_rxCount,
      (unsigned long)g_rxBad, (unsigned long)g_qDrop, (unsigned long)g_sSkip,
      (unsigned long)g_fDrop, (unsigned)g_diagSps,
      (unsigned long)ESP.getFreeHeap(), g_serialStreaming ? 1 : 0,
      g_diagHx ? 1 : 0, g_wifiEnabled ? 1 : 0, staConnected, bleConnected,
      (unsigned)hxStatus, rawAdc, offset, (double)scale, (unsigned)hxReboots);
  if (written <= 0 || (size_t)written >= sizeof(buffer)) return;

  if (wantBle) {
    ble::notifyLine(buffer);
  } else if (wantWs) {
    g_ws.textAll(buffer);
  }
  if (wantSerial) {
    if (Serial.availableForWrite() >= written + 2) {
      Serial.println(buffer);
    } else {
      g_sSkip = g_sSkip + 1;
    }
  }
}

} // namespace

namespace net {

void begin(QueueHandle_t outQueue, QueueHandle_t cmdQueue,
           const char* wifiPassword) {
  g_outQueue = outQueue;
  g_cmdQueue = cmdQueue;
  g_configLine[0] = '\0';
  g_connMode = ConnMode::Idle;
  g_wifiState = WifiState::Off;
  g_wifiEnabled = false;
  g_mdnsStarted = false;
  g_usbWebLastMs = 0;
  g_staStartMs = 0;
  g_staDropMs = 0;
  g_ledState = -1;
  g_staSsid[0] = '\0';
  g_staPass[0] = '\0';
  g_staIp[0] = '\0';
  g_staActionPending = false;

  pinMode(pins::kLedBuiltin, OUTPUT);
  updateStatusLed(millis());

  const char* password = wifiPassword;
  if (password == nullptr || strlen(password) < 8) {
    password = kDefaultWifiPass;
  }
  strncpy(g_wifiPass, password, sizeof(g_wifiPass) - 1);
  g_wifiPass[sizeof(g_wifiPass) - 1] = '\0';

  g_hasStaConfig = storage::loadStaConfig(g_staSsid, sizeof(g_staSsid),
                                          g_staPass, sizeof(g_staPass));

  WiFi.mode(WIFI_STA);
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  WiFi.macAddress(mac);
  snprintf(g_ssid, sizeof(g_ssid), "CPR-Trainer-%02X%02X", mac[4], mac[5]);

  ble::begin(g_ssid, g_cmdQueue);
  startWifi();

  g_ws.onEvent(onWsEvent);
  g_server.addHandler(&g_ws);
  g_server.on(AsyncURIMatcher::all(), onRequest);
  g_server.begin();
}

void loop() {
  const uint32_t now = millis();
  ble::loop();
  updateConnMode(now);
  serviceWifi(now);
  drainSerial();
  updateConnMode(now);
  drainOutput();
  sendLatestFrame(now);

  static uint32_t lastCleanupMs = 0;
  if (g_wifiEnabled && (uint32_t)(now - lastCleanupMs) >= 1000) {
    g_ws.cleanupClients(kMaxWsClients);
    lastCleanupMs = now;
    updateConnMode(now);
  }

  static uint32_t lastDiagMs = 0;
  if ((uint32_t)(now - lastDiagMs) >= kDiagPeriodMs) {
    sendDiag(now);
    lastDiagMs = now;
  }

  updateStatusLed(now);
}

void setConfigLine(const char* line) {
  portENTER_CRITICAL(&g_configMux);
  strncpy(g_configLine, line != nullptr ? line : "", sizeof(g_configLine) - 1);
  g_configLine[sizeof(g_configLine) - 1] = '\0';
  portEXIT_CRITICAL(&g_configMux);
  ble::setConfigLine(line);
}

void setFrame(const char* line) {
  portENTER_CRITICAL(&g_frameMux);
  strncpy(g_frameLine, line != nullptr ? line : "", sizeof(g_frameLine) - 1);
  g_frameLine[sizeof(g_frameLine) - 1] = '\0';
  g_frameFresh = true;
  portEXIT_CRITICAL(&g_frameMux);
}

void setDiagSensor(uint8_t sps, bool hxPresent, long rawAdc, long offset,
                   float scale, uint16_t rebootCount, uint8_t hxStatus) {
  g_diagSps = sps;
  g_diagHx = hxPresent;
  portENTER_CRITICAL(&g_diagMux);
  g_diagRawAdc = rawAdc;
  g_diagOffset = offset;
  g_diagScale = scale;
  g_diagHxReboots = rebootCount;
  g_diagHxStatus = hxStatus;
  portEXIT_CRITICAL(&g_diagMux);
}

void noteQueueDrop() { g_qDrop++; }

bool serialStreaming() { return g_serialStreaming; }

void setSerialStreaming(bool on) { g_serialStreaming = on; }

bool requestStaConfig(const char* ssid, const char* pass) {
  if (!storage::saveStaConfig(ssid, pass)) return false;

  const char* safePass = (pass != nullptr) ? pass : "";
  portENTER_CRITICAL(&g_staMux);
  strncpy(g_staSsid, ssid, sizeof(g_staSsid) - 1);
  g_staSsid[sizeof(g_staSsid) - 1] = '\0';
  strncpy(g_staPass, safePass, sizeof(g_staPass) - 1);
  g_staPass[sizeof(g_staPass) - 1] = '\0';
  g_hasStaConfig = true;
  g_staActionPending = true;
  g_staActionAtMs = millis() + kStaApplyDelayMs;
  portEXIT_CRITICAL(&g_staMux);
  return true;
}

bool requestStaClear() {
  if (!storage::clearStaConfig()) return false;

  portENTER_CRITICAL(&g_staMux);
  g_staSsid[0] = '\0';
  g_staPass[0] = '\0';
  g_hasStaConfig = false;
  g_staActionPending = true;
  g_staActionAtMs = millis() + kStaApplyDelayMs;
  portEXIT_CRITICAL(&g_staMux);
  return true;
}

void formatWifiStatus(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;

  if (g_connMode == ConnMode::Ble) {
    snprintf(out, cap, "ble");
    return;
  }
  if (g_connMode == ConnMode::Usb) {
    snprintf(out, cap, "usb");
    return;
  }

  char ip[20];
  char ssid[33];
  portENTER_CRITICAL(&g_staMux);
  strncpy(ip, g_staIp, sizeof(ip) - 1);
  ip[sizeof(ip) - 1] = '\0';
  strncpy(ssid, g_staSsid, sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  portEXIT_CRITICAL(&g_staMux);

  if (g_wifiState == WifiState::StaConnected) {
    snprintf(out, cap, "sta %s", ip[0] != '\0' ? ip : "0.0.0.0");
  } else if (g_wifiState == WifiState::StaConnecting) {
    snprintf(out, cap, "connecting %s", ssid);
  } else if (g_wifiState == WifiState::Ap) {
    snprintf(out, cap, "ap 192.168.4.1");
  } else {
    snprintf(out, cap, "off");
  }
}

} // namespace net

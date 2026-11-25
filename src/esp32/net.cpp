#include "net.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "web_assets.h"
#include "cpr_command.h"
#include "cpr_json.h"

#ifndef CPR_CAPTIVE_DNS
#define CPR_CAPTIVE_DNS 1
#endif

#ifndef CPR_SERIAL_STREAM_DEFAULT
#define CPR_SERIAL_STREAM_DEFAULT 1
#endif

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kApChannel = 6;
constexpr uint8_t kMaxStations = 4;
constexpr uint16_t kMaxWsClients = 4;
constexpr size_t kOutLineLen = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdLineLen = 64;
constexpr const char* kDefaultWifiPass = "cprtrain123";
constexpr uint32_t kFramePeriodMs = 50;
constexpr uint32_t kDiagPeriodMs = 5000;
constexpr uint32_t kRxWindowMs = 1000;
constexpr uint16_t kRxLinesPerWindow = 20;

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

// Each counter has a single writer task; volatile is enough for a snapshot.
volatile uint32_t g_rxCount = 0;
volatile uint32_t g_rxBad = 0;
volatile uint32_t g_qDrop = 0;
volatile uint32_t g_sSkip = 0;
volatile uint32_t g_fDrop = 0;
volatile uint8_t g_diagSps = 0;
volatile bool g_diagHx = false;

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

void handleSerialLine(char* text) {
  char* line = trimLine(text);
  if (*line == '\0') return;

  if (equalsIgnoreCase(line, "serial on") ||
      equalsIgnoreCase(line, "serial off")) {
    const bool on = equalsIgnoreCase(line, "serial on");
    net::setSerialStreaming(on);
    Serial.println(on
        ? "{\"ty\":\"ack\",\"cmd\":\"serial\",\"ok\":1,\"msg\":\"on\"}"
        : "{\"ty\":\"ack\",\"cmd\":\"serial\",\"ok\":1,\"msg\":\"off\"}");
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

    if (c < 0x20 || c > 0x7E) {
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
    g_ws.textAll(line);

    if (g_serialStreaming) {
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

  if (g_ws.count() > 0) {
    if (g_ws.availableForWriteAll()) {
      g_ws.textAll(line);
    } else {
      g_fDrop++;
    }
  }

  if (g_serialStreaming) {
    const size_t len = strlen(line);
    if (Serial.availableForWrite() >= (int)(len + 2)) {
      Serial.println(line);
    } else {
      g_sSkip++;
    }
  }
}

void sendDiag(uint32_t now) {
  if (g_ws.count() == 0) return;

  char buffer[192];
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"ty\":\"diag\",\"up\":%lu,\"rx\":%lu,\"rxBad\":%lu,\"qDrop\":%lu,"
      "\"sSkip\":%lu,\"fDrop\":%lu,\"sps\":%u,\"heap\":%lu,\"ser\":%d,"
      "\"hx\":%d}",
      (unsigned long)(now / 1000UL), (unsigned long)g_rxCount,
      (unsigned long)g_rxBad, (unsigned long)g_qDrop, (unsigned long)g_sSkip,
      (unsigned long)g_fDrop, (unsigned)g_diagSps,
      (unsigned long)ESP.getFreeHeap(), g_serialStreaming ? 1 : 0,
      g_diagHx ? 1 : 0);
  if (written <= 0 || (size_t)written >= sizeof(buffer)) return;
  g_ws.textAll(buffer);
}

} // namespace

namespace net {

void begin(QueueHandle_t outQueue, QueueHandle_t cmdQueue,
           const char* wifiPassword) {
  g_outQueue = outQueue;
  g_cmdQueue = cmdQueue;
  g_configLine[0] = '\0';

  const char* password = wifiPassword;
  if (password == nullptr || strlen(password) < 8) {
    password = kDefaultWifiPass;
  }

  WiFi.mode(WIFI_AP);
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  WiFi.macAddress(mac);

  char ssid[24];
  snprintf(ssid, sizeof(ssid), "CPR-Trainer-%02X%02X", mac[4], mac[5]);

  IPAddress ip(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, mask);
  if (!WiFi.softAP(ssid, password, kApChannel, 0, kMaxStations)) {
    Serial.println("# AP start failed");
  }

#if CPR_CAPTIVE_DNS
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  if (!g_dns.start(53, "*", WiFi.softAPIP())) {
    Serial.println("# DNS start failed");
  }
#endif

  g_ws.onEvent(onWsEvent);
  g_server.addHandler(&g_ws);
  g_server.on(AsyncURIMatcher::all(), onRequest);
  g_server.begin();
}

void loop() {
  drainSerial();
  drainOutput();

  const uint32_t now = millis();
  sendLatestFrame(now);

  static uint32_t lastCleanupMs = 0;
  if ((uint32_t)(now - lastCleanupMs) >= 1000) {
    g_ws.cleanupClients(kMaxWsClients);
    lastCleanupMs = now;
  }

  static uint32_t lastDiagMs = 0;
  if ((uint32_t)(now - lastDiagMs) >= kDiagPeriodMs) {
    sendDiag(now);
    lastDiagMs = now;
  }
}

void setConfigLine(const char* line) {
  portENTER_CRITICAL(&g_configMux);
  strncpy(g_configLine, line != nullptr ? line : "", sizeof(g_configLine) - 1);
  g_configLine[sizeof(g_configLine) - 1] = '\0';
  portEXIT_CRITICAL(&g_configMux);
}

void setFrame(const char* line) {
  portENTER_CRITICAL(&g_frameMux);
  strncpy(g_frameLine, line != nullptr ? line : "", sizeof(g_frameLine) - 1);
  g_frameLine[sizeof(g_frameLine) - 1] = '\0';
  g_frameFresh = true;
  portEXIT_CRITICAL(&g_frameMux);
}

void setDiagSensor(uint8_t sps, bool hxPresent) {
  g_diagSps = sps;
  g_diagHx = hxPresent;
}

void noteQueueDrop() { g_qDrop++; }

bool serialStreaming() { return g_serialStreaming; }

void setSerialStreaming(bool on) { g_serialStreaming = on; }

} // namespace net

#include "net.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "web_assets.h"
#include "cpr_json.h"

#ifndef CPR_CAPTIVE_DNS
#define CPR_CAPTIVE_DNS 1
#endif

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kApChannel = 6;
constexpr uint8_t kMaxStations = 4;
constexpr uint16_t kMaxWsClients = 4;
constexpr size_t kOutLineLen = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdLineLen = 64;
constexpr const char* kDefaultWifiPass = "cprtrain123";

AsyncWebServer g_server(kHttpPort);
AsyncWebSocket g_ws("/ws");
DNSServer g_dns;

QueueHandle_t g_outQueue = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

char g_configLine[kOutLineLen];
portMUX_TYPE g_configMux = portMUX_INITIALIZER_UNLOCKED;

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

void drainSerial() {
  static char line[kCmdLineLen];
  static size_t length = 0;

  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    const char c = (char)value;

    if (c == '\n' || c == '\r') {
      if (length > 0) {
        line[length] = '\0';
        if (g_cmdQueue != nullptr) xQueueSend(g_cmdQueue, line, 0);
        length = 0;
      }
    } else if (length < sizeof(line) - 1) {
      line[length++] = c;
    } else {
      length = 0;
    }
  }
}

void drainOutput() {
  if (g_outQueue == nullptr) return;

  char line[kOutLineLen];
  while (xQueueReceive(g_outQueue, line, 0) == pdTRUE) {
    line[kOutLineLen - 1] = '\0';
    Serial.println(line);
    g_ws.textAll(line);
  }
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

  static uint32_t lastCleanupMs = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastCleanupMs) >= 1000) {
    g_ws.cleanupClients(kMaxWsClients);
    lastCleanupMs = now;
  }
}

void setConfigLine(const char* line) {
  portENTER_CRITICAL(&g_configMux);
  strncpy(g_configLine, line != nullptr ? line : "", sizeof(g_configLine) - 1);
  g_configLine[sizeof(g_configLine) - 1] = '\0';
  portEXIT_CRITICAL(&g_configMux);
}

} // namespace net

#include "net.h"

#include <esp_mac.h>

#include <stdio.h>
#include <string.h>

#include "ble.h"
#include "cpr_command.h"
#include "cpr_json.h"
#include "pins.h"

#ifndef CPR_SERIAL_STREAM_DEFAULT
#define CPR_SERIAL_STREAM_DEFAULT 1
#endif

namespace {

enum class ConnMode : uint8_t { Idle = 0, Usb = 1, Ble = 2 };

constexpr size_t kOutLineLen = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdLineLen = 64;
constexpr uint32_t kFramePeriodMs = 50;
constexpr uint32_t kDiagPeriodMs = 5000;
constexpr uint32_t kRxWindowMs = 1000;
constexpr uint16_t kRxLinesPerWindow = 20;

// Engineering values, tunable: USB lease timeout and status LED timings.
constexpr uint32_t kUsbWebTimeoutMs = 10000;
constexpr uint32_t kLedIdleHalfPeriodMs = 500;
constexpr uint32_t kLedBlePeriodMs = 2000;

QueueHandle_t g_outQueue = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

char g_frameLine[kOutLineLen];
portMUX_TYPE g_frameMux = portMUX_INITIALIZER_UNLOCKED;
bool g_frameFresh = false;

bool g_serialStreaming = (CPR_SERIAL_STREAM_DEFAULT != 0);
ConnMode g_connMode = ConnMode::Idle;
uint32_t g_usbWebLastMs = 0;
int8_t g_ledState = -1;
char g_deviceName[24];

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
    g_rxBad = g_rxBad + 1;
    return false;
  }
  if (g_cmdQueue == nullptr) return false;
  g_rxWindowCount++;
  if (xQueueSend(g_cmdQueue, line, 0) != pdTRUE) {
    g_rxBad = g_rxBad + 1;
    return false;
  }
  return true;
}

bool usbAllowed() {
  return g_connMode != ConnMode::Ble;
}

void updateStatusLed(uint32_t now) {
  uint8_t level = LOW;
  if (g_connMode == ConnMode::Usb) {
    level = HIGH;
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
    }
    return;
  }

  if (g_connMode == ConnMode::Usb) {
    if ((uint32_t)(now - g_usbWebLastMs) >= kUsbWebTimeoutMs) {
      g_connMode = ConnMode::Idle;
      ble::resumeAdvertising();
    }
    return;
  }

  if (g_connMode == ConnMode::Idle && ble::isConnected()) {
    g_connMode = ConnMode::Ble;
  }
}

void handleSerialLine(char* text) {
  char* line = trimLine(text);
  if (*line == '\0') return;

  // Ignore USB commands while a BLE client owns the session.
  if (!usbAllowed()) return;

  if (equalsIgnoreCase(line, "usb active")) {
    g_usbWebLastMs = millis();
    net::setSerialStreaming(true);
    if (g_connMode != ConnMode::Usb) {
      g_connMode = ConnMode::Usb;
      ble::pauseAdvertising();
    }
    return;
  }

  if (equalsIgnoreCase(line, "usb idle")) {
    if (g_connMode == ConnMode::Usb) {
      g_connMode = ConnMode::Idle;
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
      equalsIgnoreCase(line, "autostart off")) {
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
    g_rxBad = g_rxBad + 1;
  }
}

void drainSerial() {
  static char line[kCmdLineLen];
  static size_t length = 0;
  static bool bad = false;

  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    g_rxCount = g_rxCount + 1;
    const char c = (char)value;

    if (c == '\n' || c == '\r') {
      if (length > 0 || bad) {
        line[length] = '\0';
        if (bad) {
          g_rxBad = g_rxBad + 1;
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
    if (g_connMode == ConnMode::Ble && ble::isSubscribed()) {
      ble::notifyLine(line);
    }

    if (g_serialStreaming && usbAllowed()) {
      const size_t len = strlen(line);
      if (Serial.availableForWrite() >= (int)(len + 2)) {
        Serial.println(line);
      } else {
        g_sSkip = g_sSkip + 1;
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
  }

  if (g_serialStreaming && usbAllowed()) {
    const size_t len = strlen(line);
    if (Serial.availableForWrite() >= (int)(len + 2)) {
      Serial.println(line);
    } else {
      g_sSkip = g_sSkip + 1;
    }
  }
}

void sendDiag(uint32_t now) {
  const bool wantBle = (g_connMode == ConnMode::Ble && ble::isSubscribed());
  const bool wantSerial = g_serialStreaming && usbAllowed();
  if (!wantBle && !wantSerial) return;

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

  const int bleConnected = ble::isConnected() ? 1 : 0;

  char buffer[288];
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"ty\":\"diag\",\"up\":%lu,\"rx\":%lu,\"rxBad\":%lu,\"qDrop\":%lu,"
      "\"sSkip\":%lu,\"fDrop\":%lu,\"sps\":%u,\"heap\":%lu,\"ser\":%d,"
      "\"hx\":%d,\"ble\":%d,\"hxs\":%u,\"hxRaw\":%ld,\"hxOff\":%ld,"
      "\"hxSc\":%.1f,\"hxRst\":%u}",
      (unsigned long)(now / 1000UL), (unsigned long)g_rxCount,
      (unsigned long)g_rxBad, (unsigned long)g_qDrop, (unsigned long)g_sSkip,
      (unsigned long)g_fDrop, (unsigned)g_diagSps,
      (unsigned long)ESP.getFreeHeap(), g_serialStreaming ? 1 : 0,
      g_diagHx ? 1 : 0, bleConnected, (unsigned)hxStatus, rawAdc, offset,
      (double)scale, (unsigned)hxReboots);
  if (written <= 0 || (size_t)written >= sizeof(buffer)) return;

  if (wantBle) {
    ble::notifyLine(buffer);
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

void begin(QueueHandle_t outQueue, QueueHandle_t cmdQueue) {
  g_outQueue = outQueue;
  g_cmdQueue = cmdQueue;
  g_connMode = ConnMode::Idle;
  g_usbWebLastMs = 0;
  g_ledState = -1;

  pinMode(pins::kLedBuiltin, OUTPUT);
  updateStatusLed(millis());

  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(g_deviceName, sizeof(g_deviceName), "CPR-Trainer-%02X%02X",
           mac[4], mac[5]);

  ble::begin(g_deviceName, g_cmdQueue);
  Serial.print("# BLE ready: ");
  Serial.println(g_deviceName);
}

void loop() {
  const uint32_t now = millis();
  ble::loop();
  updateConnMode(now);
  drainSerial();
  updateConnMode(now);
  drainOutput();
  sendLatestFrame(now);

  static uint32_t lastDiagMs = 0;
  if ((uint32_t)(now - lastDiagMs) >= kDiagPeriodMs) {
    sendDiag(now);
    lastDiagMs = now;
  }

  updateStatusLed(now);
}

void setConfigLine(const char* line) {
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

void noteQueueDrop() { g_qDrop = g_qDrop + 1; }

bool serialStreaming() { return g_serialStreaming; }

void setSerialStreaming(bool on) { g_serialStreaming = on; }

} // namespace net

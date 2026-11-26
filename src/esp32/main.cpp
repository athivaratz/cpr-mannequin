#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpr.h"

#include "feedback.h"
#include "net.h"
#include "pins.h"
#include "sensors.h"
#include "storage.h"

namespace {

constexpr const char* kFirmwareVersion = "1.4.0";
constexpr const char* kDefaultWifiPass = "cprtrain123";
constexpr size_t kOutQueueLength = 24;
constexpr size_t kOutLineLength = cpr::kMaxJsonLine + 8;
constexpr size_t kCmdQueueLength = 8;
constexpr size_t kCmdLineLength = 64;
constexpr uint32_t kFramePeriodMs = 50;

QueueHandle_t g_outQueue = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

cpr::Engine g_engine;
cpr::Config g_cfg;
cpr::CalibrationData g_cal;
cpr::UserTh g_th;
cpr::Mode g_mode = cpr::Mode::Free;
cpr::Metronome g_metro;

Sensors g_sensors;
Feedback g_feedback;

bool g_lowSpsWarned = false;
bool g_lineTooLongLogged = false;
float g_lastPeakKg = NAN;

bool isBlank(const char* line) {
  if (line == nullptr) return true;
  for (const char* p = line; *p; ++p) {
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return false;
  }
  return true;
}

void queueLine(const char* line, bool important) {
  if (g_outQueue == nullptr || line == nullptr) return;

  const size_t length = strlen(line);
  char buffer[kOutLineLength];
  if (length >= sizeof(buffer)) {
    if (!g_lineTooLongLogged) {
      g_lineTooLongLogged = true;
      const char* message = "# line too long";
      memset(buffer, 0, sizeof(buffer));
      strncpy(buffer, message, sizeof(buffer) - 1);
      if (xQueueSend(g_outQueue, buffer, pdMS_TO_TICKS(20)) != pdTRUE) {
        net::noteQueueDrop();
      }
    }
    return;
  }

  memset(buffer, 0, sizeof(buffer));
  memcpy(buffer, line, length + 1);

  const TickType_t wait = important ? pdMS_TO_TICKS(20) : 0;
  if (xQueueSend(g_outQueue, buffer, wait) != pdTRUE) {
    net::noteQueueDrop();
  }
}

void sendAck(const char* cmd, bool ok, const char* msg) {
  char buffer[kOutLineLength];
  const size_t length = cpr::jsonAck(buffer, sizeof(buffer), cmd, ok, msg);
  if (length != 0) queueLine(buffer, true);
}

void sendConfig() {
  char buffer[kOutLineLength];
  const size_t length =
      cpr::jsonConfig(buffer, sizeof(buffer), g_cfg, g_cal,
                      g_sensors.tofPresent(), "esp32", kFirmwareVersion,
                      g_sensors.hx711Present(), CPR_HAS_FOIL != 0);
  if (length == 0) return;

  net::setConfigLine(buffer);
  queueLine(buffer, true);
}

void applyUserTh() {
  g_cfg = cpr::defaultConfig((cpr::Profile)g_cal.profile);
  cpr::userThApply(g_th, g_cfg);
  g_cal.goodMinKg = g_th.good;
  g_cal.deepMinKg = g_th.deep;
  g_engine.setConfig(g_cfg);
}

char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool startsWithSet(const char* line) {
  const char* prefix = "set ";
  for (; *prefix; ++prefix, ++line) {
    if (*line == '\0' || lowerAscii(*line) != *prefix) return false;
  }
  return true;
}

bool isHxResetCommand(const char* line) {
  if (line == nullptr) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* expected = "hx reset";
  for (; *expected; ++expected, ++line) {
    if (*line == '\0' || lowerAscii(*line) != *expected) return false;
  }
  while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
  return *line == '\0';
}

bool parseAutoStartCommand(const char* line, bool& outOn) {
  if (line == nullptr) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* prefix = "autostart ";
  for (; *prefix; ++prefix, ++line) {
    if (*line == '\0' || lowerAscii(*line) != *prefix) return false;
  }
  while (*line == ' ' || *line == '\t') line++;
  char arg[8];
  size_t n = 0;
  while (*line && *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
    if (n + 1 >= sizeof(arg)) return false;
    arg[n++] = lowerAscii(*line++);
  }
  arg[n] = '\0';
  while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
  if (*line != '\0') return false;
  if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
    outOn = true;
    return true;
  }
  if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
    outOn = false;
    return true;
  }
  return false;
}

void lowerWord(char* s) {
  for (char* p = s; *p; ++p) *p = lowerAscii(*p);
}

bool parseFloatToken(const char* tok, float& out) {
  if (tok == nullptr || *tok == '\0') return false;
  char* end = nullptr;
  const float value = strtof(tok, &end);
  if (end == tok || *end != '\0') return false;
  out = value;
  return true;
}

void applySet(const cpr::UserTh& t) {
  if (!cpr::userThValid(t)) {
    sendAck("set", false, "bad range");
    return;
  }

  g_th = t;
  const bool saved = storage::saveUserTh(g_th);
  applyUserTh();
  sendAck("set", saved, saved ? "ok" : "write failed");
  sendConfig();
}

void handleSet(const char* line) {
  char work[kCmdLineLength];
  const size_t length = strlen(line);
  if (length >= sizeof(work)) {
    sendAck("set", false, "bad syntax");
    return;
  }
  memcpy(work, line, length + 1);

  char* save = nullptr;
  if (strtok_r(work, " \t\r\n", &save) == nullptr) {
    sendAck("set", false, "bad syntax");
    return;
  }

  char* tok = strtok_r(nullptr, " \t\r\n", &save);
  if (tok == nullptr) {
    sendAck("set", false, "bad syntax");
    return;
  }
  lowerWord(tok);

  if (strcmp(tok, "reset") == 0) {
    if (strtok_r(nullptr, " \t\r\n", &save) != nullptr) {
      sendAck("set", false, "bad syntax");
      return;
    }
    cpr::userThDefaults(g_th);
    const bool saved = storage::saveUserTh(g_th);
    applyUserTh();
    sendAck("set", saved, saved ? "ok" : "write failed");
    sendConfig();
    return;
  }

  if (strcmp(tok, "th") == 0) {
    float values[6];
    for (uint8_t i = 0; i < 6; ++i) {
      char* value = strtok_r(nullptr, " \t\r\n", &save);
      if (value == nullptr || !parseFloatToken(value, values[i])) {
        sendAck("set", false, "bad syntax");
        return;
      }
    }
    if (strtok_r(nullptr, " \t\r\n", &save) != nullptr) {
      sendAck("set", false, "bad syntax");
      return;
    }

    cpr::UserTh t;
    t.good = values[0];
    t.deep = values[1];
    t.over = values[2];
    t.contact = values[3];
    t.recoil = values[4];
    t.filt = values[5];
    applySet(t);
    return;
  }

  int field = -1;
  if (strcmp(tok, "good") == 0) field = 0;
  else if (strcmp(tok, "deep") == 0) field = 1;
  else if (strcmp(tok, "over") == 0) field = 2;
  else if (strcmp(tok, "contact") == 0) field = 3;
  else if (strcmp(tok, "recoil") == 0) field = 4;
  else if (strcmp(tok, "filter") == 0) field = 5;

  if (field < 0) {
    sendAck("set", false, "bad syntax");
    return;
  }

  char* valueTok = strtok_r(nullptr, " \t\r\n", &save);
  float value = 0.0f;
  if (valueTok == nullptr || !parseFloatToken(valueTok, value)) {
    sendAck("set", false, "bad syntax");
    return;
  }
  if (strtok_r(nullptr, " \t\r\n", &save) != nullptr) {
    sendAck("set", false, "bad syntax");
    return;
  }

  cpr::UserTh t = g_th;
  switch (field) {
    case 0: t.good = value; break;
    case 1: t.deep = value; break;
    case 2: t.over = value; break;
    case 3: t.contact = value; break;
    case 4: t.recoil = value; break;
    default: t.filt = value; break;
  }
  applySet(t);
}

bool readHxTare() {
  if (!g_sensors.hx711Present()) return false;
  return g_sensors.tareLoadCell();
}

bool readTofRest() {
  if (!g_sensors.tofPresent()) return false;
  return g_sensors.readRestDistance();
}

void handleTare() {
  if (!g_sensors.hx711Present()) {
    sendAck("tare", false, "no hx711");
    return;
  }

  const bool hxOk = readHxTare();
  const bool tofOk = g_sensors.tofPresent() ? readTofRest() : true;
  const bool ok = hxOk && tofOk;
  sendAck("tare", ok, ok ? "ok" : "sensor not ready");
  sendConfig();
}

void handleProfile(uint8_t arg) {
  g_cal.profile = arg;
  applyUserTh();
  g_engine.setCalibration(g_cal);
  sendAck("profile", true, "ok");
  sendConfig();
}

void handleCalWeight(float kg) {
  if (!g_sensors.hx711Present()) {
    sendAck("cal weight", false, "no hx711");
    return;
  }
  if (kg <= 0.0f) {
    sendAck("cal weight", false, "bad weight");
    return;
  }

  float newScale = 0.0f;
  if (!g_sensors.calibrateWeight(kg, newScale)) {
    sendAck("cal weight", false, "reading failed");
    return;
  }

  g_cal.scaleFactor = newScale;
  g_engine.setCalibration(g_cal);
  sendAck("cal weight", true, "ok");
  sendConfig();
}

void handleCalBands(float goodKg, float deepKg) {
  cpr::UserTh candidate = g_th;
  candidate.good = goodKg;
  candidate.deep = deepKg;
  if (candidate.deep >= candidate.over) {
    candidate.over = candidate.deep + 6.0f;
    if (candidate.over > 60.0f) candidate.over = 60.0f;
  }

  if (!cpr::userThValid(candidate)) {
    sendAck("cal bands", false, "bad bands");
    return;
  }

  g_th = candidate;
  storage::saveUserTh(g_th);
  applyUserTh();
  sendAck("cal bands", true, "ok");
  sendConfig();
}

void handleCalPeak() {
  if (isnan(g_lastPeakKg)) {
    sendAck("cal peak", false, "no peak");
    return;
  }

  char msg[24];
  snprintf(msg, sizeof(msg), "%.1f", (double)g_lastPeakKg);
  sendAck("cal peak", true, msg);
}

void handleCommand(const char* line) {
  if (isBlank(line)) return;
  if (startsWithSet(line)) {
    handleSet(line);
    return;
  }
  if (isHxResetCommand(line)) {
    g_sensors.requestHxReboot(millis());
    sendAck("hx reset", true, "ok");
    sendConfig();
    return;
  }
  bool autoStartOn = false;
  if (parseAutoStartCommand(line, autoStartOn)) {
    g_engine.setAutoStart(autoStartOn);
    sendAck("autostart", true, autoStartOn ? "on" : "off");
    return;
  }

  cpr::Command command;
  if (!cpr::parseCommand(line, command)) {
    sendAck("unknown", false, "unknown command");
    return;
  }

  const uint32_t now = millis();

  switch (command.cmd) {
    case cpr::Cmd::Tare:
      handleTare();
      break;

    case cpr::Cmd::Profile:
      handleProfile(command.arg);
      break;

    case cpr::Cmd::Mode:
      g_mode = (cpr::Mode)command.arg;
      g_engine.setMode(g_mode);
      sendAck("mode", true, "ok");
      break;

    case cpr::Cmd::Start:
      g_engine.startSession(now);
      sendAck("start", true, "ok");
      break;

    case cpr::Cmd::Stop:
      g_engine.endSession(now);
      sendAck("stop", true, "ok");
      break;

    case cpr::Cmd::MetroOn:
      g_metro.start(now, g_cfg.metronomeBpm);
      sendAck("metro", true, "on");
      break;

    case cpr::Cmd::MetroOff:
      g_metro.stop();
      sendAck("metro", true, "off");
      break;

    case cpr::Cmd::CalStart:
      if (!g_sensors.tofPresent()) {
        sendAck("cal start", false, "no tof");
      } else {
        g_engine.calStart();
        sendAck("cal start", true, "ok");
      }
      break;

    case cpr::Cmd::CalFinish: {
      cpr::CalibrationData fitted;
      if (!g_engine.calFinish(fitted)) {
        sendAck("cal finish", false, "need 2 points");
        break;
      }
      g_cal = fitted;
      g_engine.setCalibration(g_cal);
      applyUserTh();
      sendAck("cal finish", true, "ok");
      sendConfig();
      break;
    }

    case cpr::Cmd::CalCancel:
      g_engine.calCancel();
      sendAck("cal cancel", true, "ok");
      break;

    case cpr::Cmd::CalWeight:
      handleCalWeight(command.f1);
      break;

    case cpr::Cmd::CalBands:
      handleCalBands(command.f1, command.f2);
      break;

    case cpr::Cmd::CalPeak:
      handleCalPeak();
      break;

    case cpr::Cmd::CalSave: {
      const bool ok = storage::saveCalibration(g_cal);
      sendAck("cal save", ok, ok ? "ok" : "write failed");
      if (ok) sendConfig();
      break;
    }

    case cpr::Cmd::CalReset: {
      cpr::calibDefaults(g_cal);
      cpr::userThDefaults(g_th);
      g_sensors.setScale(g_cal.scaleFactor);
      g_engine.setCalibration(g_cal);
      applyUserTh();
      const bool saved = storage::saveUserTh(g_th);
      sendAck("cal reset", saved, saved ? "ok" : "write failed");
      sendConfig();
      break;
    }

    case cpr::Cmd::Config:
      sendAck("config", true, "ok");
      sendConfig();
      break;

    case cpr::Cmd::WifiPass: {
      const bool ok = storage::saveWifiPass(command.text);
      sendAck("wifi pass", ok, ok ? "saved, reboot" : "write failed");
      break;
    }

    case cpr::Cmd::None:
    case cpr::Cmd::Unknown:
    default:
      sendAck("unknown", false, "unknown command");
      break;
  }
}

void pollEvents() {
  const uint32_t now = millis();
  cpr::Event event;
  while (g_engine.pollEvent(event)) {
    char buffer[kOutLineLength];
    if (cpr::jsonEvent(buffer, sizeof(buffer), event) != 0) {
      queueLine(buffer, true);
    }

    if (event.type == cpr::EventType::Compression) {
      g_lastPeakKg = (event.c.source == cpr::Source::ForceKg) ? event.c.peak : NAN;
    }

    // While the metronome runs its 25 ms ticks are the only compression
    // sound; overload still warns.
    if (event.type == cpr::EventType::Overload || !g_metro.running()) {
      g_feedback.onEvent(event, now);
    }

    if (event.type == cpr::EventType::SessionEnd) {
      if (cpr::jsonSummary(buffer, sizeof(buffer), g_engine.stats(), g_mode,
                           g_cfg.profile) != 0) {
        queueLine(buffer, true);
      }
    }
  }
}

void sendFrame() {
  char buffer[kOutLineLength];
  const size_t baseLen = cpr::jsonFrame(buffer, sizeof(buffer), g_engine.live());
  if (baseLen == 0) return;

  // Append ESP32-only raw signed 2-decimal kg and HX711 health status so
  // lib/cpr_core and Nano builds stay untouched.
  if (baseLen > 1 && buffer[baseLen - 1] == '}') {
    char extra[48];
    const float rawKg = g_sensors.lastRawKg();
    const unsigned hxs = (unsigned)g_sensors.hxStatus();
    int extraLen = 0;
    if (isfinite(rawKg) && fabsf(rawKg) < 999999.0f) {
      extraLen = snprintf(extra, sizeof(extra), ",\"rkg\":%.2f,\"hxs\":%u}",
                          (double)rawKg, hxs);
    } else {
      extraLen = snprintf(extra, sizeof(extra), ",\"rkg\":null,\"hxs\":%u}", hxs);
    }
    if (extraLen > 0 && (size_t)extraLen < sizeof(extra) &&
        (baseLen - 1) + (size_t)extraLen <= cpr::kMaxJsonLine) {
      memcpy(buffer + baseLen - 1, extra, (size_t)extraLen + 1);
    }
  }

  net::setFrame(buffer);
}

void sampleTask(void*) {
  sendConfig();

  uint32_t lastFrameMs = millis();
  uint32_t lastProcessMs = lastFrameMs;
  uint32_t lastRateCheckMs = lastFrameMs;
  bool fastFilter = false;
  bool haveFrameTime = true;

  for (;;) {
    const uint32_t now = millis();

    char commandLine[kCmdLineLength];
    while (g_cmdQueue != nullptr &&
           xQueueReceive(g_cmdQueue, commandLine, 0) == pdTRUE) {
      commandLine[kCmdLineLength - 1] = '\0';
      handleCommand(commandLine);
    }

    if (g_sensors.service(now)) {
      g_engine.setTofPresent(g_sensors.tofPresent());
      sendConfig();
    }

    cpr::Sample sample;
    bool haveSample = g_sensors.read(now, sample);

    // With no sensors at all, keep the dashboard alive with empty samples.
    if (!haveSample && !g_sensors.hx711Present() &&
        !g_sensors.tofPresent() &&
        (uint32_t)(now - lastProcessMs) >= kFramePeriodMs) {
      haveSample = true;
    }

    if (haveSample) {
      g_engine.process(sample);
      lastProcessMs = now;
    }

    g_feedback.update(g_engine.live());
    if (g_metro.tick(now)) {
      g_feedback.beep(now, 25);
    }
    g_feedback.tick(now);

    pollEvents();
    net::setDiagSensor(g_engine.live().sps, g_sensors.hx711Present(),
                       g_sensors.lastRawAdc(), g_sensors.hxOffset(),
                       g_sensors.scale(), g_sensors.hxRebootCount(),
                       (uint8_t)g_sensors.hxStatus());

    if (!haveFrameTime ||
        (uint32_t)(now - lastFrameMs) >= kFramePeriodMs) {
      sendFrame();
      lastFrameMs = now;
      haveFrameTime = true;
    }

    if (g_engine.live().lowSps && !g_lowSpsWarned) {
      queueLine("# HX711 sample rate is low, check the RATE pin and wiring",
                true);
      g_lowSpsWarned = true;
    }

    if ((uint32_t)(now - lastRateCheckMs) >= 1000) {
      lastRateCheckMs = now;
      const uint8_t sps = g_engine.live().sps;
      bool wantFast = fastFilter;
      if (sps >= 1 && sps <= 20) {
        wantFast = true;
      } else if (sps > 30) {
        wantFast = false;
      }
      if (wantFast != fastFilter) {
        fastFilter = wantFast;
        g_engine.setFastFilter(fastFilter);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

} // namespace

void setup() {
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  if (!storage::loadCalibration(g_cal)) {
    cpr::calibDefaults(g_cal);
  }

  cpr::userThDefaults(g_th);
  if (isfinite(g_cal.goodMinKg) && isfinite(g_cal.deepMinKg) &&
      g_cal.goodMinKg > 0.0f && g_cal.deepMinKg > 0.0f) {
    cpr::UserTh oldBands = g_th;
    oldBands.good = g_cal.goodMinKg;
    oldBands.deep = g_cal.deepMinKg;
    if (cpr::userThValid(oldBands)) g_th = oldBands;
  }
  cpr::UserTh savedTh;
  if (storage::loadUserTh(savedTh)) g_th = savedTh;

  applyUserTh();
  g_sensors.begin(g_cal.scaleFactor);
  g_engine.begin(g_cfg, g_cal);
  g_engine.setTofPresent(g_sensors.tofPresent());
  g_feedback.begin();

  char wifiPass[64];
  if (!storage::loadWifiPass(wifiPass, sizeof(wifiPass))) {
    strncpy(wifiPass, kDefaultWifiPass, sizeof(wifiPass) - 1);
    wifiPass[sizeof(wifiPass) - 1] = '\0';
  }

  g_outQueue = xQueueCreate(kOutQueueLength, kOutLineLength);
  g_cmdQueue = xQueueCreate(kCmdQueueLength, kCmdLineLength);
  if (g_outQueue == nullptr || g_cmdQueue == nullptr) {
    Serial.println("# queue allocation failed");
    for (;;) delay(1000);
  }

  net::begin(g_outQueue, g_cmdQueue, wifiPass);

  char cfgBuffer[kOutLineLength];
  if (cpr::jsonConfig(cfgBuffer, sizeof(cfgBuffer), g_cfg, g_cal,
                      g_sensors.tofPresent(), "esp32", kFirmwareVersion,
                      g_sensors.hx711Present(), CPR_HAS_FOIL != 0) != 0) {
    net::setConfigLine(cfgBuffer);
  }

  if (xTaskCreatePinnedToCore(sampleTask, "cpr_sample", 8192, nullptr, 5,
                              nullptr, 1) != pdPASS) {
    Serial.println("# sample task failed");
  }
}

void loop() {
  net::loop();
  delay(1);
}

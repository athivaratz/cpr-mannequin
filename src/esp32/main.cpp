#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cpr.h"

#include "feedback.h"
#include "net.h"
#include "sensors.h"
#include "storage.h"

namespace {

constexpr const char* kFirmwareVersion = "1.0.0";
constexpr const char* kDefaultWifiPass = "cprtrain123";
constexpr size_t kOutQueueLength = 24;
constexpr size_t kOutLineLength = 208;
constexpr size_t kCmdQueueLength = 8;
constexpr size_t kCmdLineLength = 64;
constexpr uint32_t kFramePeriodMs = 50;

QueueHandle_t g_outQueue = nullptr;
QueueHandle_t g_cmdQueue = nullptr;

cpr::Engine g_engine;
cpr::Config g_cfg;
cpr::CalibrationData g_cal;
cpr::Mode g_mode = cpr::Mode::Free;
cpr::Metronome g_metro;

Sensors g_sensors;
Feedback g_feedback;

bool g_lowSpsWarned = false;
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

  char buffer[kOutLineLength];
  strncpy(buffer, line, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  const TickType_t wait = important ? portMAX_DELAY : 0;
  xQueueSend(g_outQueue, buffer, wait);
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
                      g_sensors.tofPresent(), "esp32", kFirmwareVersion);
  if (length == 0) return;

  net::setConfigLine(buffer);
  queueLine(buffer, true);
}

void applyCalibrationBands() {
  g_cfg.th.goodMinKg = g_cal.goodMinKg;
  g_cfg.th.deepMinKg = g_cal.deepMinKg;
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
  const bool hxOk = g_sensors.hx711Present() ? readHxTare() : true;
  const bool tofOk = g_sensors.tofPresent() ? readTofRest() : true;
  const bool ok = hxOk && tofOk;
  sendAck("tare", ok, ok ? "ok" : "sensor not ready");
  sendConfig();
}

void handleProfile(uint8_t arg) {
  g_cfg = cpr::defaultConfig((cpr::Profile)arg);
  g_cal.profile = arg;
  applyCalibrationBands();
  g_engine.setConfig(g_cfg);
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
  if (goodKg <= 0.0f || deepKg <= goodKg) {
    sendAck("cal bands", false, "bad bands");
    return;
  }

  g_cfg.th.goodMinKg = goodKg;
  g_cfg.th.deepMinKg = deepKg;
  g_cal.goodMinKg = goodKg;
  g_cal.deepMinKg = deepKg;
  g_engine.setConfig(g_cfg);
  g_engine.setCalibration(g_cal);
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
      applyCalibrationBands();
      g_engine.setConfig(g_cfg);
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

    case cpr::Cmd::CalReset:
      cpr::calibDefaults(g_cal);
      g_cfg = cpr::defaultConfig((cpr::Profile)g_cal.profile);
      applyCalibrationBands();
      g_sensors.setScale(g_cal.scaleFactor);
      g_engine.setCalibration(g_cal);
      g_engine.setConfig(g_cfg);
      sendAck("cal reset", true, "ok");
      sendConfig();
      break;

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
  if (cpr::jsonFrame(buffer, sizeof(buffer), g_engine.live()) != 0) {
    queueLine(buffer, false);
  }
}

void sampleTask(void*) {
  sendConfig();

  uint32_t lastFrameMs = millis();
  uint32_t lastProcessMs = lastFrameMs;
  bool haveFrameTime = true;

  for (;;) {
    const uint32_t now = millis();

    char commandLine[kCmdLineLength];
    while (g_cmdQueue != nullptr &&
           xQueueReceive(g_cmdQueue, commandLine, 0) == pdTRUE) {
      commandLine[kCmdLineLength - 1] = '\0';
      handleCommand(commandLine);
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

    if (!haveFrameTime ||
        (uint32_t)(now - lastFrameMs) >= kFramePeriodMs) {
      sendFrame();
      lastFrameMs = now;
      haveFrameTime = true;
    }

    if (g_engine.live().lowSps && !g_lowSpsWarned) {
      queueLine("# HX711 is running at 10 SPS, see docs: RATE pin", true);
      g_lowSpsWarned = true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

} // namespace

void setup() {
  Serial.begin(115200);

  if (!storage::loadCalibration(g_cal)) {
    cpr::calibDefaults(g_cal);
  }

  g_cfg = cpr::defaultConfig((cpr::Profile)g_cal.profile);
  applyCalibrationBands();

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
                      g_sensors.tofPresent(), "esp32",
                      kFirmwareVersion) != 0) {
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

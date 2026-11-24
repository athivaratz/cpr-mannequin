#include <Arduino.h>

#include <math.h>
#include <string.h>

#include "cpr.h"

#include "feedback.h"
#include "sensors.h"
#include "storage.h"

namespace {

constexpr const char* kFirmwareVersion = "1.0.0";
constexpr uint32_t kFramePeriodMs = 50;
constexpr size_t kLineLength = 208;
constexpr size_t kCmdLength = 64;

cpr::Engine g_engine;
cpr::Config g_cfg;
cpr::CalibrationData g_cal;
cpr::Mode g_mode = cpr::Mode::Free;
cpr::Metronome g_metro;

Sensors g_sensors;
Feedback g_feedback;

// One output line buffer, shared by every JSON message.
char g_line[kLineLength];
char g_cmdLine[kCmdLength];
size_t g_cmdLen = 0;
bool g_cmdOverflow = false;

bool g_lowSpsWarned = false;
float g_lastPeakKg = NAN;

uint32_t g_lastFrameMs = 0;
uint32_t g_lastProcessMs = 0;

void sendLine(const char* line) {
  Serial.println(line);
}

void sendAck(const char* cmd, bool ok, const char* msg) {
  if (cpr::jsonAck(g_line, sizeof(g_line), cmd, ok, msg) != 0) {
    sendLine(g_line);
  }
}

void copyPgm(char* out, size_t cap, const char* src) {
  if (out == nullptr || cap == 0 || src == nullptr) return;
  size_t i = 0;
  for (; i + 1 < cap; ++i) {
    const char c = (char)pgm_read_byte(src + i);
    out[i] = c;
    if (c == '\0') return;
  }
  out[i] = '\0';
}

void sendAckF(const char* cmdP, bool ok, const char* msgP) {
  char cmd[24];
  char msg[32];
  copyPgm(cmd, sizeof(cmd), cmdP);
  copyPgm(msg, sizeof(msg), msgP);
  sendAck(cmd, ok, msg);
}

void sendAckMsgF(const char* cmdP, bool ok, const char* msg) {
  char cmd[24];
  copyPgm(cmd, sizeof(cmd), cmdP);
  sendAck(cmd, ok, msg);
}

void sendConfig() {
  if (cpr::jsonConfig(g_line, sizeof(g_line), g_cfg, g_cal,
                      g_sensors.tofPresent(), "nano", kFirmwareVersion) != 0) {
    sendLine(g_line);
  }
}

// One-decimal formatting without printf, which avr-libc cannot do for floats.
void formatKg1(float kg, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;

  const bool neg = kg < 0.0f;
  if (neg) kg = -kg;

  uint32_t scaled = (uint32_t)(kg * 10.0f + 0.5f);
  uint32_t whole = scaled / 10UL;
  const uint8_t frac = (uint8_t)(scaled % 10UL);

  char digits[11];
  uint8_t count = 0;
  if (whole == 0) {
    digits[count++] = '0';
  } else {
    while (whole != 0 && count < sizeof(digits)) {
      digits[count++] = (char)('0' + (whole % 10UL));
      whole /= 10UL;
    }
  }

  size_t pos = 0;
  if (neg && pos + 1 < cap) out[pos++] = '-';
  while (count > 0 && pos + 1 < cap) out[pos++] = digits[--count];
  if (pos + 1 < cap) out[pos++] = '.';
  if (pos + 1 < cap) out[pos++] = (char)('0' + frac);
  out[pos < cap ? pos : cap - 1] = '\0';
}

void applyCalibrationBands() {
  g_cfg.th.goodMinKg = g_cal.goodMinKg;
  g_cfg.th.deepMinKg = g_cal.deepMinKg;
}

void handleTare() {
  const bool hxOk = g_sensors.hx711Present() ? g_sensors.tareLoadCell() : true;
  const bool tofOk = g_sensors.tofPresent() ? g_sensors.readRestDistance() : true;
  const bool ok = hxOk && tofOk;
  if (ok) {
    sendAckF(PSTR("tare"), true, PSTR("ok"));
  } else {
    sendAckF(PSTR("tare"), false, PSTR("sensor not ready"));
  }
  sendConfig();
}

void handleProfile(uint8_t arg) {
  g_cfg = cpr::defaultConfig((cpr::Profile)arg);
  g_cal.profile = arg;
  applyCalibrationBands();
  g_engine.setConfig(g_cfg);
  g_engine.setCalibration(g_cal);
  sendAckF(PSTR("profile"), true, PSTR("ok"));
  sendConfig();
}

void handleCalWeight(float kg) {
  if (!g_sensors.hx711Present()) {
    sendAckF(PSTR("cal weight"), false, PSTR("no hx711"));
    return;
  }
  if (kg <= 0.0f) {
    sendAckF(PSTR("cal weight"), false, PSTR("bad weight"));
    return;
  }

  float newScale = 0.0f;
  if (!g_sensors.calibrateWeight(kg, newScale)) {
    sendAckF(PSTR("cal weight"), false, PSTR("reading failed"));
    return;
  }

  g_cal.scaleFactor = newScale;
  g_engine.setCalibration(g_cal);
  sendAckF(PSTR("cal weight"), true, PSTR("ok"));
  sendConfig();
}

void handleCalBands(float goodKg, float deepKg) {
  if (goodKg <= 0.0f || deepKg <= goodKg) {
    sendAckF(PSTR("cal bands"), false, PSTR("bad bands"));
    return;
  }

  g_cfg.th.goodMinKg = goodKg;
  g_cfg.th.deepMinKg = deepKg;
  g_cal.goodMinKg = goodKg;
  g_cal.deepMinKg = deepKg;
  g_engine.setConfig(g_cfg);
  g_engine.setCalibration(g_cal);
  sendAckF(PSTR("cal bands"), true, PSTR("ok"));
  sendConfig();
}

void handleCalPeak() {
  if (isnan(g_lastPeakKg)) {
    sendAckF(PSTR("cal peak"), false, PSTR("no peak"));
    return;
  }

  char msg[16];
  formatKg1(g_lastPeakKg, msg, sizeof(msg));
  sendAckMsgF(PSTR("cal peak"), true, msg);
}

void handleCommand(const char* line) {
  if (line == nullptr) return;

  cpr::Command command;
  if (!cpr::parseCommand(line, command)) {
    sendAckF(PSTR("unknown"), false, PSTR("unknown command"));
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
      sendAckF(PSTR("mode"), true, PSTR("ok"));
      break;

    case cpr::Cmd::Start:
      g_engine.startSession(now);
      sendAckF(PSTR("start"), true, PSTR("ok"));
      break;

    case cpr::Cmd::Stop:
      g_engine.endSession(now);
      sendAckF(PSTR("stop"), true, PSTR("ok"));
      break;

    case cpr::Cmd::MetroOn:
      g_metro.start(now, g_cfg.metronomeBpm);
      sendAckF(PSTR("metro"), true, PSTR("on"));
      break;

    case cpr::Cmd::MetroOff:
      g_metro.stop();
      sendAckF(PSTR("metro"), true, PSTR("off"));
      break;

    case cpr::Cmd::CalStart:
      if (!g_sensors.tofPresent()) {
        sendAckF(PSTR("cal start"), false, PSTR("no tof"));
      } else {
        g_engine.calStart();
        sendAckF(PSTR("cal start"), true, PSTR("ok"));
      }
      break;

    case cpr::Cmd::CalFinish: {
      cpr::CalibrationData fitted;
      if (!g_engine.calFinish(fitted)) {
        sendAckF(PSTR("cal finish"), false, PSTR("need 2 points"));
        break;
      }
      g_cal = fitted;
      g_engine.setCalibration(g_cal);
      applyCalibrationBands();
      g_engine.setConfig(g_cfg);
      sendAckF(PSTR("cal finish"), true, PSTR("ok"));
      sendConfig();
      break;
    }

    case cpr::Cmd::CalCancel:
      g_engine.calCancel();
      sendAckF(PSTR("cal cancel"), true, PSTR("ok"));
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
      if (ok) {
        sendAckF(PSTR("cal save"), true, PSTR("ok"));
      } else {
        sendAckF(PSTR("cal save"), false, PSTR("write failed"));
      }
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
      sendAckF(PSTR("cal reset"), true, PSTR("ok"));
      sendConfig();
      break;

    case cpr::Cmd::Config:
      sendAckF(PSTR("config"), true, PSTR("ok"));
      sendConfig();
      break;

    case cpr::Cmd::WifiPass:
      sendAckF(PSTR("wifi pass"), false, PSTR("not on nano"));
      break;

    case cpr::Cmd::None:
    case cpr::Cmd::Unknown:
    default:
      sendAckF(PSTR("unknown"), false, PSTR("unknown command"));
      break;
  }
}

void processSerial() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    const char ch = (char)value;

    if (ch == '\n' || ch == '\r') {
      if (g_cmdOverflow) {
        sendAckF(PSTR("unknown"), false, PSTR("unknown command"));
      } else if (g_cmdLen > 0) {
        g_cmdLine[g_cmdLen] = '\0';
        handleCommand(g_cmdLine);
      }
      g_cmdLen = 0;
      g_cmdOverflow = false;
      continue;
    }

    if (g_cmdOverflow) continue;

    if (g_cmdLen + 1 < kCmdLength) {
      g_cmdLine[g_cmdLen++] = ch;
    } else {
      // Too long for the buffer: drop the line, keep reading until newline.
      g_cmdOverflow = true;
      g_cmdLen = 0;
    }
  }
}

void pollEvents() {
  const uint32_t now = millis();
  cpr::Event event;
  while (g_engine.pollEvent(event)) {
    if (cpr::jsonEvent(g_line, sizeof(g_line), event) != 0) {
      sendLine(g_line);
    }

    if (event.type == cpr::EventType::Compression) {
      g_lastPeakKg =
          (event.c.source == cpr::Source::ForceKg) ? event.c.peak : NAN;
    }

    // While the metronome runs its 25 ms ticks are the only compression
    // sound; overload still warns.
    if (event.type == cpr::EventType::Overload || !g_metro.running()) {
      g_feedback.onEvent(event, now);
    }

    if (event.type == cpr::EventType::SessionEnd) {
      if (cpr::jsonSummary(g_line, sizeof(g_line), g_engine.stats(), g_mode,
                           g_cfg.profile) != 0) {
        sendLine(g_line);
      }
    }
  }
}

void sendFrame() {
  if (cpr::jsonFrame(g_line, sizeof(g_line), g_engine.live()) != 0) {
    sendLine(g_line);
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

  sendConfig();

  g_lastFrameMs = millis();
  g_lastProcessMs = g_lastFrameMs;
}

void loop() {
  const uint32_t now = millis();

  processSerial();

  cpr::Sample sample;
  bool haveSample = g_sensors.read(now, sample);

  // With no sensors at all, keep the dashboard alive with empty samples.
  if (!haveSample && !g_sensors.hx711Present() && !g_sensors.tofPresent() &&
      (uint32_t)(now - g_lastProcessMs) >= kFramePeriodMs) {
    haveSample = true;
  }

  if (haveSample) {
    g_engine.process(sample);
    g_lastProcessMs = now;
  }

  g_feedback.update(g_engine.live());
  if (g_metro.tick(now)) {
    g_feedback.beep(now, 25);
  }
  g_feedback.tick(now);

  pollEvents();

  if ((uint32_t)(now - g_lastFrameMs) >= kFramePeriodMs) {
    sendFrame();
    g_lastFrameMs = now;
  }

  if (g_engine.live().lowSps && !g_lowSpsWarned) {
    Serial.println(F("# HX711 is running at 10 SPS, see docs: RATE pin"));
    g_lowSpsWarned = true;
  }
}

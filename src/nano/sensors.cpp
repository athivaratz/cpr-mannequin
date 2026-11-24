#include "sensors.h"

#include <math.h>

#include "pins.h"

#if defined(USE_VL53L0X) && USE_VL53L0X
#include <Wire.h>
#endif

namespace {

// Engineering value, tunable: far beyond the useful plate travel; readings
// above this are treated as invalid.
constexpr uint16_t kMaxValidRangeMm = 8000;

} // namespace

Sensors::Sensors()
    : scale_(1.0f),
      hxPresent_(false),
      tofPresent_(false),
      haveDepth_(false),
      restMm_(0),
      lastDepthMm_(0) {}

void Sensors::begin(float scaleFactor) {
  pinMode(pins::kFoil, INPUT_PULLUP);

  scale_ = scaleFactor;
  hx_.begin(pins::kHx711Dout, pins::kHx711Sck);
  hx_.set_scale(scale_);

  // The HX711 needs a moment after power-up. Then give it up to two seconds
  // to show a ready signal before deciding it is not connected.
  delay(800);
  const uint32_t waitStart = millis();
  while (!hx_.is_ready() && (uint32_t)(millis() - waitStart) < 1200) {
    delay(10);
  }

  if (hx_.is_ready()) {
    hxPresent_ = true;
    if (!tareLoadCell()) {
      Serial.println(F("# HX711 tare failed"));
    }
  } else {
    hxPresent_ = false;
    Serial.println(F("# HX711 not found"));
  }

#if defined(USE_VL53L0X) && USE_VL53L0X
  tofPresent_ = false;
  Wire.begin();
  Wire.setClock(400000);
  tof_.setBus(&Wire);
  tof_.setTimeout(100);

  bool tofOk = tof_.init(true);
  if (tofOk) {
    tofOk = tof_.setMeasurementTimingBudget(20000);
  }
  if (!tofOk) {
    Serial.println(F("# VL53L0X not found"));
  } else {
    tof_.startContinuous(0);
    uint16_t rest = 0;
    if (averageTofMm(10, 100, rest)) {
      restMm_ = rest;
      tofPresent_ = true;
    } else {
      Serial.println(F("# VL53L0X no reading"));
    }
  }
#endif
}

void Sensors::setScale(float scale) {
  scale_ = scale;
  hx_.set_scale(scale_);
}

bool Sensors::read(uint32_t tMs, cpr::Sample& s) {
  s.tMs = tMs;
  s.kg = NAN;
  s.mm = -1;
  s.handOk = digitalRead(pins::kFoil) == LOW;

  bool haveSample = false;

  if (hxPresent_) {
    if (hx_.is_ready()) {
      const float kg = hx_.get_units(1);
      if (isfinite(kg)) {
        s.kg = kg;
      }
      haveSample = true;
    }
  }

  if (!hxPresent_) {
    int16_t depth = 0;
    if (tofPresent_ && readTofDepth(depth)) {
      lastDepthMm_ = depth;
      haveDepth_ = true;
      s.mm = depth;
      haveSample = true;
    }
  } else if (haveSample && tofPresent_) {
    int16_t depth = 0;
    if (readTofDepth(depth)) {
      lastDepthMm_ = depth;
      haveDepth_ = true;
    }
    // At the HX711 sample rate some samples arrive between ToF measurements.
    // Carry the newest depth so the engine does not flip source every time.
    if (haveDepth_) {
      s.mm = lastDepthMm_;
    }
  }

  return haveSample;
}

bool Sensors::tareLoadCell(uint32_t timeoutMs) {
  if (!hxPresent_) return false;

  long sum = 0;
  uint8_t count = 0;
  const uint32_t start = millis();
  while (count < 10 && (uint32_t)(millis() - start) < timeoutMs) {
    if (hx_.is_ready()) {
      sum += hx_.read();
      count++;
    } else {
      delay(5);
    }
  }

  if (count < 3) return false;
  hx_.set_offset(sum / (long)count);
  return true;
}

bool Sensors::readRestDistance(uint32_t timeoutPerReadMs) {
  if (!tofPresent_) return false;

  uint16_t rest = 0;
  if (!averageTofMm(10, (uint16_t)timeoutPerReadMs, rest)) return false;
  restMm_ = rest;
  haveDepth_ = false;
  lastDepthMm_ = 0;
  return true;
}

bool Sensors::calibrateWeight(float knownKg, float& newScale) {
  if (!hxPresent_ || knownKg <= 0.0f) return false;

  double sum = 0.0;
  uint8_t count = 0;
  const uint32_t start = millis();
  while (count < 20 && (uint32_t)(millis() - start) < 3000) {
    if (hx_.is_ready()) {
      sum += (double)hx_.read();
      count++;
    } else {
      delay(5);
    }
  }

  if (count < 10) return false;

  const double rawAverage = sum / (double)count;
  const double tared = rawAverage - (double)hx_.get_offset();
  const float candidate = (float)(tared / (double)knownKg);
  if (!isfinite(candidate) || fabsf(candidate) < 0.001f) return false;

  setScale(candidate);
  newScale = candidate;
  return true;
}

bool Sensors::readTofDepth(int16_t& depthMm) {
#if defined(USE_VL53L0X) && USE_VL53L0X
  if (!tofPresent_) return false;

  if ((tof_.readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
    return false;
  }

  const uint16_t range =
      tof_.readReg16Bit((uint8_t)(VL53L0X::RESULT_RANGE_STATUS + 10));
  tof_.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);

  if (range == 0 || range == 65535 || range >= kMaxValidRangeMm) {
    return false;
  }

  int32_t depth = (int32_t)restMm_ - (int32_t)range;
  if (depth < 0) depth = 0;
  if (depth > 32767) depth = 32767;
  depthMm = (int16_t)depth;
  return true;
#else
  (void)depthMm;
  return false;
#endif
}

bool Sensors::averageTofMm(uint16_t samples, uint16_t timeoutPerReadMs,
                           uint16_t& outMm) {
#if defined(USE_VL53L0X) && USE_VL53L0X
  if (samples == 0) return false;

  uint32_t sum = 0;
  uint16_t count = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    tof_.setTimeout(timeoutPerReadMs);
    const uint16_t range = tof_.readRangeContinuousMillimeters();
    if (range != 0 && range != 65535 && range < kMaxValidRangeMm) {
      sum += range;
      count++;
    }
  }

  const uint16_t needed = (uint16_t)(samples / 2 + 1);
  if (count < needed) return false;

  outMm = (uint16_t)((sum + count / 2) / count);
  return true;
#else
  (void)samples;
  (void)timeoutPerReadMs;
  (void)outMm;
  return false;
#endif
}

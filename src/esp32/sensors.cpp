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

constexpr uint32_t kHxReadyWaitMs = 3000;
constexpr uint16_t kHxReadTimeoutMs = 200;
constexpr uint8_t kHxDiscardReads = 4;
constexpr uint8_t kHxTareReads = 8;
constexpr long kHxSatHigh = 8388607L;
constexpr long kHxSatLow = -8388608L;
constexpr uint32_t kHxLostMs = 1000;
constexpr uint32_t kHxProbeMs = 1000;
constexpr uint32_t kHxPulseMs = 5000;
constexpr long kTareMinCounts = 200;       // engineering value, tunable
constexpr float kTareScaleFrac = 0.05f;    // engineering value, tunable

} // namespace

Sensors::Sensors()
    : scale_(1.0f),
      hxPresent_(false),
      tofPresent_(false),
      haveDepth_(false),
      hasOffset_(false),
      restMm_(0),
      lastDepthMm_(0),
      hxState_(HxState::Probe),
      lastReadyMs_(0),
      lastProbeMs_(0),
      lastPulseMs_(0),
      lastProgressMs_(0),
      tareStartMs_(0),
      satCount_(0),
      discardLeft_(0),
      tareStage_(0),
      tareReads_(0),
      tareAttempts_(0),
      tareSum_(0),
      tareAvg1_(0),
      tareLastAvg_(0) {}

void Sensors::begin(float scaleFactor) {
#if CPR_HAS_FOIL
  pinMode(pins::kFoil, INPUT_PULLUP);
#endif

  scale_ = scaleFactor;
  hx_.begin(pins::kHx711Dout, pins::kHx711Sck);
  hx_.set_scale(scale_);

  hx_.power_down();
  delayMicroseconds(100);
  hx_.power_up();

  const uint32_t waitStart = millis();
  while (!hx_.is_ready() &&
         (uint32_t)(millis() - waitStart) < kHxReadyWaitMs) {
    delay(10);
  }

  if (hx_.is_ready()) {
    hxPresent_ = true;
    lastReadyMs_ = millis();
    discardConversions(kHxDiscardReads, kHxReadTimeoutMs);
    if (!tareLoadCell(kHxReadyWaitMs)) {
      Serial.println("# HX711 tare failed");
    }
  } else {
    hxPresent_ = false;
    Serial.println("# HX711 not found");
  }

  const uint32_t now = millis();
  hxState_ = hxPresent_ ? HxState::Present : HxState::Probe;
  lastProbeMs_ = now;
  lastPulseMs_ = now;

#if defined(USE_VL53L0X) && USE_VL53L0X
  tofPresent_ = false;
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(400000);
  tof_.setBus(&Wire);
  tof_.setTimeout(100);

  bool tofOk = tof_.init(true);
  if (tofOk) {
    tofOk = tof_.setMeasurementTimingBudget(20000);
  }
  if (!tofOk) {
    Serial.println("# VL53L0X not found");
  } else {
    tof_.startContinuous(0);
    uint16_t rest = 0;
    if (averageTofMm(10, 100, rest)) {
      restMm_ = rest;
      tofPresent_ = true;
    } else {
      Serial.println("# VL53L0X no reading");
    }
  }
#endif
}

void Sensors::setScale(float scale) {
  scale_ = scale;
  hx_.set_scale(scale_);
}

long Sensors::stableThreshold() const {
  long threshold = kTareMinCounts;
  const float scaled = fabsf(scale_) * kTareScaleFrac;
  if (scaled > (float)threshold) threshold = (long)scaled;
  return threshold;
}

bool Sensors::readRawAverage(uint8_t samples, uint16_t timeoutPerReadMs,
                             long& out) {
  if (samples == 0) return false;

  long sum = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    const uint32_t start = millis();
    while (!hx_.is_ready()) {
      if ((uint32_t)(millis() - start) >= timeoutPerReadMs) break;
      delay(1);
    }
    if (!hx_.is_ready()) return false;
    sum += hx_.read();
    count++;
  }

  if (count == 0) return false;
  out = sum / (long)count;
  return true;
}

bool Sensors::discardConversions(uint8_t samples, uint16_t timeoutPerReadMs) {
  for (uint8_t i = 0; i < samples; ++i) {
    const uint32_t start = millis();
    while (!hx_.is_ready()) {
      if ((uint32_t)(millis() - start) >= timeoutPerReadMs) return false;
      delay(1);
    }
    hx_.read();
  }
  return true;
}

bool Sensors::stableTare(uint32_t budgetMs, uint16_t timeoutPerReadMs,
                         long& offset) {
  const uint32_t start = millis();
  const long threshold = stableThreshold();
  bool haveLast = false;
  long lastAverage = 0;

  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    long average1 = 0;
    long average2 = 0;
    if (!readRawAverage(kHxTareReads, timeoutPerReadMs, average1)) {
      return false;
    }
    if (!readRawAverage(kHxTareReads, timeoutPerReadMs, average2)) {
      return false;
    }

    long diff = average1 - average2;
    if (diff < 0) diff = -diff;
    lastAverage = (average1 + average2) / 2;
    haveLast = true;

    if (diff <= threshold) {
      offset = lastAverage;
      return true;
    }
    if ((uint32_t)(millis() - start) >= budgetMs) break;
  }

  if (!haveLast) return false;
  offset = lastAverage;
  Serial.println("# HX711 tare unstable");
  return true;
}

void Sensors::startRecovery(uint32_t nowMs) {
  hxState_ = HxState::Discard;
  discardLeft_ = kHxDiscardReads;
  tareStage_ = 0;
  tareReads_ = 0;
  tareAttempts_ = 0;
  tareSum_ = 0;
  tareAvg1_ = 0;
  tareLastAvg_ = 0;
  tareStartMs_ = nowMs;
  lastProgressMs_ = nowMs;
}

bool Sensors::service(uint32_t nowMs) {
  if (hxPresent_) {
    if (hx_.is_ready()) {
      lastReadyMs_ = nowMs;
    }

    // lastReadyMs_ can be newer than nowMs when a command (tare, cal weight)
    // stamped it from millis() after this loop captured nowMs. A signed
    // difference keeps that from wrapping into a false loss.
    const bool lost =
        (int32_t)(nowMs - lastReadyMs_) > (int32_t)kHxLostMs || satCount_ >= 5;
    if (lost) {
      hxPresent_ = false;
      satCount_ = 0;
      hxState_ = HxState::Probe;
      lastProbeMs_ = nowMs;
      lastPulseMs_ = nowMs;
      return true;
    }
    return false;
  }

  if (hxState_ == HxState::Probe &&
      (uint32_t)(nowMs - lastPulseMs_) >= kHxPulseMs) {
    lastPulseMs_ = nowMs;
    hx_.power_down();
    delayMicroseconds(100);
    hx_.power_up();
    lastProbeMs_ = nowMs;
  }

  switch (hxState_) {
    case HxState::Probe:
      if ((uint32_t)(nowMs - lastProbeMs_) < kHxProbeMs) return false;
      lastProbeMs_ = nowMs;
      if (!hx_.is_ready()) return false;
      startRecovery(nowMs);
      return false;

    case HxState::Discard:
      if (!hx_.is_ready()) {
        if ((uint32_t)(nowMs - lastProgressMs_) >= kHxLostMs) {
          hxState_ = HxState::Probe;
          lastProbeMs_ = nowMs;
        }
        return false;
      }
      hx_.read();
      lastProgressMs_ = nowMs;
      if (discardLeft_ > 0) discardLeft_--;
      if (discardLeft_ == 0) {
        // The discard flushes unsettled conversions. If the cell was already
        // tared, keep that offset and go straight back to Present: a new tare
        // here could zero out a real load left on the plate.
        if (hasOffset_) {
          hxPresent_ = true;
          hxState_ = HxState::Present;
          lastReadyMs_ = nowMs;
          satCount_ = 0;
          return true;
        }
        hxState_ = HxState::Tare;
        tareStage_ = 0;
        tareReads_ = 0;
        tareSum_ = 0;
        tareAttempts_ = 0;
        tareStartMs_ = nowMs;
      }
      return false;

    case HxState::Tare: {
      if (!hx_.is_ready()) {
        if ((uint32_t)(nowMs - lastProgressMs_) >= kHxLostMs) {
          hxState_ = HxState::Probe;
          lastProbeMs_ = nowMs;
        }
        return false;
      }
      tareSum_ += hx_.read();
      lastProgressMs_ = nowMs;
      tareReads_++;

      if (tareStage_ == 0) {
        if (tareReads_ >= kHxTareReads) {
          tareAvg1_ = tareSum_ / (long)kHxTareReads;
          tareSum_ = 0;
          tareReads_ = 0;
          tareStage_ = 1;
        }
        return false;
      }

      if (tareReads_ < kHxTareReads) return false;

      const long average2 = tareSum_ / (long)kHxTareReads;
      tareSum_ = 0;
      tareReads_ = 0;
      tareLastAvg_ = (tareAvg1_ + average2) / 2;

      long diff = tareAvg1_ - average2;
      if (diff < 0) diff = -diff;
      if (diff <= stableThreshold()) {
        hx_.set_offset(tareLastAvg_);
        hasOffset_ = true;
        hxPresent_ = true;
        hxState_ = HxState::Present;
        lastReadyMs_ = nowMs;
        satCount_ = 0;
        return true;
      }

      tareAttempts_++;
      if (tareAttempts_ >= 3 ||
          (uint32_t)(nowMs - tareStartMs_) >= kHxReadyWaitMs) {
        hx_.set_offset(tareLastAvg_);
        hasOffset_ = true;
        Serial.println("# HX711 tare unstable");
        hxPresent_ = true;
        hxState_ = HxState::Present;
        lastReadyMs_ = nowMs;
        satCount_ = 0;
        return true;
      }

      tareStage_ = 0;
      return false;
    }

    case HxState::Present:
    default:
      return false;
  }
}

bool Sensors::read(uint32_t tMs, cpr::Sample& s) {
  s.tMs = tMs;
  s.kg = NAN;
  s.mm = -1;
#if CPR_HAS_FOIL
  s.handOk = digitalRead(pins::kFoil) == LOW;
#else
  s.handOk = true;
#endif

  bool haveSample = false;

  if (hxPresent_) {
    if (hx_.is_ready()) {
      lastReadyMs_ = tMs;
      // Read the raw value so a saturated cell can be spotted; this is the
      // same one-reading conversion as get_units(1).
      const long raw = hx_.read();
      if (raw >= kHxSatHigh || raw <= kHxSatLow) {
        if (satCount_ < 255) satCount_++;
      } else {
        satCount_ = 0;
      }

      const float currentScale = hx_.get_scale();
      if (currentScale != 0.0f) {
        const float kg = (float)(raw - hx_.get_offset()) / currentScale;
        if (isfinite(kg)) {
          s.kg = kg;
        }
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

  long offset = 0;
  if (!stableTare(timeoutMs, kHxReadTimeoutMs, offset)) return false;
  hx_.set_offset(offset);
  hasOffset_ = true;
  lastReadyMs_ = millis();
  satCount_ = 0;
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

  lastReadyMs_ = millis();
  if (count < 10) return false;
  satCount_ = 0;

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

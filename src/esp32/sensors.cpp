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
constexpr uint32_t kHxPowerUpDelayMs = 400; // engineering value, tunable: let 3.3V rail and HX711 settle on cold boot
constexpr uint32_t kHxResetPulseMs = 2;     // engineering value, tunable: PD_SCK HIGH duration (>60 us) to reset HX711
constexpr uint32_t kHxPostResetWaitMs = 50; // engineering value, tunable: wait after PD_SCK LOW before polling DOUT
constexpr uint16_t kHxReadTimeoutMs = 500;  // engineering value, tunable: >400 ms HX711 10-SPS reset settling time
constexpr uint8_t kHxBootMaxAttempts = 3;   // engineering value, tunable: boot power-cycle/tare/verify retries
constexpr uint8_t kHxDiscardReads = 4;
constexpr uint8_t kHxTareReads = 8;
constexpr uint8_t kHxZeroCheckSamples = 4;  // engineering value, tunable: post-tare zero-range verification samples
constexpr uint8_t kHxStuckZeroLimit = 5;    // engineering value, tunable: consecutive exact-zero raw reads mean dead bus
constexpr float kHxZeroCheckMinKg = -1.0f;  // engineering value, tunable: allowed resting kg lower bound
constexpr float kHxZeroCheckMaxKg = 1.0f;   // engineering value, tunable: allowed resting kg upper bound
constexpr long kHxSatHigh = 8388607L;
constexpr long kHxSatLow = -8388608L;
constexpr uint32_t kHxLostMs = 1000;
constexpr uint32_t kHxProbeMs = 1000;
constexpr uint32_t kHxPulseMs = 5000;
constexpr long kTareMinCounts = 200;       // engineering value, tunable
constexpr float kTareScaleFrac = 0.05f;    // engineering value, tunable

bool isRawSaturated(long raw) {
  return raw >= kHxSatHigh || raw <= kHxSatLow;
}

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
      hxStatus_(HxStatus::NotFound),
      lastRawKg_(NAN),
      lastRawAdc_(0),
      hxRebootCount_(0),
      lastReadyMs_(0),
      lastProbeMs_(0),
      lastPulseMs_(0),
      lastProgressMs_(0),
      tareStartMs_(0),
      satCount_(0),
      zeroRawCount_(0),
      discardLeft_(0),
      tareStage_(0),
      tareReads_(0),
      tareAttempts_(0),
      verifyReads_(0),
      verifyTared_(false),
      verifyOk_(true),
      tareSum_(0),
      tareAvg1_(0),
      tareLastAvg_(0) {}

void Sensors::powerCycleHx711(bool countReboot) {
  hx_.power_down();
  delay(kHxResetPulseMs);
  hx_.power_up();
  if (countReboot && hxRebootCount_ < 65535) {
    hxRebootCount_++;
  }
}

bool Sensors::readHxSample(long& raw) {
  raw = hx_.read();
  lastRawAdc_ = raw;
  // Per HX711 datasheet, the 25th PD_SCK pulse pulls DOUT back HIGH until the
  // next conversion completes (>=12.5 ms at 80 SPS). DOUT staying LOW right
  // after read() means the line is shorted/unpowered or the chip is dead.
  delayMicroseconds(10);
  if (digitalRead(pins::kHx711Dout) == LOW) {
    return false;
  }
  if (isRawSaturated(raw)) {
    return false;
  }
  return true;
}

void Sensors::begin(float scaleFactor) {
#if CPR_HAS_FOIL
  pinMode(pins::kFoil, INPUT_PULLUP);
#endif

  scale_ = scaleFactor;
  hx_.begin(pins::kHx711Dout, pins::kHx711Sck);
  hx_.set_scale(scale_);

  // Let the 3.3 V rail and HX711 settle after cold power-on before pulsing
  // PD_SCK to reset the ADC.
  delay(kHxPowerUpDelayMs);
  powerCycleHx711(false);
  delay(kHxPostResetWaitMs);

  bool bootOk = false;
  for (uint8_t attempt = 0; attempt < kHxBootMaxAttempts; ++attempt) {
    if (attempt > 0) {
      powerCycleHx711(true);
      delay(kHxPowerUpDelayMs);
    }

    const uint32_t waitStart = millis();
    while (!hx_.is_ready() &&
           (uint32_t)(millis() - waitStart) < kHxReadyWaitMs) {
      delay(10);
    }

    if (!hx_.is_ready()) {
      hxPresent_ = false;
      hxStatus_ = HxStatus::NotFound;
      lastRawKg_ = NAN;
      Serial.println("# HX711 not found");
      continue;
    }

    hxPresent_ = true;
    lastReadyMs_ = millis();

    long offset = 0;
    if (!discardConversions(kHxDiscardReads, kHxReadTimeoutMs) ||
        !stableTare(kHxReadyWaitMs, kHxReadTimeoutMs, offset)) {
      hxPresent_ = false;
      hasOffset_ = false;
      hxStatus_ = HxStatus::ZeroFail;
      lastRawKg_ = NAN;
      Serial.println("# HX711 tare failed, rebooting");
      continue;
    }

    hx_.set_offset(offset);
    hasOffset_ = true;

    float avgKg = NAN;
    if (verifyZeroRange(kHxReadTimeoutMs, avgKg)) {
      bootOk = true;
      hxStatus_ = HxStatus::Ok;
      lastReadyMs_ = millis();
      satCount_ = 0;
      break;
    }

    hxPresent_ = false;
    hasOffset_ = false;
    hxStatus_ = HxStatus::ZeroFail;
    char msg[64];
    if (isfinite(avgKg)) {
      snprintf(msg, sizeof(msg), "# HX711 zero check failed (%.2f kg), rebooting",
               (double)avgKg);
    } else {
      snprintf(msg, sizeof(msg), "# HX711 zero check failed (invalid), rebooting");
    }
    Serial.println(msg);
  }

  const uint32_t now = millis();
  hxState_ = bootOk ? HxState::Present : HxState::Probe;
  if (!bootOk) {
    hxPresent_ = false;
    lastRawKg_ = NAN;
  }
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
  bool anyNonZero = false;
  for (uint8_t i = 0; i < samples; ++i) {
    const uint32_t start = millis();
    while (!hx_.is_ready()) {
      if ((uint32_t)(millis() - start) >= timeoutPerReadMs) break;
      delay(1);
    }
    if (!hx_.is_ready()) return false;
    long raw = 0;
    if (!readHxSample(raw)) return false;
    if (raw != 0) anyNonZero = true;
    sum += raw;
    count++;
  }

  if (count == 0 || !anyNonZero) return false;
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
    long raw = 0;
    if (!readHxSample(raw)) return false;
  }
  return true;
}

bool Sensors::stableTare(uint32_t budgetMs, uint16_t timeoutPerReadMs,
                         long& offset) {
  const uint32_t start = millis();
  const long threshold = stableThreshold();

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
    const long avg = (average1 + average2) / 2;

    if (diff <= threshold) {
      offset = avg;
      return true;
    }
    if ((uint32_t)(millis() - start) >= budgetMs) break;
  }

  Serial.println("# HX711 tare unstable");
  return false;
}

bool Sensors::verifyZeroRange(uint16_t timeoutPerReadMs, float& outAvgKg) {
  outAvgKg = NAN;
  const float currentScale = hx_.get_scale();
  if (currentScale == 0.0f) return false;

  float sumKg = 0.0f;
  bool allInRange = true;
  bool anyNonZero = false;
  for (uint8_t i = 0; i < kHxZeroCheckSamples; ++i) {
    const uint32_t start = millis();
    while (!hx_.is_ready()) {
      if ((uint32_t)(millis() - start) >= timeoutPerReadMs) return false;
      delay(1);
    }
    long raw = 0;
    if (!readHxSample(raw)) {
      lastRawKg_ = NAN;
      return false;
    }
    if (raw != 0) anyNonZero = true;
    const float kg = (float)(raw - hx_.get_offset()) / currentScale;
    if (!isfinite(kg)) {
      lastRawKg_ = NAN;
      return false;
    }
    lastRawKg_ = kg;
    sumKg += kg;
    if (kg < kHxZeroCheckMinKg || kg > kHxZeroCheckMaxKg) {
      allInRange = false;
    }
  }

  if (!anyNonZero) {
    lastRawKg_ = NAN;
    return false;
  }
  outAvgKg = sumKg / (float)kHxZeroCheckSamples;
  return allInRange && outAvgKg >= kHxZeroCheckMinKg &&
         outAvgKg <= kHxZeroCheckMaxKg;
}

void Sensors::startRecovery(uint32_t nowMs) {
  hxState_ = HxState::Discard;
  hxStatus_ = HxStatus::Recovering;
  discardLeft_ = kHxDiscardReads;
  tareStage_ = 0;
  tareReads_ = 0;
  tareAttempts_ = 0;
  verifyReads_ = 0;
  verifyTared_ = false;
  verifyOk_ = true;
  tareSum_ = 0;
  tareAvg1_ = 0;
  tareLastAvg_ = 0;
  tareStartMs_ = nowMs;
  lastProgressMs_ = nowMs;
}

void Sensors::requestHxReboot(uint32_t nowMs) {
  hxPresent_ = false;
  hasOffset_ = false;
  lastRawKg_ = NAN;
  satCount_ = 0;
  hxStatus_ = HxStatus::Recovering;
  powerCycleHx711(true);
  hxState_ = HxState::Probe;
  lastProbeMs_ = nowMs;
  lastPulseMs_ = nowMs;
  Serial.println("# HX711 manual reboot requested");
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
        (int32_t)(nowMs - lastReadyMs_) > (int32_t)kHxLostMs ||
        satCount_ >= 5 || zeroRawCount_ >= kHxStuckZeroLimit;
    if (lost) {
      hxPresent_ = false;
      lastRawKg_ = NAN;
      satCount_ = 0;
      zeroRawCount_ = 0;
      hxState_ = HxState::Probe;
      hxStatus_ = HxStatus::NotFound;
      lastProbeMs_ = nowMs;
      lastPulseMs_ = nowMs;
      return true;
    }
    return false;
  }

  if (hxState_ == HxState::Probe &&
      (uint32_t)(nowMs - lastPulseMs_) >= kHxPulseMs) {
    lastPulseMs_ = nowMs;
    powerCycleHx711(true);
    lastProbeMs_ = nowMs;
  }

  switch (hxState_) {
    case HxState::Probe:
      if ((uint32_t)(nowMs - lastProbeMs_) < kHxProbeMs) return false;
      lastProbeMs_ = nowMs;
      if (!hx_.is_ready()) return false;
      startRecovery(nowMs);
      return true;

    case HxState::Discard: {
      if (!hx_.is_ready()) {
        if ((uint32_t)(nowMs - lastProgressMs_) >= kHxLostMs) {
          hxState_ = HxState::Probe;
          hxStatus_ = HxStatus::NotFound;
          lastProbeMs_ = nowMs;
          return true;
        }
        return false;
      }
      long raw = 0;
      lastProgressMs_ = nowMs;
      if (!readHxSample(raw)) {
        hxStatus_ = HxStatus::NotFound;
        powerCycleHx711(true);
        hxState_ = HxState::Probe;
        lastProbeMs_ = nowMs;
        lastPulseMs_ = nowMs;
        Serial.println("# HX711 invalid signal during recovery, rebooting");
        return true;
      }
      if (discardLeft_ > 0) discardLeft_--;
      if (discardLeft_ == 0) {
        if (hasOffset_) {
          // Verify the existing offset still reads within -1..+1 kg before
          // declaring the sensor present; re-taring happens if it drifted.
          hxState_ = HxState::Verify;
          verifyReads_ = 0;
          verifyTared_ = false;
          verifyOk_ = true;
          return false;
        }
        hxState_ = HxState::Tare;
        tareStage_ = 0;
        tareReads_ = 0;
        tareSum_ = 0;
        tareAttempts_ = 0;
        tareStartMs_ = nowMs;
      }
      return false;
    }

    case HxState::Tare: {
      if (!hx_.is_ready()) {
        if ((uint32_t)(nowMs - lastProgressMs_) >= kHxLostMs) {
          hxState_ = HxState::Probe;
          hxStatus_ = HxStatus::NotFound;
          lastProbeMs_ = nowMs;
          return true;
        }
        return false;
      }
      long raw = 0;
      lastProgressMs_ = nowMs;
      if (!readHxSample(raw)) {
        hasOffset_ = false;
        hxStatus_ = HxStatus::NotFound;
        powerCycleHx711(true);
        hxState_ = HxState::Probe;
        lastProbeMs_ = nowMs;
        lastPulseMs_ = nowMs;
        Serial.println("# HX711 invalid signal during tare, rebooting");
        return true;
      }
      tareSum_ += raw;
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
        hxState_ = HxState::Verify;
        verifyReads_ = 0;
        verifyTared_ = true;
        verifyOk_ = true;
        return false;
      }

      tareAttempts_++;
      if (tareAttempts_ >= 3 ||
          (uint32_t)(nowMs - tareStartMs_) >= kHxReadyWaitMs) {
        hasOffset_ = false;
        hxStatus_ = HxStatus::ZeroFail;
        Serial.println("# HX711 tare unstable, rebooting");
        powerCycleHx711(true);
        hxState_ = HxState::Probe;
        lastProbeMs_ = nowMs;
        lastPulseMs_ = nowMs;
        return true;
      }

      tareStage_ = 0;
      return false;
    }

    case HxState::Verify: {
      if (!hx_.is_ready()) {
        if ((uint32_t)(nowMs - lastProgressMs_) >= kHxLostMs) {
          hxState_ = HxState::Probe;
          hxStatus_ = HxStatus::NotFound;
          lastProbeMs_ = nowMs;
          return true;
        }
        return false;
      }
      long raw = 0;
      lastProgressMs_ = nowMs;
      const float currentScale = hx_.get_scale();
      if (!readHxSample(raw) || raw == 0 || currentScale == 0.0f) {
        verifyOk_ = false;
      } else {
        const float kg = (float)(raw - hx_.get_offset()) / currentScale;
        lastRawKg_ = isfinite(kg) ? kg : NAN;
        if (!isfinite(kg) || kg < kHxZeroCheckMinKg || kg > kHxZeroCheckMaxKg) {
          verifyOk_ = false;
        }
      }

      verifyReads_++;
      if (verifyReads_ < kHxZeroCheckSamples) return false;

      if (verifyOk_) {
        hxPresent_ = true;
        hxState_ = HxState::Present;
        hxStatus_ = HxStatus::Ok;
        lastReadyMs_ = nowMs;
        satCount_ = 0;
        return true;
      }

      if (!verifyTared_) {
        // Existing offset is out of -1..+1 kg (e.g. re-plugged cable); try
        // a fresh tare once before power-cycling the HX711.
        hxState_ = HxState::Tare;
        tareStage_ = 0;
        tareReads_ = 0;
        tareSum_ = 0;
        tareAttempts_ = 0;
        tareStartMs_ = nowMs;
        return false;
      }

      hasOffset_ = false;
      hxStatus_ = HxStatus::ZeroFail;
      Serial.println("# HX711 zero check failed, rebooting");
      powerCycleHx711(true);
      hxState_ = HxState::Probe;
      lastProbeMs_ = nowMs;
      lastPulseMs_ = nowMs;
      return true;
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
      // Read the raw value and verify DOUT returns HIGH after the 25th pulse.
      long raw = 0;
      if (!readHxSample(raw)) {
        if (satCount_ < 255) satCount_++;
        lastRawKg_ = NAN;
      } else {
        satCount_ = 0;
        if (raw == 0) {
          if (zeroRawCount_ < 255) zeroRawCount_++;
        } else {
          zeroRawCount_ = 0;
        }

        const float currentScale = hx_.get_scale();
        if (currentScale != 0.0f) {
          const float kg = (float)(raw - hx_.get_offset()) / currentScale;
          if (isfinite(kg)) {
            s.kg = kg;
            lastRawKg_ = kg;
          } else {
            lastRawKg_ = NAN;
          }
        } else {
          lastRawKg_ = NAN;
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

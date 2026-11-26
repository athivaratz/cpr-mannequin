#ifndef CPR_ESP32_SENSORS_H
#define CPR_ESP32_SENSORS_H

#include <Arduino.h>

#include "HX711.h"
#include "cpr_types.h"

#if defined(USE_VL53L0X) && USE_VL53L0X
#include <VL53L0X.h>
#endif

enum class HxStatus : uint8_t {
  Ok = 0,
  NotFound = 1,
  Recovering = 2,
  ZeroFail = 3
};

class Sensors {
 public:
  Sensors();

  void begin(float scaleFactor);

  bool hx711Present() const { return hxPresent_; }
  bool tofPresent() const { return tofPresent_; }
  HxStatus hxStatus() const { return hxStatus_; }
  float lastRawKg() const { return lastRawKg_; }
  long lastRawAdc() const { return lastRawAdc_; }
  long hxOffset() { return hx_.get_offset(); }
  uint16_t hxRebootCount() const { return hxRebootCount_; }

  bool service(uint32_t nowMs);
  bool read(uint32_t tMs, cpr::Sample& s);
  void requestHxReboot(uint32_t nowMs);

  bool tareLoadCell(uint32_t timeoutMs = 1500);
  bool readRestDistance(uint32_t timeoutPerReadMs = 100);
  bool calibrateWeight(float knownKg, float& newScale);

  float scale() const { return scale_; }
  void setScale(float scale);

 private:
  enum class HxState : uint8_t { Present, Probe, Discard, Tare, Verify };

  void powerCycleHx711(bool countReboot);
  bool readHxSample(long& raw);
  bool readRawAverage(uint8_t samples, uint16_t timeoutPerReadMs, long& out);
  bool discardConversions(uint8_t samples, uint16_t timeoutPerReadMs);
  bool stableTare(uint32_t budgetMs, uint16_t timeoutPerReadMs, long& offset);
  bool verifyZeroRange(uint16_t timeoutPerReadMs, float& outAvgKg);
  long stableThreshold() const;
  void startRecovery(uint32_t nowMs);
  bool readTofDepth(int16_t& depthMm);
  bool averageTofMm(uint16_t samples, uint16_t timeoutPerReadMs, uint16_t& outMm);

  HX711 hx_;
  float scale_;
  bool hxPresent_;
  bool tofPresent_;
  bool haveDepth_;
  bool hasOffset_;
  uint16_t restMm_;
  int16_t lastDepthMm_;

  HxState hxState_;
  HxStatus hxStatus_;
  float lastRawKg_;
  long lastRawAdc_;
  uint16_t hxRebootCount_;
  uint32_t lastReadyMs_;
  uint32_t lastProbeMs_;
  uint32_t lastPulseMs_;
  uint32_t lastProgressMs_;
  uint32_t tareStartMs_;
  uint8_t satCount_;
  uint8_t zeroRawCount_;
  uint8_t discardLeft_;
  uint8_t tareStage_;
  uint8_t tareReads_;
  uint8_t tareAttempts_;
  uint8_t verifyReads_;
  bool verifyTared_;
  bool verifyOk_;
  long tareSum_;
  long tareAvg1_;
  long tareLastAvg_;

#if defined(USE_VL53L0X) && USE_VL53L0X
  VL53L0X tof_;
#endif
};

#endif

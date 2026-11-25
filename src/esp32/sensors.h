#ifndef CPR_ESP32_SENSORS_H
#define CPR_ESP32_SENSORS_H

#include <Arduino.h>

#include "HX711.h"
#include "cpr_types.h"

#if defined(USE_VL53L0X) && USE_VL53L0X
#include <VL53L0X.h>
#endif

class Sensors {
 public:
  Sensors();

  void begin(float scaleFactor);

  bool hx711Present() const { return hxPresent_; }
  bool tofPresent() const { return tofPresent_; }

  bool service(uint32_t nowMs);
  bool read(uint32_t tMs, cpr::Sample& s);

  bool tareLoadCell(uint32_t timeoutMs = 1500);
  bool readRestDistance(uint32_t timeoutPerReadMs = 100);
  bool calibrateWeight(float knownKg, float& newScale);

  float scale() const { return scale_; }
  void setScale(float scale);

 private:
  enum class HxState : uint8_t { Present, Probe, Discard, Tare };

  bool readRawAverage(uint8_t samples, uint16_t timeoutPerReadMs, long& out);
  bool discardConversions(uint8_t samples, uint16_t timeoutPerReadMs);
  bool stableTare(uint32_t budgetMs, uint16_t timeoutPerReadMs, long& offset);
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
  uint32_t lastReadyMs_;
  uint32_t lastProbeMs_;
  uint32_t lastPulseMs_;
  uint32_t lastProgressMs_;
  uint32_t tareStartMs_;
  uint8_t satCount_;
  uint8_t discardLeft_;
  uint8_t tareStage_;
  uint8_t tareReads_;
  uint8_t tareAttempts_;
  long tareSum_;
  long tareAvg1_;
  long tareLastAvg_;

#if defined(USE_VL53L0X) && USE_VL53L0X
  VL53L0X tof_;
#endif
};

#endif

#ifndef CPR_NANO_SENSORS_H
#define CPR_NANO_SENSORS_H

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

  bool read(uint32_t tMs, cpr::Sample& s);

  bool tareLoadCell(uint32_t timeoutMs = 1500);
  bool readRestDistance(uint32_t timeoutPerReadMs = 100);
  bool calibrateWeight(float knownKg, float& newScale);

  float scale() const { return scale_; }
  void setScale(float scale);

 private:
  bool readTofDepth(int16_t& depthMm);
  bool averageTofMm(uint16_t samples, uint16_t timeoutPerReadMs, uint16_t& outMm);

  HX711 hx_;
  float scale_;
  bool hxPresent_;
  bool tofPresent_;
  bool haveDepth_;
  uint16_t restMm_;
  int16_t lastDepthMm_;

#if defined(USE_VL53L0X) && USE_VL53L0X
  VL53L0X tof_;
#endif
};

#endif

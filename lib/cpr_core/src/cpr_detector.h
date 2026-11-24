#ifndef CPR_DETECTOR_H
#define CPR_DETECTOR_H

#include "cpr_types.h"

namespace cpr {

struct PressResult {
  uint32_t tStartMs, tPeakMs;
  float peak;
  bool recoilOk;
  bool handOk;
};

class Detector {
 public:
  Detector();
  void reset();
  void setThresholds(float contact, float release, float recoil,
                     float restartDelta, float peakDropFrac, uint16_t handsOffMs);
  bool update(uint32_t tMs, float v, bool handOk);
  const PressResult& result() const;
  Phase phase() const;
  bool pressStartedThisSample() const;
  uint32_t lastPressStartMs() const;

 private:
  void startPress(uint32_t tMs, float v, bool handOk);
  void countHand(bool handOk);
  bool finishPress(uint32_t tMs, bool recoilOk);
  bool evaluateRecoil();

  float contact_, release_, recoil_, restartDelta_, peakDropFrac_;
  uint16_t handsOffMs_;

  Phase phase_;
  uint32_t tStart_, tPeak_;
  uint32_t lastPressStartMs_;
  uint32_t finishMs_;
  float max_;
  float lo_;
  bool hasLo_;
  float minSinceRelease_;
  bool hasMin_;

  uint16_t handOn_;
  uint16_t handTotal_;

  bool pending_;
  PressResult result_;
  bool pressStarted_;
};

} // namespace cpr

#endif

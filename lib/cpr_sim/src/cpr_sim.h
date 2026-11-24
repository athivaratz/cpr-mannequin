#ifndef CPR_SIM_H
#define CPR_SIM_H

#include "cpr_types.h"

#include <stdint.h>

namespace cprsim {

enum class Scenario : uint8_t {
  Good,
  Shallow,
  Deep,
  Fast,
  Slow,
  NoRecoil,
  WrongHand,
  Noisy,
  Cycle30x2
};

struct SimOptions {
  Scenario sc;
  uint16_t sps;
  bool tof;
  uint32_t seed;
  uint16_t seconds;
  cpr::Profile profile;
};

class Generator {
 public:
  Generator();
  void begin(const SimOptions& o);
  bool next(cpr::Sample& s);
  void setRate(uint16_t bpm);
  void setKgScale(float scale);

 private:
  uint32_t nextRandom();
  float unitRandom();
  float shapeAt(uint32_t inPeriodMs) const;
  bool handClosed(uint32_t pressIndex) const;

  Scenario sc_;
  uint16_t sps_;
  bool tof_;
  uint32_t seed_;
  uint16_t seconds_;
  cpr::Profile profile_;
  uint16_t rate_;
  float kgScale_;

  uint32_t sampleIndex_;
  uint32_t totalSamples_;
  uint32_t rng_;
};

const char* scenarioName(Scenario s);
bool scenarioFromName(const char* n, Scenario& out);

} // namespace cprsim

#endif

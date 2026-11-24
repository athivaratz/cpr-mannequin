#ifndef CPR_RATE_H
#define CPR_RATE_H

#include "cpr_types.h"

namespace cpr {

class RateEstimator {
 public:
  RateEstimator();
  void reset();
  void addPress(uint32_t tStartMs);
  uint8_t bpm() const;

 private:
  uint32_t presses_[5];
  uint8_t count_;
};

RateLevel classifyRate(uint8_t bpm, const Thresholds& th);

} // namespace cpr

#endif

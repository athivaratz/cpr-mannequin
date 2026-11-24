#include "cpr_rate.h"

namespace cpr {

RateEstimator::RateEstimator() { reset(); }

void RateEstimator::reset() {
  for (uint8_t i = 0; i < 5; ++i) presses_[i] = 0;
  count_ = 0;
}

void RateEstimator::addPress(uint32_t tStartMs) {
  if (count_ < 5) {
    presses_[count_++] = tStartMs;
    return;
  }
  for (uint8_t i = 0; i < 4; ++i) presses_[i] = presses_[i + 1];
  presses_[4] = tStartMs;
}

uint8_t RateEstimator::bpm() const {
  if (count_ < 2) return 0;
  uint32_t first = presses_[0];
  uint32_t last = presses_[count_ - 1];
  uint32_t dt = last - first;
  if (dt == 0) return 0;
  uint32_t n = count_ - 1;
  uint32_t value = (60000UL * n + dt / 2) / dt;
  if (value > 250) value = 250;
  return (uint8_t)value;
}

RateLevel classifyRate(uint8_t bpm, const Thresholds& th) {
  if (bpm == 0) return RateLevel::None;
  if (bpm < th.rateMin) return RateLevel::Slow;
  if (bpm > th.rateMax) return RateLevel::Fast;
  return RateLevel::Good;
}

} // namespace cpr

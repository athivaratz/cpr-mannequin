#include "cpr_samplerate.h"

namespace cpr {

SampleRateMeter::SampleRateMeter() {
  bucketStart_ = 0;
  count_ = 0;
  sps_ = 0;
  buckets_ = 0;
  started_ = false;
}

void SampleRateMeter::reset(uint32_t tMs) {
  bucketStart_ = tMs;
  count_ = 0;
  sps_ = 0;
  buckets_ = 0;
  started_ = true;
}

void SampleRateMeter::add(uint32_t tMs) {
  if (!started_) {
    reset(tMs);
    return;
  }

  if ((uint32_t)(tMs - bucketStart_) < 1000UL) {
    if (count_ < 0xFFFF) count_++;
    return;
  }

  sps_ = (count_ > 255) ? 255 : (uint8_t)count_;
  if (buckets_ < 2) buckets_++;
  bucketStart_ = tMs;
  count_ = 1;
}

uint8_t SampleRateMeter::sps() const { return sps_; }

uint8_t SampleRateMeter::buckets() const { return buckets_; }

bool SampleRateMeter::lowSps(uint8_t warn) const {
  return buckets_ >= 2 && sps_ < warn;
}

} // namespace cpr

#ifndef CPR_SAMPLERATE_H
#define CPR_SAMPLERATE_H

#include <stdint.h>

namespace cpr {

class SampleRateMeter {
 public:
  SampleRateMeter();
  void reset(uint32_t tMs);
  void add(uint32_t tMs);
  uint8_t sps() const;
  uint8_t buckets() const;
  bool lowSps(uint8_t warn) const;

 private:
  uint32_t bucketStart_;
  uint16_t count_;
  uint8_t sps_;
  uint8_t buckets_;
  bool started_;
};

} // namespace cpr

#endif

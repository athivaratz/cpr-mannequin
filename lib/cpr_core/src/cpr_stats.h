#ifndef CPR_STATS_H
#define CPR_STATS_H

#include "cpr_types.h"

namespace cpr {

class Stats {
 public:
  Stats();
  void reset();
  void addCompression(const CompressionEvent& c);
  void addPause(uint32_t pauseMs, const Thresholds& th);
  void touch(uint32_t tMs);
  void setCycles(uint8_t cycles);
  const SessionStats& data() const;

 private:
  SessionStats s_;
};

uint8_t pct(uint16_t part, uint16_t whole);
uint8_t ccfPercent(const SessionStats& s);
uint8_t score(const SessionStats& s);

} // namespace cpr

#endif

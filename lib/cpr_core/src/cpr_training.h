#ifndef CPR_TRAINING_H
#define CPR_TRAINING_H

#include "cpr_types.h"

namespace cpr {

class Training {
 public:
  Training();
  void reset(Mode m);
  bool onCompression(uint32_t tMs, Event& ev);
  bool onPauseEnd(uint32_t tMs, uint32_t pauseMs, const Thresholds& th, Event& ev);
  bool tick(uint32_t tMs, uint32_t sessionStartMs, Event& ev);
  uint8_t cycleCount() const;
  uint8_t cycles() const;
  bool awaitingBreaths() const;

 private:
  Mode mode_;
  uint8_t count_;
  uint8_t cycles_;
  bool awaiting_;
  bool ended_;
};

} // namespace cpr

#endif

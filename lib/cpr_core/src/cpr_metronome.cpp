#include "cpr_metronome.h"

namespace cpr {

Metronome::Metronome() {
  t0_ = 0;
  bpm_ = 0;
  nextBeat_ = 1;
  running_ = false;
}

void Metronome::start(uint32_t tMs, uint16_t bpm) {
  t0_ = tMs;
  bpm_ = bpm;
  nextBeat_ = 1;
  running_ = bpm != 0;
}

void Metronome::stop() { running_ = false; }

bool Metronome::running() const { return running_; }

bool Metronome::tick(uint32_t tMs) {
  if (!running_ || bpm_ == 0) return false;

  uint32_t due = t0_ + (nextBeat_ * 60000UL) / bpm_;
  if (tMs < due) return false;

  nextBeat_++;
  return true;
}

} // namespace cpr

#include "cpr_training.h"

namespace cpr {

namespace {
const uint8_t kCycleCompressions = 30; // AHA single-rescuer 30:2
const uint32_t kTest2MinMs = 120000UL;
}

Training::Training() { reset(Mode::Free); }

void Training::reset(Mode m) {
  mode_ = m;
  count_ = 0;
  cycles_ = 0;
  awaiting_ = false;
  ended_ = false;
}

bool Training::onCompression(uint32_t tMs, Event& ev) {
  (void)tMs;
  (void)ev;
  if (mode_ != Mode::Cycle30x2) return false;

  if (count_ < 255) count_++;
  if (!awaiting_ && count_ >= kCycleCompressions) {
    awaiting_ = true;
  }
  return false;
}

bool Training::onPauseEnd(uint32_t tMs, uint32_t pauseMs,
                          const Thresholds& th, Event& ev) {
  if (mode_ != Mode::Cycle30x2 || !awaiting_) return false;

  cycles_++;
  ev.type = EventType::CycleDone;
  ev.tMs = tMs;
  ev.pauseMs = pauseMs;
  ev.cycle = cycles_;
  ev.pauseTooLong = pauseMs > th.maxPauseMs;
  count_ = 0;
  awaiting_ = false;
  return true;
}

bool Training::tick(uint32_t tMs, uint32_t sessionStartMs, Event& ev) {
  if (mode_ != Mode::Test2Min || ended_) return false;
  if (sessionStartMs == 0) return false;
  if ((uint32_t)(tMs - sessionStartMs) < kTest2MinMs) return false;

  ended_ = true;
  ev.type = EventType::SessionEnd;
  ev.tMs = tMs;
  ev.pauseMs = 0;
  ev.cycle = 0;
  ev.pauseTooLong = false;
  return true;
}

uint8_t Training::cycleCount() const { return count_; }

uint8_t Training::cycles() const { return cycles_; }

bool Training::awaitingBreaths() const { return awaiting_; }

} // namespace cpr

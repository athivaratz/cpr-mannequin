#ifndef CPR_METRONOME_H
#define CPR_METRONOME_H

#include <stdint.h>

namespace cpr {

class Metronome {
 public:
  Metronome();
  void start(uint32_t tMs, uint16_t bpm);
  void stop();
  bool running() const;
  bool tick(uint32_t tMs);

 private:
  uint32_t t0_;
  uint16_t bpm_;
  uint32_t nextBeat_;
  bool running_;
};

} // namespace cpr

#endif

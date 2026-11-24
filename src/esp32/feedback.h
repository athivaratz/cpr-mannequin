#ifndef CPR_ESP32_FEEDBACK_H
#define CPR_ESP32_FEEDBACK_H

#include <Arduino.h>

#include "cpr_types.h"

class Feedback {
 public:
  Feedback();

  void begin();
  void update(const cpr::LiveState& live);
  void onEvent(const cpr::Event& ev, uint32_t nowMs);
  void beep(uint32_t nowMs, uint16_t durationMs);
  void tick(uint32_t nowMs);

 private:
  uint32_t buzzerUntilMs_;
};

#endif

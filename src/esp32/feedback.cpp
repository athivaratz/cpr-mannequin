#include "feedback.h"

#include "pins.h"

Feedback::Feedback() : buzzerUntilMs_(0) {}

void Feedback::begin() {
  pinMode(pins::kBuzzer, OUTPUT);
  pinMode(pins::kLedPos, OUTPUT);
  pinMode(pins::kLedGood, OUTPUT);
  pinMode(pins::kLedShallow, OUTPUT);
  pinMode(pins::kLedDeep, OUTPUT);

  digitalWrite(pins::kBuzzer, LOW);
  digitalWrite(pins::kLedPos, LOW);
  digitalWrite(pins::kLedGood, LOW);
  digitalWrite(pins::kLedShallow, LOW);
  digitalWrite(pins::kLedDeep, LOW);
}

void Feedback::update(const cpr::LiveState& live) {
  const bool good = live.liveDepth == cpr::DepthLevel::Good;
  const bool shallow = live.liveDepth == cpr::DepthLevel::Shallow;
  const bool deep =
      live.liveDepth == cpr::DepthLevel::Deep || live.overload;

  digitalWrite(pins::kLedGood, good ? HIGH : LOW);
  digitalWrite(pins::kLedShallow, shallow ? HIGH : LOW);
  digitalWrite(pins::kLedDeep, deep ? HIGH : LOW);

#if CPR_HAS_FOIL
  digitalWrite(pins::kLedPos, live.handOk ? HIGH : LOW);
#else
  digitalWrite(pins::kLedPos,
               live.phase != cpr::Phase::Idle ? HIGH : LOW);
#endif
}

void Feedback::onEvent(const cpr::Event& ev, uint32_t nowMs) {
  if (ev.type == cpr::EventType::Compression) {
    beep(nowMs, ev.c.allOk ? 40 : 150);
  } else if (ev.type == cpr::EventType::Overload) {
    beep(nowMs, 200);
  }
}

void Feedback::beep(uint32_t nowMs, uint16_t durationMs) {
  const uint32_t end = nowMs + (uint32_t)durationMs;
  if (buzzerUntilMs_ == 0 || (int32_t)(end - buzzerUntilMs_) > 0) {
    buzzerUntilMs_ = end;
  }
  digitalWrite(pins::kBuzzer, HIGH);
}

void Feedback::tick(uint32_t nowMs) {
  if (buzzerUntilMs_ != 0 &&
      (int32_t)(nowMs - buzzerUntilMs_) >= 0) {
    digitalWrite(pins::kBuzzer, LOW);
    buzzerUntilMs_ = 0;
  }
}

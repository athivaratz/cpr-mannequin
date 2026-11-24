#include "cpr_sim.h"

#include <math.h>
#include <string.h>

namespace cprsim {

namespace {

const float kPi = 3.14159265358979f;
const float kGoodDepthMm = 54.0f;
const float kShallowDepthMm = 35.0f;
const float kDeepDepthMm = 68.0f;
const float kNoRecoilBaseMm = 12.0f;
const uint32_t kCyclePauseMs = 5000UL;
const uint8_t kCyclePresses = 30;

bool lowerEq(const char* a, const char* b) {
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

float kgFromMm(float mm) {
  if (mm < 0.0f) mm = 0.0f;
  return 0.2f * mm + 0.0012f * mm * mm;
}

} // namespace

Generator::Generator() {
  sc_ = Scenario::Good;
  sps_ = 80;
  tof_ = true;
  seed_ = 1;
  seconds_ = 40;
  profile_ = cpr::Profile::Adult;
  rate_ = 110;
  kgScale_ = 1.0f;
  sampleIndex_ = 0;
  totalSamples_ = 0;
  rng_ = 1;
}

void Generator::begin(const SimOptions& o) {
  sc_ = o.sc;
  sps_ = (o.sps == 0) ? 1 : o.sps;
  tof_ = o.tof;
  seed_ = (o.seed == 0) ? 1 : o.seed;
  seconds_ = o.seconds;
  profile_ = o.profile;

  rate_ = 110;
  switch (sc_) {
    case Scenario::Good:
    case Scenario::NoRecoil:
    case Scenario::WrongHand:
    case Scenario::Noisy:
    case Scenario::Cycle30x2:
      rate_ = 110;
      break;
    case Scenario::Shallow:
    case Scenario::Deep:
      rate_ = 110;
      break;
    case Scenario::Fast:
      rate_ = 135;
      break;
    case Scenario::Slow:
      rate_ = 85;
      break;
  }

  sampleIndex_ = 0;
  totalSamples_ = (uint32_t)seconds_ * (uint32_t)sps_;
  rng_ = seed_;
}

void Generator::setRate(uint16_t bpm) {
  if (bpm != 0) rate_ = bpm;
}

void Generator::setKgScale(float scale) {
  if (scale > 0.0f) kgScale_ = scale;
}

uint32_t Generator::nextRandom() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

float Generator::unitRandom() {
  return (float)(nextRandom() & 0x00FFFFFFUL) / 16777216.0f;
}

float Generator::shapeAt(uint32_t inPeriodMs) const {
  float periodMs = 60000.0f / (float)rate_;
  float phase = (float)inPeriodMs / periodMs;
  if (phase < 0.45f) {
    return (1.0f - cosf(kPi * phase / 0.45f)) * 0.5f;
  }
  if (phase < 0.90f) {
    return (1.0f + cosf(kPi * (phase - 0.45f) / 0.45f)) * 0.5f;
  }
  return 0.0f;
}

bool Generator::handClosed(uint32_t pressIndex) const {
  if (sc_ != Scenario::WrongHand) return true;
  uint32_t h = pressIndex * 2654435761UL + seed_ * 2246822519UL;
  h ^= h >> 15;
  h *= 2246822519UL;
  h ^= h >> 13;
  return (h % 100UL) < 30UL;
}

bool Generator::next(cpr::Sample& s) {
  if (sampleIndex_ >= totalSamples_) return false;

  uint32_t tMs = (sampleIndex_ * 1000UL) / (uint32_t)sps_;
  float periodMs = 60000.0f / (float)rate_;
  uint32_t period = (uint32_t)(periodMs + 0.5f);
  if (period == 0) period = 1;

  float depth = kGoodDepthMm;
  float base = 0.0f;
  switch (sc_) {
    case Scenario::Good:
    case Scenario::Fast:
    case Scenario::Slow:
    case Scenario::WrongHand:
    case Scenario::Noisy:
    case Scenario::Cycle30x2:
      depth = kGoodDepthMm;
      break;
    case Scenario::Shallow:
      depth = kShallowDepthMm;
      break;
    case Scenario::Deep:
      depth = kDeepDepthMm;
      break;
    case Scenario::NoRecoil:
      depth = kGoodDepthMm;
      base = kNoRecoilBaseMm;
      break;
  }

  float value = 0.0f;
  uint32_t pressIndex = 0;

  if (sc_ == Scenario::Cycle30x2) {
    uint32_t activeMs = (uint32_t)kCyclePresses * period;
    uint32_t cycleLen = activeMs + kCyclePauseMs;
    uint32_t cycle = tMs / cycleLen;
    uint32_t local = tMs % cycleLen;
    if (local < activeMs) {
      uint32_t inPeriod = local % period;
      pressIndex = cycle * kCyclePresses + local / period;
      float shape = shapeAt(inPeriod);
      value = base + (depth - base) * shape;
    } else {
      pressIndex = cycle * kCyclePresses + (kCyclePresses - 1);
      value = base;
    }
  } else {
    pressIndex = tMs / period;
    uint32_t inPeriod = tMs % period;
    float shape = shapeAt(inPeriod);
    value = base + (depth - base) * shape;
  }

  if (sc_ == Scenario::Noisy) {
    float n = (unitRandom() + unitRandom() + unitRandom() +
               unitRandom() + unitRandom() + unitRandom() - 3.0f);
    value += n;
    if ((nextRandom() % 137UL) == 0UL) {
      float spike = ((nextRandom() & 1UL) != 0UL) ? 6.0f : -6.0f;
      value += spike;
    }
    if (value < 0.0f) value = 0.0f;
  }

  s.tMs = tMs;
  s.kg = kgFromMm(value) * kgScale_;
  if (tof_) {
    float rounded = value + 0.5f;
    if (rounded < 0.0f) rounded = 0.0f;
    if (rounded > 32767.0f) rounded = 32767.0f;
    s.mm = (int16_t)rounded;
  } else {
    s.mm = -1;
  }
  s.handOk = handClosed(pressIndex);

  sampleIndex_++;
  return true;
}

const char* scenarioName(Scenario s) {
  switch (s) {
    case Scenario::Good: return "good";
    case Scenario::Shallow: return "shallow";
    case Scenario::Deep: return "deep";
    case Scenario::Fast: return "fast";
    case Scenario::Slow: return "slow";
    case Scenario::NoRecoil: return "norecoil";
    case Scenario::WrongHand: return "wronghand";
    case Scenario::Noisy: return "noisy";
    case Scenario::Cycle30x2: return "cycle";
  }
  return "good";
}

bool scenarioFromName(const char* n, Scenario& out) {
  if (n == nullptr) return false;
  if (lowerEq(n, "good")) { out = Scenario::Good; return true; }
  if (lowerEq(n, "shallow")) { out = Scenario::Shallow; return true; }
  if (lowerEq(n, "deep")) { out = Scenario::Deep; return true; }
  if (lowerEq(n, "fast")) { out = Scenario::Fast; return true; }
  if (lowerEq(n, "slow")) { out = Scenario::Slow; return true; }
  if (lowerEq(n, "norecoil")) { out = Scenario::NoRecoil; return true; }
  if (lowerEq(n, "wronghand")) { out = Scenario::WrongHand; return true; }
  if (lowerEq(n, "noisy")) { out = Scenario::Noisy; return true; }
  if (lowerEq(n, "cycle")) { out = Scenario::Cycle30x2; return true; }
  return false;
}

} // namespace cprsim

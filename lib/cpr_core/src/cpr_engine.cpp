#include "cpr_engine.h"

#include "cpr_profiles.h"

#include <math.h>
#include <string.h>

namespace cpr {

namespace {

const uint32_t kIdleSessionEndMs = 30000UL; // 30 s without a press ends the session

void zeroEvent(Event& e, EventType type, uint32_t tMs) {
  memset(&e, 0, sizeof(e));
  e.type = type;
  e.tMs = tMs;
}

} // namespace

Engine::Engine() {
  cfg_ = defaultConfig(Profile::Child);
  calibDefaults(cal_);
  tofPresent_ = false;
  mode_ = Mode::Free;
  source_ = Source::ForceKg;
  pressSource_ = Source::ForceKg;
  pressInProgress_ = false;
  meterStarted_ = false;
  kgSeen_ = false;
  overload_ = false;
  sessionActive_ = false;
  inPause_ = false;
  calActive_ = false;
  sessionStartMs_ = 0;
  lastPressActivityMs_ = 0;
  lastPressFinishMs_ = 0;
  compIndex_ = 0;
  eventHead_ = 0;
  eventTail_ = 0;
  eventCount_ = 0;
  memset(&live_, 0, sizeof(live_));
  live_.kg = NAN;
  live_.mm = -1;
  configureDetector();
}

void Engine::configureDetector() {
  if (source_ == Source::ForceKg) {
    det_.setThresholds(cfg_.th.contactKg, cfg_.th.releaseKg, cfg_.th.recoilKg,
                       cfg_.th.restartDeltaKg, cfg_.th.peakDropFrac,
                       cfg_.th.handsOffMs);
  } else {
    det_.setThresholds(cfg_.th.contactMm, cfg_.th.releaseMm, cfg_.th.recoilMm,
                       cfg_.th.restartDeltaMm, cfg_.th.peakDropFrac,
                       cfg_.th.handsOffMs);
  }
}

void Engine::begin(const Config& cfg, const CalibrationData& cal) {
  cfg_ = cfg;
  cal_ = cal;
  tofPresent_ = false;
  mode_ = Mode::Free;
  source_ = Source::ForceKg;
  pressSource_ = Source::ForceKg;
  pressInProgress_ = false;

  valueFilter_.reset();
  valueFilter_.setAlpha(cfg_.filterAlpha);
  kgFilter_.reset();
  kgFilter_.setAlpha(cfg_.filterAlpha);

  det_.reset();
  rate_.reset();
  stats_.reset();
  training_.reset(mode_);
  spsMeter_ = SampleRateMeter();

  meterStarted_ = false;
  kgSeen_ = false;
  overload_ = false;
  sessionActive_ = false;
  inPause_ = false;
  calActive_ = false;
  sessionStartMs_ = 0;
  lastPressActivityMs_ = 0;
  lastPressFinishMs_ = 0;
  compIndex_ = 0;
  eventHead_ = 0;
  eventTail_ = 0;
  eventCount_ = 0;
  recorder_.reset();

  memset(&live_, 0, sizeof(live_));
  live_.kg = NAN;
  live_.mm = -1;
  configureDetector();
}

void Engine::setConfig(const Config& cfg) {
  cfg_ = cfg;
  valueFilter_.setAlpha(cfg_.filterAlpha);
  kgFilter_.setAlpha(cfg_.filterAlpha);
  configureDetector();
}

void Engine::setCalibration(const CalibrationData& cal) { cal_ = cal; }

void Engine::setMode(Mode m) {
  mode_ = m;
  training_.reset(m);
}

void Engine::setTofPresent(bool present) { tofPresent_ = present; }

void Engine::startSession(uint32_t tMs) {
  stats_.reset();
  training_.reset(mode_);
  rate_.reset();
  compIndex_ = 0;
  sessionActive_ = false;
  inPause_ = false;
  sessionStartMs_ = 0;
  lastPressActivityMs_ = tMs;
  lastPressFinishMs_ = tMs;
}

void Engine::endSession(uint32_t tMs) {
  Event e;
  zeroEvent(e, EventType::SessionEnd, tMs);
  pushEvent(e);
  stats_.touch(tMs);
  sessionActive_ = false;
  inPause_ = false;
}

void Engine::pushEvent(const Event& e) {
  if (eventCount_ >= 4) {
    eventHead_ = (uint8_t)((eventHead_ + 1) & 3);
    eventCount_--;
  }

  QueuedEvent& q = events_[eventTail_];
  memset(&q, 0, sizeof(q));
  q.type = e.type;
  q.cycle = e.cycle;
  q.pauseTooLong = e.pauseTooLong;
  q.tMs = e.tMs;
  q.pauseMs = e.pauseMs;
  if (e.type == EventType::Compression) {
    q.index = e.c.index;
    q.source = (uint8_t)e.c.source;
    q.depth = (uint8_t)e.c.depth;
    q.rate = (uint8_t)e.c.rate;
    q.bpm = e.c.bpm;
    q.flags = (uint8_t)((e.c.recoilOk ? 1 : 0) |
                        (e.c.handOk ? 2 : 0) |
                        (e.c.allOk ? 4 : 0));
    q.tStartMs = e.c.tStartMs;
    q.tPeakMs = e.c.tPeakMs;
    q.peak = e.c.peak;
  }

  eventTail_ = (uint8_t)((eventTail_ + 1) & 3);
  eventCount_++;
}

bool Engine::pollEvent(Event& out) {
  if (eventCount_ == 0) return false;

  const QueuedEvent& q = events_[eventHead_];
  memset(&out, 0, sizeof(out));
  out.type = q.type;
  out.tMs = q.tMs;
  out.pauseMs = q.pauseMs;
  out.cycle = q.cycle;
  out.pauseTooLong = q.pauseTooLong;
  if (q.type == EventType::Compression) {
    out.c.index = q.index;
    out.c.tStartMs = q.tStartMs;
    out.c.tPeakMs = q.tPeakMs;
    out.c.peak = q.peak;
    out.c.source = (Source)q.source;
    out.c.depth = (DepthLevel)q.depth;
    out.c.rate = (RateLevel)q.rate;
    out.c.bpm = q.bpm;
    out.c.recoilOk = (q.flags & 1) != 0;
    out.c.handOk = (q.flags & 2) != 0;
    out.c.allOk = (q.flags & 4) != 0;
  }

  eventHead_ = (uint8_t)((eventHead_ + 1) & 3);
  eventCount_--;
  return true;
}

const LiveState& Engine::live() const { return live_; }

const SessionStats& Engine::stats() const { return stats_.data(); }

const Config& Engine::config() const { return cfg_; }

Source Engine::source() const { return source_; }

DepthLevel Engine::depthFor(float value, Source src) const {
  if (isnan(value)) return DepthLevel::None;
  float good, deep;
  if (src == Source::ForceKg) {
    good = cfg_.th.goodMinKg;
    deep = cfg_.th.deepMinKg;
  } else {
    good = cfg_.th.goodMinMm;
    deep = cfg_.th.deepMinMm;
  }
  if (value < good) return DepthLevel::Shallow;
  if (value >= deep) return DepthLevel::Deep;
  return DepthLevel::Good;
}

void Engine::handleResult(uint32_t tMs) {
  const PressResult& r = det_.result();
  CompressionEvent c;
  c.index = (uint16_t)(compIndex_ + 1);
  compIndex_++;
  c.tStartMs = r.tStartMs;
  c.tPeakMs = r.tPeakMs;
  c.peak = r.peak;
  c.source = pressSource_;
  c.depth = depthFor(r.peak, pressSource_);
  c.bpm = rate_.bpm();
  c.rate = classifyRate(c.bpm, cfg_.th);
  c.recoilOk = r.recoilOk;
  c.handOk = r.handOk;
  c.allOk = (c.depth == DepthLevel::Good) &&
            (c.rate == RateLevel::Good || c.rate == RateLevel::None) &&
            c.recoilOk && c.handOk;

  Event e;
  zeroEvent(e, EventType::Compression, tMs);
  e.c = c;
  pushEvent(e);

  stats_.addCompression(c);
  stats_.touch(tMs);

  lastPressFinishMs_ = tMs;
  lastPressActivityMs_ = tMs;

  Event te;
  if (training_.onCompression(tMs, te)) {
    pushEvent(te);
  }
}

void Engine::handlePressStart(uint32_t tMs) {
  if (!sessionActive_) {
    stats_.reset();
    training_.reset(mode_);
    rate_.reset();
    compIndex_ = 0;
    inPause_ = false;
    sessionStartMs_ = tMs;
    sessionActive_ = true;
  } else if (inPause_) {
    uint32_t pauseMs = (tMs >= lastPressFinishMs_) ? (tMs - lastPressFinishMs_) : 0;
    Event pe;
    zeroEvent(pe, EventType::PauseEnd, tMs);
    pe.pauseMs = pauseMs;
    pe.pauseTooLong = pauseMs > cfg_.th.maxPauseMs;
    pushEvent(pe);
    stats_.addPause(pauseMs, cfg_.th);

    Event ce;
    if (training_.onPauseEnd(tMs, pauseMs, cfg_.th, ce)) {
      stats_.setCycles(training_.cycles());
      pushEvent(ce);
    }
    inPause_ = false;
  }

  rate_.addPress(tMs);
  lastPressActivityMs_ = tMs;
}

void Engine::process(const Sample& s) {
  const uint32_t t = s.tMs;

  if (!meterStarted_) {
    spsMeter_.reset(t);
    meterStarted_ = true;
  } else {
    spsMeter_.add(t);
  }

  const bool kgValid = !isnan(s.kg);
  const float kgFiltered = kgFilter_.push(s.kg);
  if (kgValid) kgSeen_ = true;

  if (calActive_ && kgValid && s.mm >= 0) {
    recorder_.add(s.kg, (float)s.mm);
  }

  if (det_.phase() == Phase::Idle && !pressInProgress_) {
    Source candidate = source_;
    if (tofPresent_ && s.mm >= 0) {
      candidate = Source::TofMm;
    } else if (cal_.mapCount >= 2 && kgValid) {
      candidate = Source::MappedMm;
    } else if (kgValid) {
      candidate = Source::ForceKg;
    }
    if (candidate != source_) {
      source_ = candidate;
      valueFilter_.reset();
      configureDetector();
    }
  }

  float raw = NAN;
  if (source_ == Source::TofMm) {
    if (s.mm >= 0) raw = (float)s.mm;
  } else if (source_ == Source::MappedMm) {
    if (kgValid) raw = calibMapKgToMm(cal_, s.kg);
  } else if (kgValid) {
    raw = s.kg;
  }

  const float value = valueFilter_.push(raw);
  const bool valueValid = !isnan(raw);

  bool resultReady = false;
  bool pressStarted = false;
  if (valueValid) {
    resultReady = det_.update(t, value, s.handOk);
    pressStarted = det_.pressStartedThisSample();
  }

  if (resultReady && pressStarted) {
    if (sessionActive_) handleResult(t);
    pressSource_ = source_;
    pressInProgress_ = true;
    handlePressStart(t);
  } else if (resultReady) {
    if (sessionActive_) handleResult(t);
    pressInProgress_ = false;
  } else if (pressStarted) {
    pressSource_ = source_;
    pressInProgress_ = true;
    handlePressStart(t);
  }

  if (kgValid) {
    bool ov = kgFiltered >= cfg_.th.overloadKg;
    if (ov && !overload_) {
      Event e;
      zeroEvent(e, EventType::Overload, t);
      pushEvent(e);
    }
    overload_ = ov;
  }

  if (sessionActive_ && !inPause_ &&
      (det_.phase() == Phase::Idle || det_.phase() == Phase::Releasing)) {
    if ((uint32_t)(t - det_.lastPressStartMs()) > cfg_.th.handsOffMs) {
      Event e;
      zeroEvent(e, EventType::PauseStart, t);
      pushEvent(e);
      inPause_ = true;
      rate_.reset();
    }
  }

  if (sessionActive_ && (uint32_t)(t - lastPressActivityMs_) >= kIdleSessionEndMs) {
    Event e;
    zeroEvent(e, EventType::SessionEnd, t);
    pushEvent(e);
    stats_.touch(t);
    sessionActive_ = false;
    inPause_ = false;
  }

  if (sessionActive_ && mode_ == Mode::Test2Min) {
    Event e;
    if (training_.tick(t, sessionStartMs_, e)) {
      pushEvent(e);
      stats_.touch(t);
      sessionActive_ = false;
      inPause_ = false;
    }
  }

  live_.tMs = t;
  live_.value = value;
  live_.kg = kgSeen_ ? kgFiltered : NAN;
  if (source_ == Source::MappedMm && valueValid) {
    float rounded = raw + 0.5f;
    if (rounded < 0.0f) rounded = 0.0f;
    if (rounded > 32767.0f) rounded = 32767.0f;
    live_.mm = (int16_t)rounded;
  } else {
    live_.mm = s.mm;
  }
  live_.source = source_;
  live_.phase = det_.phase();
  live_.liveDepth = (det_.phase() == Phase::Idle)
                        ? DepthLevel::None
                        : depthFor(value, source_);
  live_.bpm = rate_.bpm();
  live_.rate = classifyRate(live_.bpm, cfg_.th);
  live_.handOk = s.handOk;
  live_.overload = overload_;
  live_.inPause = inPause_;
  live_.count = stats_.data().count;
  live_.cycleCount = training_.cycleCount();
  live_.cycles = training_.cycles();
  live_.sps = spsMeter_.sps();
  live_.lowSps = spsMeter_.lowSps(cfg_.th.lowSpsWarn);
  live_.sessionActive = sessionActive_;
  live_.elapsedS = sessionActive_
                       ? (uint16_t)((t - sessionStartMs_) / 1000UL)
                       : 0;
}

void Engine::calStart() {
  recorder_.reset();
  calActive_ = true;
}

void Engine::calCancel() {
  calActive_ = false;
  recorder_.reset();
}

bool Engine::calActive() const { return calActive_; }

uint16_t Engine::calCount() const { return recorder_.count(); }

bool Engine::calFinish(CalibrationData& out) {
  CalibrationData tmp = cal_;
  uint8_t n = calibFit(recorder_.pairs(), recorder_.count(), tmp);
  if (n < 2) return false;
  tmp.profile = (uint8_t)cfg_.profile;
  cal_ = tmp;
  out = tmp;
  calActive_ = false;
  return true;
}

} // namespace cpr

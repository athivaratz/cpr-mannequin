#include "cpr_detector.h"

namespace cpr {

Detector::Detector() { reset(); }

void Detector::reset() {
  contact_ = 0.0f;
  release_ = 0.0f;
  recoil_ = 0.0f;
  restartDelta_ = 0.0f;
  peakDropFrac_ = 0.15f;
  handsOffMs_ = 2000;

  phase_ = Phase::Idle;
  tStart_ = 0;
  tPeak_ = 0;
  lastPressStartMs_ = 0;
  finishMs_ = 0;
  max_ = 0.0f;
  lo_ = 0.0f;
  hasLo_ = false;
  minSinceRelease_ = 0.0f;
  hasMin_ = false;

  handOn_ = 0;
  handTotal_ = 0;

  pending_ = false;
  result_.tStartMs = 0;
  result_.tPeakMs = 0;
  result_.peak = 0.0f;
  result_.recoilOk = false;
  result_.handOk = false;
  pressStarted_ = false;
}

void Detector::setThresholds(float contact, float release, float recoil,
                             float restartDelta, float peakDropFrac,
                             uint16_t handsOffMs) {
  contact_ = contact;
  release_ = release;
  recoil_ = recoil;
  restartDelta_ = restartDelta;
  peakDropFrac_ = peakDropFrac;
  handsOffMs_ = handsOffMs;
}

void Detector::startPress(uint32_t tMs, float v, bool handOk) {
  phase_ = Phase::Pressing;
  tStart_ = tMs;
  tPeak_ = tMs;
  lastPressStartMs_ = tMs;
  max_ = v;
  lo_ = v;
  hasLo_ = false;
  handOn_ = handOk ? 1 : 0;
  handTotal_ = 1;
  pressStarted_ = true;
}

void Detector::countHand(bool handOk) {
  if (handOk) handOn_++;
  handTotal_++;
}

bool Detector::finishPress(uint32_t tMs, bool recoilOk) {
  result_.tStartMs = tStart_;
  result_.tPeakMs = tPeak_;
  result_.peak = max_;
  result_.recoilOk = recoilOk;
  result_.handOk = (handTotal_ != 0) && ((uint32_t)handOn_ * 2 >= handTotal_);
  finishMs_ = tMs;
  phase_ = Phase::Idle;
  pending_ = false;
  return true;
}

bool Detector::evaluateRecoil() {
  bool ok = hasMin_ && (minSinceRelease_ <= recoil_);
  result_.recoilOk = ok;
  pending_ = false;
  return true;
}

bool Detector::update(uint32_t tMs, float v, bool handOk) {
  pressStarted_ = false;

  if (phase_ == Phase::Idle) {
    if (pending_) {
      if (!hasMin_ || v < minSinceRelease_) {
        minSinceRelease_ = v;
        hasMin_ = true;
      }
      if (v <= recoil_) {
        return evaluateRecoil();
      }
      if (v >= contact_) {
        result_.recoilOk = false;
        pending_ = false;
        startPress(tMs, v, handOk);
        return true;
      }
      if ((uint32_t)(tMs - finishMs_) >= handsOffMs_) {
        return evaluateRecoil();
      }
      return false;
    }

    if (v >= contact_) {
      startPress(tMs, v, handOk);
    }
    return false;
  }

  countHand(handOk);

  if (phase_ == Phase::Pressing) {
    if (v > max_) {
      max_ = v;
      tPeak_ = tMs;
    }
    if (max_ >= contact_ && v <= max_ * (1.0f - peakDropFrac_)) {
      phase_ = Phase::Peak;
    }
    return false;
  }

  if (phase_ == Phase::Peak) {
    phase_ = Phase::Releasing;
    lo_ = v;
    hasLo_ = true;
  } else {
    if (!hasLo_ || v < lo_) {
      lo_ = v;
      hasLo_ = true;
    }
  }

  if (v <= release_) {
    minSinceRelease_ = v;
    hasMin_ = true;
    pending_ = true;
    finishMs_ = tMs;
    phase_ = Phase::Idle;
    result_.tStartMs = tStart_;
    result_.tPeakMs = tPeak_;
    result_.peak = max_;
    result_.handOk = (handTotal_ != 0) && ((uint32_t)handOn_ * 2 >= handTotal_);
    if (v <= recoil_) {
      result_.recoilOk = true;
      pending_ = false;
      return true;
    }
    return false;
  }

  if (hasLo_ && lo_ > release_ && v >= lo_ + restartDelta_) {
    bool ok = finishPress(tMs, false);
    startPress(tMs, v, handOk);
    return ok;
  }

  return false;
}

const PressResult& Detector::result() const { return result_; }

Phase Detector::phase() const { return phase_; }

bool Detector::pressStartedThisSample() const { return pressStarted_; }

uint32_t Detector::lastPressStartMs() const { return lastPressStartMs_; }

} // namespace cpr

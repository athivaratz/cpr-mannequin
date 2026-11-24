#include "cpr_filter.h"

#include <math.h>
#include <stdint.h>

namespace cpr {

Filter::Filter() { reset(); }

void Filter::reset() {
  buf_[0] = 0.0f;
  buf_[1] = 0.0f;
  buf_[2] = 0.0f;
  count_ = 0;
  value_ = 0.0f;
  alpha_ = 0.5f;
  has_ = false;
}

void Filter::setAlpha(float a) {
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  alpha_ = a;
}

float Filter::push(float x) {
  if (isnan(x)) return value_;

  if (count_ < 3) {
    buf_[count_++] = x;
  } else {
    buf_[0] = buf_[1];
    buf_[1] = buf_[2];
    buf_[2] = x;
  }

  float med;
  if (count_ == 1) {
    med = buf_[0];
  } else if (count_ == 2) {
    med = (buf_[0] + buf_[1]) * 0.5f;
  } else {
    float a = buf_[0], b = buf_[1], c = buf_[2];
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    med = b;
  }

  if (!has_) {
    value_ = med;
    has_ = true;
  } else {
    value_ = alpha_ * med + (1.0f - alpha_) * value_;
  }
  return value_;
}

float Filter::value() const { return value_; }

} // namespace cpr

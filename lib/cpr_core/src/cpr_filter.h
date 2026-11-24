#ifndef CPR_FILTER_H
#define CPR_FILTER_H

#include <stdint.h>

namespace cpr {

class Filter {
 public:
  Filter();
  void reset();
  void setAlpha(float a);
  float push(float x);
  float value() const;

 private:
  float buf_[3];
  uint8_t count_;
  float value_;
  float alpha_;
  bool has_;
};

} // namespace cpr

#endif

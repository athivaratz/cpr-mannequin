#ifndef CPR_CALIB_H
#define CPR_CALIB_H

#include "cpr_types.h"

#include <stddef.h>
#include <stdint.h>

#ifndef CPR_CAL_PAIRS
#define CPR_CAL_PAIRS 48
#endif

namespace cpr {

static const uint8_t kMaxCalPoints = 8;
static const uint16_t kCalMagic = 0xC9A1;
static const uint8_t kCalVersion = 1;

struct CalPoint {
  float kg;
  float mm;
};

struct CalibrationData {
  uint16_t magic;
  uint8_t  version;
  uint8_t  profile;
  float    scaleFactor;
  uint8_t  mapCount;
  CalPoint map[kMaxCalPoints];
  float    goodMinKg, deepMinKg;
  uint16_t crc;
};

void calibDefaults(CalibrationData& d);
uint16_t crc16(const uint8_t* p, size_t n);
size_t calibSerialize(const CalibrationData& d, uint8_t* out, size_t cap);
bool calibDeserialize(const uint8_t* in, size_t n, CalibrationData& d);
float calibMapKgToMm(const CalibrationData& d, float kg);
uint8_t calibFit(const CalPoint* pairs, uint16_t n, CalibrationData& d);

class CalRecorder {
 public:
  CalRecorder();
  void reset();
  void add(float kg, float mm);
  uint16_t count() const;
  const CalPoint* pairs() const;

 private:
  CalPoint pairs_[CPR_CAL_PAIRS];
  uint16_t count_;
};

} // namespace cpr

#endif

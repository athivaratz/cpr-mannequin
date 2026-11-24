#include "cpr_calib.h"

#include <math.h>
#include <string.h>

namespace cpr {

namespace {

const size_t kSerializedSize = 2 + 1 + 1 + 4 + 1 + (kMaxCalPoints * 8) + 4 + 4 + 2;

void putU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void putU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint16_t getU16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t getU32(const uint8_t* p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

void putFloat(uint8_t* p, float f) {
  uint32_t bits = 0;
  memcpy(&bits, &f, sizeof(bits));
  putU32(p, bits);
}

float getFloat(const uint8_t* p) {
  uint32_t bits = getU32(p);
  float f = 0.0f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

void refreshCrc(CalibrationData& d) {
  uint8_t tmp[kSerializedSize];
  size_t n = calibSerialize(d, tmp, sizeof(tmp));
  if (n >= 2) {
    d.crc = getU16(&tmp[n - 2]);
  }
}

} // namespace

void calibDefaults(CalibrationData& d) {
  d.magic = kCalMagic;
  d.version = kCalVersion;
  d.profile = (uint8_t)Profile::Adult;
  d.scaleFactor = -10000.0f; // HX711 starting scale from the old sketches
  d.mapCount = 0;
  for (uint8_t i = 0; i < kMaxCalPoints; ++i) {
    d.map[i].kg = 0.0f;
    d.map[i].mm = 0.0f;
  }
  d.goodMinKg = 6.0f;  // existing sketches
  d.deepMinKg = 12.0f; // existing sketches
  d.crc = 0;
  refreshCrc(d);
}

uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= (uint16_t)p[i] << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
      else crc = (uint16_t)(crc << 1);
    }
  }
  return crc;
}

size_t calibSerialize(const CalibrationData& d, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kSerializedSize) return 0;

  uint8_t* p = out;
  putU16(p, d.magic); p += 2;
  *p++ = d.version;
  *p++ = d.profile;
  putFloat(p, d.scaleFactor); p += 4;
  *p++ = d.mapCount;
  for (uint8_t i = 0; i < kMaxCalPoints; ++i) {
    putFloat(p, d.map[i].kg); p += 4;
    putFloat(p, d.map[i].mm); p += 4;
  }
  putFloat(p, d.goodMinKg); p += 4;
  putFloat(p, d.deepMinKg); p += 4;

  size_t n = (size_t)(p - out);
  uint16_t crc = crc16(out, n);
  putU16(p, crc);
  return n + 2;
}

bool calibDeserialize(const uint8_t* in, size_t n, CalibrationData& d) {
  if (in == nullptr || n < kSerializedSize) return false;

  if (getU16(in) != kCalMagic) return false;
  if (in[2] != kCalVersion) return false;

  size_t payload = kSerializedSize - 2;
  uint16_t stored = getU16(in + payload);
  if (crc16(in, payload) != stored) return false;

  const uint8_t* p = in + 3;
  CalibrationData t;
  t.magic = kCalMagic;
  t.version = in[2];
  t.profile = *p++;
  t.scaleFactor = getFloat(p); p += 4;
  t.mapCount = *p++;
  if (t.mapCount > kMaxCalPoints) return false;
  if (t.profile > (uint8_t)Profile::Child) return false;
  if (isnan(t.scaleFactor) || t.scaleFactor == 0.0f) return false;

  for (uint8_t i = 0; i < kMaxCalPoints; ++i) {
    t.map[i].kg = getFloat(p); p += 4;
    t.map[i].mm = getFloat(p); p += 4;
    if (isnan(t.map[i].kg) || isnan(t.map[i].mm)) return false;
  }
  t.goodMinKg = getFloat(p); p += 4;
  t.deepMinKg = getFloat(p); p += 4;
  if (isnan(t.goodMinKg) || isnan(t.deepMinKg)) return false;
  if (t.goodMinKg <= 0.0f || t.deepMinKg <= t.goodMinKg) return false;

  for (uint8_t i = 1; i < t.mapCount; ++i) {
    if (t.map[i].kg < t.map[i - 1].kg) return false;
    if (t.map[i].mm <= t.map[i - 1].mm) return false;
  }

  t.crc = stored;
  d = t;
  return true;
}

float calibMapKgToMm(const CalibrationData& d, float kg) {
  if (d.mapCount < 2) return NAN;
  if (isnan(kg)) return NAN;

  if (kg <= d.map[0].kg) return d.map[0].mm;
  if (kg >= d.map[d.mapCount - 1].kg) return d.map[d.mapCount - 1].mm;

  for (uint8_t i = 1; i < d.mapCount; ++i) {
    if (kg <= d.map[i].kg) {
      float k0 = d.map[i - 1].kg;
      float k1 = d.map[i].kg;
      float m0 = d.map[i - 1].mm;
      float m1 = d.map[i].mm;
      float span = k1 - k0;
      if (span <= 0.0f) return m1;
      return m0 + (m1 - m0) * (kg - k0) / span;
    }
  }
  return d.map[d.mapCount - 1].mm;
}

uint8_t calibFit(const CalPoint* pairs, uint16_t n, CalibrationData& d) {
  if (pairs == nullptr || n == 0) return 0;

  float minKg = 0.0f;
  float maxKg = 0.0f;
  bool have = false;
  uint16_t valid = 0;
  for (uint16_t i = 0; i < n; ++i) {
    float kg = pairs[i].kg;
    float mm = pairs[i].mm;
    if (isnan(kg) || isnan(mm) || kg <= 0.0f || mm < 0.0f) continue;
    if (!have) {
      minKg = maxKg = kg;
      have = true;
    } else {
      if (kg < minKg) minKg = kg;
      if (kg > maxKg) maxKg = kg;
    }
    valid++;
  }
  if (!have || valid < 2 || maxKg <= minKg) return 0;

  float kgSum[kMaxCalPoints];
  float mmSum[kMaxCalPoints];
  uint16_t bCount[kMaxCalPoints];
  for (uint8_t i = 0; i < kMaxCalPoints; ++i) {
    kgSum[i] = 0.0f;
    mmSum[i] = 0.0f;
    bCount[i] = 0;
  }

  float width = (maxKg - minKg) / (float)kMaxCalPoints;
  for (uint16_t i = 0; i < n; ++i) {
    float kg = pairs[i].kg;
    float mm = pairs[i].mm;
    if (isnan(kg) || isnan(mm) || kg <= 0.0f || mm < 0.0f) continue;

    uint8_t bin;
    if (kg <= minKg) bin = 0;
    else if (kg >= maxKg) bin = kMaxCalPoints - 1;
    else bin = (uint8_t)((kg - minKg) / width);
    if (bin >= kMaxCalPoints) bin = kMaxCalPoints - 1;

    kgSum[bin] += kg;
    mmSum[bin] += mm;
    bCount[bin]++;
  }

  uint8_t outCount = 0;
  float prevMm = -1.0f;
  for (uint8_t i = 0; i < kMaxCalPoints; ++i) {
    if (bCount[i] == 0) continue;
    float kg = kgSum[i] / (float)bCount[i];
    float mm = mmSum[i] / (float)bCount[i];
    if (outCount > 0 && mm <= prevMm) mm = prevMm + 0.1f;
    d.map[outCount].kg = kg;
    d.map[outCount].mm = mm;
    prevMm = mm;
    outCount++;
  }

  if (outCount < 2) return 0;

  for (uint8_t i = outCount; i < kMaxCalPoints; ++i) {
    d.map[i].kg = 0.0f;
    d.map[i].mm = 0.0f;
  }
  d.mapCount = outCount;
  refreshCrc(d);
  return outCount;
}

CalRecorder::CalRecorder() { reset(); }

void CalRecorder::reset() { count_ = 0; }

void CalRecorder::add(float kg, float mm) {
  if (isnan(kg) || isnan(mm) || kg < 0.3f || mm < 0.0f) return;

  if (count_ >= CPR_CAL_PAIRS) {
    uint16_t j = 0;
    for (uint16_t i = 0; i < count_; i = (uint16_t)(i + 2)) {
      pairs_[j++] = pairs_[i];
    }
    count_ = j;
  }

  if (count_ < CPR_CAL_PAIRS) {
    pairs_[count_].kg = kg;
    pairs_[count_].mm = mm;
    count_++;
  }
}

uint16_t CalRecorder::count() const { return count_; }

const CalPoint* CalRecorder::pairs() const { return pairs_; }

} // namespace cpr

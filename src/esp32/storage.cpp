#include "storage.h"

#include <Preferences.h>

#include <string.h>

namespace {

constexpr const char* kNamespace = "cpr";
constexpr const char* kCalKey = "cal";
constexpr const char* kThKey = "th";

// calibSerialize never writes more than 96 bytes (DESIGN 3.7).
constexpr size_t kCalBufferSize = 96;

// One version byte plus six little-endian floats.
constexpr size_t kThBufferSize = 1 + 6 * 4;
constexpr uint8_t kThVersion = 1;

void putFloat(uint8_t* p, float v) {
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  p[0] = (uint8_t)(u & 0xFF);
  p[1] = (uint8_t)((u >> 8) & 0xFF);
  p[2] = (uint8_t)((u >> 16) & 0xFF);
  p[3] = (uint8_t)((u >> 24) & 0xFF);
}

float getFloat(const uint8_t* p) {
  uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  float v;
  memcpy(&v, &u, sizeof(v));
  return v;
}

} // namespace

namespace storage {

bool loadCalibration(cpr::CalibrationData& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  uint8_t buffer[kCalBufferSize];
  const size_t length = prefs.getBytesLength(kCalKey);
  if (length == 0 || length > sizeof(buffer)) {
    prefs.end();
    return false;
  }

  const size_t read = prefs.getBytes(kCalKey, buffer, length);
  prefs.end();
  if (read != length) return false;

  return cpr::calibDeserialize(buffer, length, out);
}

bool saveCalibration(const cpr::CalibrationData& in) {
  uint8_t buffer[kCalBufferSize];
  const size_t length = cpr::calibSerialize(in, buffer, sizeof(buffer));
  if (length == 0) return false;

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  const size_t written = prefs.putBytes(kCalKey, buffer, length);
  prefs.end();
  return written == length;
}

bool loadUserTh(cpr::UserTh& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  uint8_t buffer[kThBufferSize];
  const size_t length = prefs.getBytesLength(kThKey);
  if (length != sizeof(buffer)) {
    prefs.end();
    return false;
  }

  const size_t read = prefs.getBytes(kThKey, buffer, length);
  prefs.end();
  if (read != length) return false;
  if (buffer[0] != kThVersion) return false;

  cpr::UserTh t;
  t.good = getFloat(buffer + 1);
  t.deep = getFloat(buffer + 5);
  t.over = getFloat(buffer + 9);
  t.contact = getFloat(buffer + 13);
  t.recoil = getFloat(buffer + 17);
  t.filt = getFloat(buffer + 21);
  if (!cpr::userThValid(t)) return false;

  out = t;
  return true;
}

bool saveUserTh(const cpr::UserTh& in) {
  if (!cpr::userThValid(in)) return false;

  uint8_t buffer[kThBufferSize];
  buffer[0] = kThVersion;
  putFloat(buffer + 1, in.good);
  putFloat(buffer + 5, in.deep);
  putFloat(buffer + 9, in.over);
  putFloat(buffer + 13, in.contact);
  putFloat(buffer + 17, in.recoil);
  putFloat(buffer + 21, in.filt);

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  const size_t written = prefs.putBytes(kThKey, buffer, sizeof(buffer));
  prefs.end();
  return written == sizeof(buffer);
}

} // namespace storage

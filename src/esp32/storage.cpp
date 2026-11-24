#include "storage.h"

#include <Preferences.h>

#include <string.h>

namespace {

constexpr const char* kNamespace = "cpr";
constexpr const char* kCalKey = "cal";
constexpr const char* kWifiKey = "wifipass";

// calibSerialize never writes more than 96 bytes (DESIGN 3.7).
constexpr size_t kCalBufferSize = 96;

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

bool loadWifiPass(char* out, size_t cap) {
  if (out == nullptr || cap < 9) return false;

  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  const size_t length = prefs.getString(kWifiKey, out, cap);
  prefs.end();
  if (length < 9 || length > 64) return false;
  return true;
}

bool saveWifiPass(const char* pass) {
  if (pass == nullptr) return false;
  const size_t length = strlen(pass);
  if (length < 8 || length > 63) return false;

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  const size_t written = prefs.putString(kWifiKey, pass);
  prefs.end();
  return written == length;
}

} // namespace storage

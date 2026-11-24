#include "storage.h"

#include <EEPROM.h>

namespace {

// The calibration blob starts at address 0. calibSerialize never writes more
// than 96 bytes (DESIGN 3.7).
constexpr int kCalAddress = 0;
constexpr size_t kCalBufferSize = 96;

} // namespace

namespace storage {

bool loadCalibration(cpr::CalibrationData& out) {
  if ((size_t)EEPROM.length() < kCalBufferSize) return false;

  uint8_t buffer[kCalBufferSize];
  for (size_t i = 0; i < kCalBufferSize; ++i) {
    buffer[i] = EEPROM.read(kCalAddress + (int)i);
  }

  return cpr::calibDeserialize(buffer, kCalBufferSize, out);
}

bool saveCalibration(const cpr::CalibrationData& in) {
  if ((size_t)EEPROM.length() < kCalBufferSize) return false;

  uint8_t buffer[kCalBufferSize];
  const size_t length = cpr::calibSerialize(in, buffer, sizeof(buffer));
  if (length == 0) return false;

  for (size_t i = 0; i < length; ++i) {
    EEPROM.update(kCalAddress + (int)i, buffer[i]);
  }

  return true;
}

} // namespace storage

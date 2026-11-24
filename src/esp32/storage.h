#ifndef CPR_ESP32_STORAGE_H
#define CPR_ESP32_STORAGE_H

#include <stddef.h>

#include "cpr_calib.h"

namespace storage {

bool loadCalibration(cpr::CalibrationData& out);
bool saveCalibration(const cpr::CalibrationData& in);

bool loadWifiPass(char* out, size_t cap);
bool saveWifiPass(const char* pass);

} // namespace storage

#endif

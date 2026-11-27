#ifndef CPR_ESP32_STORAGE_H
#define CPR_ESP32_STORAGE_H

#include <stddef.h>

#include "cpr_calib.h"
#include "cpr_settings.h"

namespace storage {

bool loadCalibration(cpr::CalibrationData& out);
bool saveCalibration(const cpr::CalibrationData& in);

bool loadUserTh(cpr::UserTh& out);
bool saveUserTh(const cpr::UserTh& in);

bool loadWifiPass(char* out, size_t cap);
bool saveWifiPass(const char* pass);

bool loadStaConfig(char* ssid, size_t ssidCap, char* pass, size_t passCap);
bool saveStaConfig(const char* ssid, const char* pass);
bool clearStaConfig();

} // namespace storage

#endif

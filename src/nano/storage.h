#ifndef CPR_NANO_STORAGE_H
#define CPR_NANO_STORAGE_H

#include "cpr_calib.h"

namespace storage {

bool loadCalibration(cpr::CalibrationData& out);
bool saveCalibration(const cpr::CalibrationData& in);

} // namespace storage

#endif

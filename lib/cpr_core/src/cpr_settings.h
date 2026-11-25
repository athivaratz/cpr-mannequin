#ifndef CPR_SETTINGS_H
#define CPR_SETTINGS_H

#include "cpr_types.h"

namespace cpr {

// User-tunable kg thresholds. The rubber-band damper keeps the cell near 0 kg
// until the plate has travelled about 3 cm, so contact and recoil are low.
struct UserTh {
  float good, deep, over, contact, recoil, filt;
};

// Engineering values, tunable for the final build.
void userThDefaults(UserTh& t);

// Range checks: 0 <= recoil < contact, 0.1 <= contact <= 10, contact < good,
// 0.5 <= good < deep < over <= 60, 0.2 <= filt <= 1.0, all finite.
bool userThValid(const UserTh& t);

float userThRelease(const UserTh& t);

void userThApply(const UserTh& t, Config& c);

} // namespace cpr

#endif

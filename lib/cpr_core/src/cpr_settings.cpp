#include "cpr_settings.h"

#include <math.h>

namespace cpr {

void userThDefaults(UserTh& t) {
  // Engineering values, tunable. The damper means the cell only reads a real
  // force in the last part of the press.
  t.good = 6.0f;
  t.deep = 12.0f;
  t.over = 18.0f;
  t.contact = 0.5f;
  t.recoil = 0.2f;
  t.filt = 1.0f;  // no extra smoothing; at 10 SPS any lag hides the recoil
}

bool userThValid(const UserTh& t) {
  if (!isfinite(t.good) || !isfinite(t.deep) || !isfinite(t.over) ||
      !isfinite(t.contact) || !isfinite(t.recoil) || !isfinite(t.filt)) {
    return false;
  }

  // Engineering limits, tunable for the final mannequin.
  if (t.recoil < 0.0f || t.recoil >= t.contact) return false;
  if (t.contact < 0.1f || t.contact > 10.0f) return false;
  if (t.good < 0.5f || t.good <= t.contact) return false;
  if (t.deep <= t.good) return false;
  if (t.over <= t.deep || t.over > 60.0f) return false;
  if (t.filt < 0.2f || t.filt > 1.0f) return false;
  return true;
}

float userThRelease(const UserTh& t) {
  return (t.recoil + t.contact) * 0.5f;
}

void userThApply(const UserTh& t, Config& c) {
  c.th.goodMinKg = t.good;
  c.th.deepMinKg = t.deep;
  c.th.overloadKg = t.over;
  c.th.contactKg = t.contact;
  c.th.recoilKg = t.recoil;
  c.th.releaseKg = userThRelease(t);
  c.filterAlpha = t.filt;
  c.th.restartDeltaKg = (t.contact > 0.3f) ? t.contact : 0.3f;
}

} // namespace cpr

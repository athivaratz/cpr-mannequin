#include "cpr_profiles.h"

namespace cpr {

Config defaultConfig(Profile p) {
  Config c;
  c.profile = p;

  c.th.goodMinMm = 50.0f;   // AHA: at least 5 cm; child about 5 cm
  c.th.deepMinMm = 60.0f;   // AHA adult maximum 6 cm; child uses the same ceiling

  c.th.contactMm = 10.0f;   // engineering value, tunable: starts a press
  c.th.releaseMm = 6.0f;    // engineering value, tunable: release hysteresis
  c.th.recoilMm = 5.0f;     // engineering value, tunable: full-recoil line
  c.th.restartDeltaMm = 8.0f; // engineering value, tunable: leaning re-rise

  c.th.contactKg = 1.5f;    // from the existing sketches
  c.th.releaseKg = 0.8f;    // from the existing sketches
  c.th.recoilKg = 0.5f;     // from the existing sketches
  c.th.goodMinKg = 6.0f;    // from the existing sketches
  c.th.deepMinKg = 12.0f;   // from the existing sketches
  c.th.overloadKg = 18.0f;  // from the existing sketches
  c.th.restartDeltaKg = 1.0f; // engineering value, tunable

  c.th.peakDropFrac = 0.15f; // engineering value, tunable
  c.th.rateMin = 100;        // AHA
  c.th.rateMax = 120;        // AHA
  c.th.handsOffMs = 2000;    // engineering value, tunable
  c.th.maxPauseMs = 10000;   // AHA: interruptions under 10 s
  c.th.cycleCompressions = 30; // AHA single-rescuer ratio
  c.th.lowSpsWarn = 15;      // engineering value, tunable

  c.filterAlpha = 0.5f;      // engineering value, tunable
  c.metronomeBpm = 110;      // centre of the AHA 100-120 range

  return c;
}

} // namespace cpr

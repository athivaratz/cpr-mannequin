#ifndef CPR_JSON_H
#define CPR_JSON_H

#include "cpr_calib.h"
#include "cpr_types.h"

#include <stddef.h>

// The Nano has almost no flash left, so it leaves out the extra status keys.
#ifndef CPR_JSON_EXTRA
#if defined(CPR_BOARD_NANO)
#define CPR_JSON_EXTRA 0
#else
#define CPR_JSON_EXTRA 1
#endif
#endif

namespace cpr {

// Longest line any writer may produce. The worst-case sum message is about
// 260 bytes; 280 leaves room for larger values. See docs/OPEN_ISSUES.md.
static const size_t kMaxJsonLine = 280;

size_t jsonFrame(char* buf, size_t cap, const LiveState& s);
size_t jsonEvent(char* buf, size_t cap, const Event& e);
size_t jsonSummary(char* buf, size_t cap, const SessionStats& s, Mode m, Profile p);
size_t jsonConfig(char* buf, size_t cap, const Config& c, const CalibrationData& cal,
                  bool tofPresent, const char* board, const char* fw,
                  bool hxPresent = true, bool handSensor = true);
size_t jsonAck(char* buf, size_t cap, const char* cmd, bool ok, const char* msg);

} // namespace cpr

#endif

#ifndef CPR_JSON_H
#define CPR_JSON_H

#include "cpr_calib.h"
#include "cpr_types.h"

#include <stddef.h>

namespace cpr {

size_t jsonFrame(char* buf, size_t cap, const LiveState& s);
size_t jsonEvent(char* buf, size_t cap, const Event& e);
size_t jsonSummary(char* buf, size_t cap, const SessionStats& s, Mode m, Profile p);
size_t jsonConfig(char* buf, size_t cap, const Config& c, const CalibrationData& cal,
                  bool tofPresent, const char* board, const char* fw);
size_t jsonAck(char* buf, size_t cap, const char* cmd, bool ok, const char* msg);

} // namespace cpr

#endif

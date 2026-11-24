#ifndef CPR_COMMAND_H
#define CPR_COMMAND_H

#include "cpr_types.h"

#include <stdint.h>

namespace cpr {

enum class Cmd : uint8_t { None, Tare, Profile, Mode, Start, Stop, MetroOn, MetroOff,
  CalStart, CalFinish, CalCancel, CalWeight, CalBands, CalPeak, CalSave, CalReset,
  Config, WifiPass, Unknown };

struct Command {
  Cmd cmd;
  uint8_t arg;
  float f1, f2;
  char text[64];
};

bool parseCommand(const char* line, Command& out);

} // namespace cpr

#endif

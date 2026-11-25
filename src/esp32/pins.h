#ifndef CPR_ESP32_PINS_H
#define CPR_ESP32_PINS_H

#include <stdint.h>

#ifndef CPR_HAS_FOIL
#define CPR_HAS_FOIL 1
#endif

namespace pins {

// HX711 load cell.
constexpr uint8_t kHx711Dout = 16;
constexpr uint8_t kHx711Sck = 17;

// Buzzer.
constexpr uint8_t kBuzzer = 25;

// LEDs, active high.
constexpr uint8_t kLedBuiltin = 2;
constexpr uint8_t kLedPos = 26;
constexpr uint8_t kLedGood = 27;
constexpr uint8_t kLedShallow = 18;
constexpr uint8_t kLedDeep = 19;

// Foil switch, INPUT_PULLUP, closed = LOW.
constexpr uint8_t kFoil = 33;

// VL53L0X I2C.
constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;

} // namespace pins

#endif

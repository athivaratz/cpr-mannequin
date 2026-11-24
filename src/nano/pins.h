#ifndef CPR_NANO_PINS_H
#define CPR_NANO_PINS_H

#include <stdint.h>

namespace pins {

// HX711 load cell.
constexpr uint8_t kHx711Dout = 3;
constexpr uint8_t kHx711Sck = 2;

// Buzzer.
constexpr uint8_t kBuzzer = 8;

// LEDs, active high.
constexpr uint8_t kLedPos = 4;
constexpr uint8_t kLedGood = 5;
constexpr uint8_t kLedShallow = 6;
constexpr uint8_t kLedDeep = 7;

// Foil switch, INPUT_PULLUP, closed = LOW.
constexpr uint8_t kFoil = 9;

// VL53L0X I2C: the Nano's fixed A4/A5 pins.
constexpr uint8_t kI2cSda = 18;
constexpr uint8_t kI2cScl = 19;

} // namespace pins

#endif

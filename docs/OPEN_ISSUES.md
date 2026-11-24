# Open issues

## Simulator rate labels and the rate test

DESIGN.md §9 says the `fast` and `slow` scenarios use 135 and 85 cpm, while
§10 asks for generator runs at 90, 110 and 130 cpm. The generator keeps the §9
defaults. `cprsim::Generator::setRate()` lets the unit test run the same
waveform at 90 and 130 cpm without changing the public scenario set.

## Simulator cycle recording length

DESIGN.md §9 asks for a 75 s cycle recording. At the cycle scenario's 110 cpm
waveform, 30 presses take about 16.4 s and the 5 s breath pause makes a cycle
about 21.4 s, so 75 s only completes three cycles. The P5 acceptance check asks
for `cy >= 4` in the summary, so `make_recordings.sh` generates 90 s for
`recordings/cycle.jsonl`. The generator itself is unchanged and still uses the
§9 30-press / 5 s cycle.

## Simulator kg curve and the default kg bands (resolved)

DESIGN.md §9 now scales the generator's mm-to-kg curve by 0.7 by default:
`kg = 0.7*(0.2*mm + 0.0012*mm*mm)`. The 35, 54 and 68 mm scenario peaks map to
about 6.0, 10.0 and 13.4 kg, so the default §2.2 bands (6 kg / 12 kg) classify
the three depth scenarios the same way the mm bands do. `setKgScale()` is still
there for tests that want the unscaled curve. The engine and default profile
keep the §2 values.

## P2 sensor timing and range constants

These are engineering values, not clinical numbers:

- VL53L0X readings at or above 8000 mm are rejected as invalid
  (`kMaxValidRangeMm` in `src/esp32/sensors.cpp`). The useful range is about
  2 m, so this only filters bad I2C results.
- Each blocking ToF read during boot and `tare` uses a 100 ms timeout. The
  measurement budget is 20 ms, so this is generous but bounded.
- The HX711 tare averages 10 raw readings with a 1500 ms timeout
  (`Sensors::tareLoadCell`). The library's own `tare()` default is also 10
  readings; the timeout is ours so a dead sensor cannot hang the task.

All are tunable. Boot readiness (2 s), the 20-reading `cal weight` average (3 s),
the 10-sample ToF rest average, the 20 Hz frame period, and the buzzer durations
come from DESIGN.md.

## P3 Nano flash: VL53L0X does not fit

The Nano firmware is complete for both load-cell-only and ToF operation, but
`env:nano` builds with `USE_VL53L0X=0`. With the Pololu VL53L0X library and Wire
enabled, the same code is 35 682 bytes against the 30 720-byte application
limit of both Nano bootloader boards, and static RAM is 1 596 bytes (also over
the 1 500-byte target). The load-cell-only build is 30 076 bytes with 1 366
bytes of RAM. No combination of the normal size options closed a gap that
large.

`src/nano/sensors.cpp` still contains the high-speed, continuous, non-blocking
ToF path behind `#if USE_VL53L0X`. Build with `-DUSE_VL53L0X=1` to exercise it
on a board with more flash, or after freeing roughly 4 KB elsewhere.

## P3 Nano RAM and core strings

To keep static RAM under 1 500 bytes, the AVR build stores the JSON field names
and command words in flash (`cpr_json.cpp`, `cpr_command.cpp`) and the Nano
sketch copies its short ack strings out of flash before calling `jsonAck`. The
native build and the ESP32 build use the same code paths without the AVR
attribute. The Nano report is 1 366 bytes of data+bss.

## P4 calibration wizard live counter

`PROTOCOL.md` has no message for the calibration recorder's live `calCount()`,
so the dashboard cannot show the exact number of (kg, mm) pairs while
`cal start` is running. The wizard counts `c` (compression) events since
`cal start` and labels the number "ครั้งที่กด". The device still records every
sample pair and `cal finish` uses the full set; only the live display is
approximate.

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
- The HX711 start-up waits up to 3000 ms for `is_ready()`, then discards four
  conversions and tares with a stability check (two averages of 8 raw readings;
  accept when their difference is at most `max(200 counts, |scale|*0.05)`).
  `Sensors::service()` does the same recovery one conversion per call, so it
  never blocks the sensor task. At the stock 10 SPS rate a full tare needs
  about 1.6 s; `begin()` allows up to 3 s for retries. `Sensors::tareLoadCell()`
  uses the same routine with a 1500 ms budget.
- Once a tare has succeeded, a loss and recovery only discards four conversions
  and keeps the existing offset instead of taring again, so a load already on
  the plate (after `cal weight` or a loss while someone is pressing) is not
  zeroed. Only a sensor that has never tared runs the stable-tare steps during
  recovery.

All are tunable. Boot readiness (3 s), the 20-reading `cal weight` average
(3 s), the 10-sample ToF rest average, the 20 Hz frame period, and the buzzer
durations come from DESIGN.md or the H1 phase brief.

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

## P7 JSON line length: 280 bytes, not 200 or 240

DESIGN.md §3.13 says every JSON message is at most 200 bytes. That is already
too small for `recordings/cycle.jsonl`, whose `sum` line is 210 bytes, so the
P7 review asked for a 240-byte limit. A constructed worst-case `SessionStats`
(every counter 65535, every uint32 4294967295, peak 999.9, cycles 255) produces
a `sum` line of 259 bytes, so 240 still would not hold it.

We used `cpr::kMaxJsonLine = 280` in `cpr_json.h`. The ESP32 output queue slot
and line buffers are `kMaxJsonLine + 8` (288 bytes); the single Nano line
buffer is `kMaxJsonLine + 1` (281 bytes). The Nano static RAM is still under
the 1500-byte target. Protocol consumers should read with a 288-byte buffer.
DESIGN.md was not edited, as the review required.

## H1 kg-only demo at 10 SPS

`recordings/kgonly_good.jsonl` is generated at 10 SPS with no ToF because that
is what the final board runs. With the old thresholds and filter about 40% of
the peaks came out shallow and no recoil was counted; H2 fixed both (see
below), and `make_recordings.sh` checks depth and recoil at 0.80 again.

## H2 user thresholds and the 10 SPS recoil result

The kg thresholds are now user-tunable on the ESP32 and stored in Preferences.
The defaults (contact 0.5 kg, recoil 0.2 kg, good 6 kg, deep 12 kg, overload
18 kg, filter alpha 1.0) and the accepted ranges are engineering values for the
rubber-band damper, not clinical numbers. They are tunable for each build.

`recordings/kgonly_good.jsonl` uses `--esp32`, so it runs with these defaults
and the median switched off (the ESP32 does that by itself below 20 samples/s).
With filter alpha 0.7 recoil came out 0/73: at 10 SPS the trough between two
presses is only one or two samples, and the EMA lag kept the value above the
release line, so the press ended through the leaning re-rise path with
`recoilOk = false`. With alpha 1.0 (raw value, no smoothing) the same recording
gives 73/73 recoil and 73/73 depth, while `norecoil` still scores 0 recoil, so
the check still tells the two apart. That's why the default is 1.0. The
simulator waveform has no rubber bands, so the real mannequin still needs a
check on hardware.

## H4 junk-on-RX explanation is unproven on hardware

The "old firmware could stall on USB-chip noise when no computer was listening"
explanation behind the `หน้าเว็บค้างที่ 0` section and the `diag` `rxBad` value
is the best fit for the symptom, but it has not been checked on a real charger-only
board yet. Read the diag line on actual hardware before treating it as settled.

## H4 threshold defaults are guesses for the damper build

The default user thresholds (contact 0.5 kg, recoil 0.2 kg, good 6 kg,
deep 12 kg, overload 18 kg, filter 1.0) were chosen for the rubber-band damper.
They are engineering values, not measurements from a finished mannequin. Each
build should tune them with `docs/calibration.md` after the bands settle.

## H4 recoil at 120/min with 10 SPS

Fast presses around 120/min can still show `ยังค้าง` with a stock 10 SPS HX711,
because there may be only one or two samples in the trough. Raising the recoil
line a little or setting ความไว to ไว helps. If that is not enough, the RATE pin
mod for 80 SPS is still the reliable fix.

## Hardware check: load-cell polarity, Serial default, and HX711 boot retry

Testing on the real ESP32 (`esp32dev`, CP210x on COM3) showed three things:

- The default `scaleFactor` of `-10000.0` (from DESIGN.md §2.2) had the opposite
  sign to this mannequin's load-cell wiring/mounting, so pressing read negative
  kg (down to `-5.7 kg`), leaving the detector idle (`ph=0`) and the dashboard
  clamped at `0.0`. `cpr::calibDefaults()` now starts at `+10000.0` so pressing
  reads positive before calibration; running `cal weight` still sets the exact
  signed scale for any wiring.
- Even after 12 minutes with Serial Monitor closed, the board was running and
  sampling at 12 SPS; UART0 on the CP210x has no hardware flow control, and
  `net.cpp` already checks `Serial.availableForWrite()`. `env:esp32dev` now
  builds with `-DCPR_SERIAL_STREAM_DEFAULT=1` so Serial Monitor shows live
  output immediately, and `SerialTransport.connect()` sends `config` on open so
  Web Serial works regardless of the default flag.
- `Sensors::begin()` now waits `kHxPowerUpDelayMs` (400 ms, engineering value,
  tunable) before resetting the HX711, uses `kHxReadTimeoutMs` = 500 ms (above
  the 400 ms 10-SPS reset settling time), and falls back to `HxState::Probe` if
  boot discard or tare fails so `Sensors::service()` retries taring instead of
  staying `Present` with `offset = 0`.

## USB dashboard mode, mutually exclusive Wi-Fi/USB, and built-in LED timings

These constants in `src/esp32/net.cpp` and `tools/usb_server.py` are
engineering values (tunable), not clinical numbers:

- `kUsbWebTimeoutMs = 10000` (10 s) lease timeout on ESP32: if no `usb active`
  heartbeat arrives within 10 s while in USB web mode, the board returns to
  Idle and re-enables the Wi-Fi AP.
- `USB_HEARTBEAT_S = 3.0` (3 s) heartbeat interval sent by `tools/usb_server.py`
  and `SerialTransport` in `web/app.js` while a USB web client is active.
- `kLedIdleHalfPeriodMs = 500`, `kLedWifiPeriodMs = 3000`,
  `kLedWifiLowMs = 500` on `pins::kLedBuiltin` (GPIO 2, active high).
- `usb active` and `usb idle` are handled directly in `src/esp32/net.cpp`
  (same pattern as `serial on` / `serial off`) so `lib/cpr_core` and Nano
  firmware remain unchanged.

## HX711 startup/reconnect zero-check, PD_SCK reboot, and Developer UI

Because the ESP32 wiring (`src/esp32/pins.h`) connects only `DT` (GPIO 16) and
`SCK` (GPIO 17) to the HX711 (VCC is tied directly to 3.3 V), rebooting the
HX711 uses the datasheet PD_SCK power-down reset (holding `PD_SCK` HIGH for
more than 60 us, then releasing LOW). These constants in
`src/esp32/sensors.cpp` and `web/app.js` are engineering values (tunable), not
clinical numbers:

- `kHxResetPulseMs = 10` (10 ms PD_SCK HIGH pulse to reset the HX711).
- `kHxBootMaxAttempts = 3` (boot-time power-cycle + tare + zero-check retries
  before falling back to non-blocking background recovery).
- `kHxZeroCheckSamples = 4` (post-tare samples verified before marking HX711
  present).
- `kHxZeroCheckMinKg = -1.0f`, `kHxZeroCheckMaxKg = 1.0f` (allowed resting
  weight range after tare on boot or hot-plug reconnect; out-of-range or
  saturated readings trigger a fresh tare or HX711 reboot).
- `ZERO_STUCK_WARN_MS = 5000` (5 s in `web/app.js`: warns when the live sensor
  reading stays at `0.00 kg` continuously while connected to hardware).
- `rkg`, `hxs`, and `hx reset` are handled in `src/esp32/` so `lib/cpr_core`
  and the Nano build remain untouched.

## ESP32 BLE-only + USB firmware and offline PWA dashboard (v2.0.0)

To let phones and iPads keep internet access and allow dashboard updates without
reflashing the ESP32, the ESP32 build (v2.0.0) removes Wi-Fi AP/STA, mDNS,
Captive DNS, AsyncWebServer, and embedded `web_assets.h`, keeping NimBLE GATT
UART (Web Bluetooth) and USB Serial only. The dashboard in `web/` is deployed
to GitHub Pages (HTTPS) with a PWA Service Worker (`web/sw.js` +
`web/manifest.json`) for full offline use. Deviations from `DESIGN.md` §1/§6:

- **Partition table**: stays on `default.csv` (1.25 MB app) because removing
  Wi-Fi, AsyncWebServer, and `web_assets.h` keeps the firmware well under 1 MB.
- **Dependencies**: `esp32async/AsyncTCP` and `esp32async/ESPAsyncWebServer`
  are removed; `h2zero/NimBLE-Arduino` (peripheral-only,
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`, central/observer roles disabled) is
  used for BLE GATT UART (NUS).
- **Web assets**: `extra_scripts = pre:tools/embed_web.py` is removed from
  `env:esp32dev`; the web dashboard is served from GitHub Pages (with offline
  PWA cache) or locally via `tools/usb_server.py` (`?ws=1`).
- **Mutual exclusion**: BLE and USB web modes are mutually exclusive on ESP32.
- **Engineering values (tunable, not clinical)**:
  - `kBlePreferredMtu = 256` in `src/esp32/ble.cpp`.
  - `kLedBlePeriodMs = 2000` (double short blink on GPIO 2 for BLE mode) in
    `src/esp32/net.cpp`.

# CPR training mannequin: design

This is the build spec. Code has to match it. If something here turns out to be
wrong or impossible, stop and write the problem down in `docs/OPEN_ISSUES.md`
rather than quietly doing something different.

What it is: a school project, a child-sized CPR practice dummy. A load cell sits
under the press plate, an optional VL53L0X measures plate travel, and a foil
switch tells whether the hands are on the right spot. The firmware judges four
things for every compression (depth, rate, recoil and hand position) and shows
the result on LEDs, a buzzer and a web dashboard.

It is a teaching aid. It is not a medical device, and every user-facing screen
and doc says so once, quietly.

---

## 1. Repository layout

```
platformio.ini            envs: esp32dev, nano, nano_oldbl, native
lib/cpr_core/             pure C++ algorithm (shared by all targets)
  library.json
  src/cpr_types.h         enums, Sample, Thresholds, Config, LiveState, events
  src/cpr_profiles.h/.cpp default thresholds for adult and child
  src/cpr_filter.h/.cpp   median-of-3 + EMA filter
  src/cpr_calib.h/.cpp    calibration map, CalibrationData + CRC, (de)serialise
  src/cpr_detector.h/.cpp compression state machine
  src/cpr_rate.h/.cpp     rate estimator
  src/cpr_stats.h/.cpp    session statistics and score
  src/cpr_training.h/.cpp 30:2 cycle tracker, 2-minute test timer
  src/cpr_metronome.h/.cpp
  src/cpr_samplerate.h/.cpp sample-rate meter (detects 10 SPS HX711)
  src/cpr_engine.h/.cpp   ties everything together
  src/cpr_json.h/.cpp     fixed-buffer JSON writer for frames and events
  src/cpr_command.h/.cpp  line-protocol command parser
  src/cpr.h               umbrella header
lib/cpr_sim/              native-only: synthetic waveform generator + scenarios
src/esp32/                ESP32 firmware (Wi-Fi AP, HTTP, WebSocket)
src/nano/                 Nano firmware (Serial only)
test/test_core/           Unity tests, run with `pio test -e native`
web/                      dashboard: index.html, app.css, app.js, fonts/, demo/
tools/embed_web.py        PlatformIO pre-script: gzip web/ into a PROGMEM header
tools/subset_font.py      makes web/fonts/anuphan-thai.woff2 from vendor/fonts
tools/sim/                simulator CLI (native C++) + build.sh
tools/devserver.py        serves web/ and replays a recording over WebSocket
tools/screenshots.py      Playwright screenshots of the dashboard
tools/verify.sh           builds every env and runs the tests
recordings/               simulator output (*.jsonl), committed
docs/                     DESIGN.md, PROTOCOL.md, Thai user docs, screenshots/
vendor/fonts/             Anuphan variable TTF + OFL licence (source for subset)
```

`lib/cpr_core` must compile with avr-gcc (C++11 subset that avr-gcc 7.3 accepts,
no `<vector>`, no `<string>`, no `<functional>`, no exceptions, no RTTI, no
`new`/`malloc`), with xtensa gcc 14 and with host g++/clang (C++17). Only
`<stdint.h>`, `<stddef.h>`, `<string.h>`, `<math.h>` and `<stdlib.h>` (for
`strtol`/`strtod` only) may be included. No Arduino headers. No `float` printf:
avr libc does not do `%f`, so the JSON writer formats numbers itself.

RAM budget for the `cpr::Engine` object: **at most 600 bytes** not counting the
calibration recorder (see 3.7). A native test asserts
`sizeof(Engine) - sizeof(CalRecorder) <= 600`; host padding is a little larger
than AVR, so this is conservative.

Arduino Nano board choice: the original `cpr_nano.ino` just said "Arduino Nano
(ATmega328P)". Arduino IDE and arduino-cli default to the new Optiboot
bootloader, so `env:nano` uses `nanoatmega328new`. Many cheap CH340 clones still
have the old bootloader; `env:nano_oldbl` (`nanoatmega328`) is there for them.
Both build the same code.

---

## 2. Feedback criteria and where the numbers come from

### 2.1 Clinical targets (verified 24 Nov 2026)

| What | Adult | Child (1 yr to puberty) | Source |
|---|---|---|---|
| Depth | at least 5 cm, no more than 6 cm | at least 1/3 of chest AP depth, about 5 cm | AHA 2025 adult BLS (Part 7, doi:10.1161/CIR.0000000000001369); AHA/AAP 2025 pediatric BLS (Part 6, doi:10.1161/CIR.0000000000001370); AHA "Summary of High-Quality CPR Components" © 2025 |
| Rate | 100–120 /min | 100–120 /min | same |
| Recoil | full recoil, do not lean | same | same |
| Interruptions | under 10 s, CCF at least 60% | under 10 s | AHA summary table (CCF ≥ 60%); Part 6 rec. 2 (pauses < 10 s) |
| Ratio, no advanced airway | 30:2 | 30:2 single rescuer, 15:2 two rescuers | same |
| Compressor swap | about every 2 min | | Part 7 (depth falls after 90–120 s) |

The pediatric guideline gives no upper depth limit for children. The child
profile uses the adolescent/adult ceiling of 6 cm as its "too deep" line. That
is a design choice, it is configurable, and the docs say so.

### 2.2 Values taken from the existing sketches

`cpr_esp32.ino` and `cpr_nano.ino` (and the plan page "แผนสร้างหุ่นฝึก CPR เด็ก")
use these force values for the load-cell-only setup. They are **mannequin
specific** (the plan explains there is no single "force for 5 cm" number in the
literature: Tomlinson 2007, Chi 2010, Gregson 2017, Niles 2011) and are meant to
be replaced after calibration.

| Constant | Value | Meaning |
|---|---|---|
| CONTACT_KG | 1.5 | a press starts |
| RELEASE_KG | 0.8 | press released (hysteresis) |
| RECOIL_OK_KG | 0.5 | counts as full recoil |
| GOOD_MIN_KG | 6.0 | start of the OK depth band |
| DEEP_MIN_KG | 12.0 | start of too deep |
| OVERLOAD_KG | 18.0 | warn before the 20 kg cell limit |
| RATE_MIN/MAX | 100 / 120 | |
| calFactor | -10000.0 | HX711 starting scale factor |
| Pins | see §6 and §7 | |

### 2.3 Engineering values (not clinical, tunable)

These are ours. They are not medical numbers; they only make the detector behave.
They live in `Thresholds` and the docs call them adjustable.

| Name | Default | Why |
|---|---|---|
| contactMm | 10 | ToF/mapped depth that starts a press |
| releaseMm | 6 | hysteresis for release |
| recoilMm | 5 | back within 5 mm of the resting plate = full recoil |
| restartDeltaMm / restartDeltaKg | 8 mm / 1.0 kg | a re-rise this big from a local minimum while still leaning starts the next press |
| peakDropFrac | 0.15 | signal must fall 15% below the running max to call the peak |
| handsOffMs | 2000 | a gap longer than this between presses counts as hands-off time |
| maxPauseMs | 10000 | from AHA "< 10 s" |
| filter alpha | 0.5 | EMA after median-of-3 |
| lowSpsWarn | 15 | below this sample rate the dashboard warns (HX711 at 10 SPS) |

---

## 3. `cpr_core` interfaces

Namespace `cpr`. All types below are exact; add private helpers freely, but do
not rename public members.

### 3.1 Types (`cpr_types.h`)

```cpp
namespace cpr {

enum class DepthLevel : uint8_t { None = 0, Shallow = 1, Good = 2, Deep = 3 };
enum class RateLevel  : uint8_t { None = 0, Slow = 1, Good = 2, Fast = 3 };
enum class Phase      : uint8_t { Idle = 0, Pressing = 1, Peak = 2, Releasing = 3 };
enum class Profile    : uint8_t { Adult = 0, Child = 1 };
enum class Mode       : uint8_t { Free = 0, Cycle30x2 = 1, Test2Min = 2 };
// Which signal the detector is using right now.
enum class Source     : uint8_t { ForceKg = 0, MappedMm = 1, TofMm = 2 };

struct Sample {
  uint32_t tMs;      // monotonic milliseconds
  float    kg;       // load cell, already tared and scaled; NAN if no reading
  int16_t  mm;       // ToF depth below the resting plate, >= 0; -1 if no reading
  bool     handOk;   // foil switch closed
};

struct Thresholds {
  // depth bands in mm (used for TofMm and MappedMm)
  float goodMinMm, deepMinMm;
  // detector, mm domain
  float contactMm, releaseMm, recoilMm, restartDeltaMm;
  // detector + bands, kg domain (ForceKg fallback)
  float contactKg, releaseKg, recoilKg, goodMinKg, deepMinKg, overloadKg, restartDeltaKg;
  float peakDropFrac;
  uint16_t rateMin, rateMax;      // per minute
  uint16_t handsOffMs, maxPauseMs;
  uint8_t  cycleCompressions;     // 30
  uint8_t  lowSpsWarn;            // 15
};

struct Config {
  Profile    profile;
  Thresholds th;
  float      filterAlpha;   // 0..1
  uint16_t   metronomeBpm;  // 110
};

// Per-compression result, emitted once recoil is known (see 3.4).
struct CompressionEvent {
  uint16_t   index;        // 1-based within the session
  uint32_t   tStartMs;
  uint32_t   tPeakMs;
  float      peak;         // peak value in the active unit (mm or kg)
  Source     source;
  DepthLevel depth;
  RateLevel  rate;         // None for the first compression after a pause
  uint8_t    bpm;          // 0 if unknown
  bool       recoilOk;
  bool       handOk;       // hand switch closed for >= half of the press samples
  bool       allOk;        // depth Good && rate Good-or-None && recoil && hand
};

enum class EventType : uint8_t { Compression = 0, PauseStart = 1, PauseEnd = 2,
                                 CycleDone = 3, SessionEnd = 4, Overload = 5 };

struct Event {
  EventType type;
  uint32_t  tMs;
  CompressionEvent c;      // valid for Compression
  uint32_t  pauseMs;       // PauseEnd: length of the pause; CycleDone: breath pause length
  uint8_t   cycle;         // CycleDone: cycles completed so far
  bool      pauseTooLong;  // PauseEnd / CycleDone: pause > maxPauseMs
};

struct LiveState {
  uint32_t   tMs;
  float      value;        // filtered signal in the active unit
  float      kg;           // filtered kg (NAN if absent)
  int16_t    mm;           // latest ToF/mapped mm, -1 if none
  Source     source;
  Phase      phase;
  DepthLevel liveDepth;    // level of the current value while pressing, None when idle
  uint8_t    bpm;          // current rate estimate, 0 if unknown
  RateLevel  rate;
  bool       handOk;
  bool       overload;
  bool       inPause;      // no press for > handsOffMs while a session is running
  uint16_t   count;        // compressions this session
  uint8_t    cycleCount;   // 0..30 inside the current 30:2 cycle
  uint8_t    cycles;       // finished 30:2 cycles
  uint8_t    sps;          // measured sample rate, samples per second
  bool       lowSps;       // sps < lowSpsWarn (after at least 2 s of data)
  bool       sessionActive;
  uint16_t   elapsedS;     // seconds since the session's first compression
};

struct SessionStats {
  uint16_t count;
  uint16_t depthGood, depthShallow, depthDeep;
  uint16_t rateRated, rateGood, rateSlow, rateFast;  // rateRated = compressions with a rate
  uint16_t recoilGood;
  uint16_t handGood;
  uint16_t allGood;
  uint32_t firstMs, lastMs;      // first press start, last event time
  uint32_t handsOffMs;           // sum of pauses longer than handsOffMs
  uint32_t longestPauseMs;
  uint16_t pausesTooLong;
  float    sumPeak;              // for the mean peak
  uint32_t sumBpm;               // over rated compressions
  uint8_t  cycles;
};

} // namespace cpr
```

### 3.2 Profiles (`cpr_profiles.h`)

```cpp
Config defaultConfig(Profile p);
```

Adult: goodMinMm 50, deepMinMm 60 (AHA). Child: goodMinMm 50 (AHA "about 5 cm"),
deepMinMm 60 (adolescent ceiling, see 2.1). Both: kg values and rate from §2.2,
engineering values from §2.3, cycleCompressions 30, metronomeBpm 110,
filterAlpha 0.5. Every number gets a short source comment.

### 3.3 Filter (`cpr_filter.h`)

```cpp
class Filter {
 public:
  void reset();
  void setAlpha(float a);
  float push(float x);   // median of the last 3 raw values, then EMA; returns filtered
  float value() const;
};
```
The first sample initialises the EMA directly (no ramp from 0). NAN input is
ignored and returns the previous value.

### 3.4 Detector (`cpr_detector.h`)

Works on one scalar signal `v` (mm or kg) with thresholds `contact`, `release`,
`recoil`, `restartDelta`, `peakDropFrac`.

States: `Idle → Pressing → Peak → Releasing → (Idle | Pressing)`.

- Idle: `v >= contact` → Pressing. Record `tStart`, reset `max`, hand counters.
- Pressing: track `max`. When `v <= max * (1 - peakDropFrac)` and `max >= contact`
  → Peak (`tPeak` = time of max).
- Peak: is a single-sample state; the next sample moves to Releasing.
- Releasing: track the local minimum `lo`.
  - `v <= release` → Idle (press finished, `released = true`).
  - `v >= lo + restartDelta` while `lo > release` → the user is leaning and has
    started the next press: finish the current press with `recoilOk = false`,
    and immediately start a new one (Pressing, new `tStart = now`).
- Recoil: after a press finishes, the pending result waits:
  - `v <= recoil` → `recoilOk = true`, emit now.
  - next press starts first (Idle → Pressing with `v` never at or below
    `recoil`) → `recoilOk = false`, emit before starting the new press.
  - `handsOffMs` passes without either → evaluate with what we have
    (`min since release <= recoil`), emit.
- Hand: count samples in the press with `handOk` true vs total; `handOk` for
  the press = `onSamples * 2 >= totalSamples`.

```cpp
struct PressResult { uint32_t tStartMs, tPeakMs; float peak; bool recoilOk; bool handOk; };

class Detector {
 public:
  void reset();
  void setThresholds(float contact, float release, float recoil,
                     float restartDelta, float peakDropFrac, uint16_t handsOffMs);
  // Feed one filtered sample. Returns true when a finished press result is ready.
  bool update(uint32_t tMs, float v, bool handOk);
  const PressResult& result() const;   // valid right after update() returned true
  Phase phase() const;
  bool pressStartedThisSample() const; // true on the sample that entered Pressing
  uint32_t lastPressStartMs() const;
};
```
`update` can produce at most one result per call. If a forced finish and a new
press start happen on the same sample, the result is returned on that call and
`pressStartedThisSample()` is also true.

### 3.5 Rate (`cpr_rate.h`)

```cpp
class RateEstimator {
 public:
  void reset();                  // also called when a pause starts
  void addPress(uint32_t tStartMs);
  uint8_t bpm() const;           // 0 until 2 presses; else 60000*(n-1)/(last-first)
                                 // over the last up-to-5 press starts, rounded, clamped 0..250
};
RateLevel classifyRate(uint8_t bpm, const Thresholds& th); // 0 → None
```

### 3.6 Classification

- Depth level from `peak`: `< goodMin` Shallow, `>= deepMin` Deep, otherwise Good
  (mm domain uses goodMinMm/deepMinMm, kg domain goodMinKg/deepMinKg).
- Live depth level while Pressing/Peak/Releasing uses the current value the same way.
- Overload: filtered kg `>= overloadKg`; emits one Overload event on the rising edge.

### 3.7 Calibration (`cpr_calib.h`)

```cpp
static const uint8_t kMaxCalPoints = 8;
struct CalPoint { float kg; float mm; };
struct CalibrationData {
  uint16_t magic;        // 0xC9A1
  uint8_t  version;      // 1
  uint8_t  profile;      // Profile
  float    scaleFactor;  // HX711 set_scale value, default -10000.0
  uint8_t  mapCount;     // 0 = no force->depth map
  CalPoint map[kMaxCalPoints];  // sorted by kg ascending, mm strictly increasing
  float    goodMinKg, deepMinKg; // kg bands when there is no map
  uint16_t crc;          // CRC-16/CCITT-FALSE over every byte before crc
};
void calibDefaults(CalibrationData& d);
uint16_t crc16(const uint8_t* p, size_t n);
size_t calibSerialize(const CalibrationData& d, uint8_t* out, size_t cap);  // fixed little-endian layout, returns bytes or 0
bool calibDeserialize(const uint8_t* in, size_t n, CalibrationData& d);    // checks magic, version, crc, sanity
float calibMapKgToMm(const CalibrationData& d, float kg);  // piecewise linear, clamps; NAN if mapCount < 2
// Fit a map from recorded (kg, mm) pairs: bucket into up to kMaxCalPoints kg bins,
// average each bin, enforce monotonic mm. Returns number of points (>= 2 on success).
uint8_t calibFit(const CalPoint* pairs, uint16_t n, CalibrationData& d);
```
The serialised layout is explicit byte packing (not `memcpy` of the struct) so
AVR and ESP32 agree. Size must be `<= 96` bytes.

Calibration recorder (used by the engine's calibrate mode):

```cpp
class CalRecorder {   // keeps at most 48 pairs, decimating when full
 public:
  void reset();
  void add(float kg, float mm);  // ignores pairs with kg < 0.3 or mm < 0
  uint16_t count() const;
  const CalPoint* pairs() const;
};
```
On Nano the recorder lives inside the engine too; 48 × 8 bytes = 384 bytes is
too much for the Nano budget, so `CalRecorder` capacity is a template-free
compile-time constant `CPR_CAL_PAIRS` defaulting to 48, and the Nano build sets
`-DCPR_CAL_PAIRS=16`. The 600-byte engine limit excludes the recorder.

### 3.8 Training (`cpr_training.h`)

```cpp
class Training {
 public:
  void reset(Mode m);
  // Called by the engine. Returns true and fills `ev` when something happened.
  bool onCompression(uint32_t tMs, Event& ev);
  bool onPauseEnd(uint32_t tMs, uint32_t pauseMs, const Thresholds& th, Event& ev);
  bool tick(uint32_t tMs, uint32_t sessionStartMs, Event& ev); // Test2Min end
  uint8_t cycleCount() const;
  uint8_t cycles() const;
  bool awaitingBreaths() const;  // 30 done, waiting for the pause
};
```
Cycle30x2: count presses. At `cycleCompressions` set `awaitingBreaths`. The next
pause (PauseEnd) while awaiting completes the cycle: emit CycleDone with the
pause length and `pauseTooLong = pauseMs > maxPauseMs`, reset the count. If
presses continue past 30 without a pause, keep counting (the dashboard shows
"เกิน 30 ครั้ง"); the next pause still closes the cycle.

Test2Min: SessionEnd 120 000 ms after the session's first press.

### 3.9 Metronome (`cpr_metronome.h`)

```cpp
class Metronome {
 public:
  void start(uint32_t tMs, uint16_t bpm);
  void stop();
  bool running() const;
  bool tick(uint32_t tMs);   // true once per beat; beat n at t0 + n*60000/bpm, no drift
};
```

### 3.10 Sample-rate meter (`cpr_samplerate.h`)

Counts samples in 1-second buckets, reports the last complete bucket as `sps`.
`lowSps` only after two full buckets.

### 3.11 Stats and score (`cpr_stats.h`)

```cpp
class Stats {
 public:
  void reset();
  void addCompression(const CompressionEvent& c);
  void addPause(uint32_t pauseMs, const Thresholds& th);
  void touch(uint32_t tMs);              // updates lastMs
  const SessionStats& data() const;
};
uint8_t pct(uint16_t part, uint16_t whole);                 // rounded, 0 if whole==0
uint8_t ccfPercent(const SessionStats& s);                  // 100 * (1 - handsOff / (lastMs-firstMs)), clamped
uint8_t score(const SessionStats& s);                       // see below
```

Score (0–100):
`round(0.30*depth% + 0.25*rate% + 0.20*recoil% + 0.10*hand% + 0.15*min(100, ccf%*100/60))`
where rate% is over rated compressions. Every weight is ours; the dashboard
shows the breakdown so nobody mistakes it for an official grade.

### 3.12 Engine (`cpr_engine.h`)

```cpp
class Engine {
 public:
  void begin(const Config& cfg, const CalibrationData& cal);
  void setConfig(const Config& cfg);
  void setCalibration(const CalibrationData& cal);
  void setMode(Mode m);                 // resets training state
  void setTofPresent(bool present);     // decides TofMm vs MappedMm/ForceKg
  void startSession(uint32_t tMs);      // clears stats; session becomes active on first press
  void endSession(uint32_t tMs);        // queues SessionEnd
  void process(const Sample& s);        // call once per sample
  bool pollEvent(Event& out);           // drains a 4-entry ring buffer; oldest dropped if full
  const LiveState& live() const;
  const SessionStats& stats() const;
  const Config& config() const;
  Source source() const;
  // calibrate mode
  void calStart();                      // starts recording (kg, mm) pairs on every sample with both present
  void calCancel();
  bool calActive() const;
  uint16_t calCount() const;
  bool calFinish(CalibrationData& out); // fits; returns false if < 2 points
};
```

Source selection: ToF present and the sample has `mm >= 0` → TofMm. Else a
valid map (`mapCount >= 2`) and kg present → MappedMm (mm from the map). Else
ForceKg. Do not switch source in the middle of a press: decide at Idle.

Pause logic: while a session is active and Idle/Releasing, if
`now - lastPressStart > handsOffMs` and no press is pending → PauseStart event,
`inPause = true`, reset the rate estimator. When the next press starts →
PauseEnd with `pauseMs = now - lastPressFinishOrRecoilTime`, feed stats and
training.

`startSession` is implicit: if no session is active the first press starts one.
A session ends on `endSession`, on Test2Min timeout, or after 30 s with no
press (then SessionEnd is queued). After SessionEnd, `stats()` keeps the final
numbers until the next session starts.

### 3.13 JSON writer (`cpr_json.h`)

Fixed buffer, no heap, no `printf("%f")`. Numbers: ints as decimal, floats with
exactly one decimal, `NAN` written as `null`.

```cpp
size_t jsonFrame(char* buf, size_t cap, const LiveState& s);
size_t jsonEvent(char* buf, size_t cap, const Event& e);
size_t jsonSummary(char* buf, size_t cap, const SessionStats& s, Mode m, Profile p);
size_t jsonConfig(char* buf, size_t cap, const Config& c, const CalibrationData& cal,
                  bool tofPresent, const char* board, const char* fw);
size_t jsonAck(char* buf, size_t cap, const char* cmd, bool ok, const char* msg);
```
Each returns the length written (no trailing newline) or 0 if `cap` is too
small. Max length of any message: 200 bytes. Exact field lists are in
`docs/PROTOCOL.md` (§5 below is the source; P2 writes PROTOCOL.md from it).

### 3.14 Command parser (`cpr_command.h`)

One command per line, ASCII, case-insensitive verb, max 48 chars.

| Line | Meaning |
|---|---|
| `tare` | tare the load cell, re-read ToF rest distance |
| `profile adult` / `profile child` | switch profile (loads defaults, keeps calibration) |
| `mode free` / `mode cycle` / `mode test` | training mode |
| `start` / `stop` | start or end a session |
| `metro on` / `metro off` | buzzer metronome at 110/min |
| `cal start` / `cal finish` / `cal cancel` | force→depth map recording (needs ToF) |
| `cal weight <kg>` | scale factor from a known weight (e.g. `cal weight 2.0`) |
| `cal bands <goodKg> <deepKg>` | set kg bands manually (no ToF) |
| `cal peak` | reply with the last press peak in kg (wizard uses it) |
| `cal save` / `cal reset` | write to NVS/EEPROM, or restore defaults |
| `config` | reply with a `cfg` message |
| `wifi pass <8-63 chars>` | ESP32 only, stored in NVS, applied after reboot |

```cpp
enum class Cmd : uint8_t { None, Tare, Profile, Mode, Start, Stop, MetroOn, MetroOff,
  CalStart, CalFinish, CalCancel, CalWeight, CalBands, CalPeak, CalSave, CalReset,
  Config, WifiPass, Unknown };
struct Command { Cmd cmd; uint8_t arg; float f1, f2; char text[64]; };
bool parseCommand(const char* line, Command& out);  // false for blank/garbage → Unknown
```
`arg` holds Profile or Mode. `text` holds the Wi-Fi password.

Legacy single-key commands from the old sketch (`t`, `+`, `-`, `w`) are **not**
kept; the docs list the new commands.

---

## 4. Firmware behaviour common to both boards

- Boot: init pins, HX711 at `scaleFactor` from storage, wait 800 ms, tare.
  VL53L0X (if `USE_VL53L0X`): high-speed mode (timing budget 20 ms, continuous
  back-to-back), read 10 samples for the rest distance. If not found, log once
  and carry on with load cell only.
- Read samples: every time HX711 is ready (`is_ready()`), read one raw value
  (`get_units(1)`, not 3: averaging 3 at 10 SPS makes 3 SPS). Read the newest
  ToF value without blocking.
- LEDs (active high): GOOD green when liveDepth Good; SHALLOW yellow when
  Shallow; DEEP red when Deep or overload; POS on while handOk. All off when idle
  except POS.
- Buzzer (active, digitalWrite): with metronome on, 25 ms tick per beat. Without
  it: on each compression event, 40 ms if `allOk`, 150 ms otherwise (matches the
  old sketch). Overload: 200 ms. Never block; use an end-time.
- Output: JSON frame at 20 Hz, every event as it comes out, a `cfg` message at
  boot and on `config`, `sum` on SessionEnd, `ack` for each command. One message
  per line (`\n`). Free-text log lines start with `#` so the dashboard can skip
  them.
- `cal weight <kg>`: average 20 raw readings (blocking up to 3 s is OK here, it's
  a setup step) and set the scale.

## 5. Protocol summary (PROTOCOL.md will expand this)

All messages are one-line JSON objects with a `ty` field.

```
frame  {"ty":"f","t":12345,"v":48.2,"kg":7.1,"mm":48,"src":2,"ph":1,"ld":2,"bpm":110,"rl":2,
        "hand":1,"ov":0,"ps":0,"n":17,"cc":17,"cy":0,"sps":80,"lsps":0,"act":1,"el":9}
comp   {"ty":"c","t":12400,"i":17,"pk":52.3,"src":2,"dl":2,"rl":2,"bpm":110,"rc":1,"hd":1,"ok":1}
pause  {"ty":"ps","t":..}            pause start
pend   {"ty":"pe","t":..,"ms":4200,"long":0}
cycle  {"ty":"cy","t":..,"cy":3,"ms":5100,"long":0}
over   {"ty":"ov","t":..}
end    {"ty":"end","t":..}
sum    {"ty":"sum","mode":1,"prof":1,"n":120,"dg":100,"dsh":12,"dd":8,"rr":118,"rg":101,"rs":9,
        "rf":8,"rc":110,"hd":115,"ok":90,"dur":72,"off":9100,"lp":5200,"plong":0,"pk":51.2,
        "bpm":112,"cy":4,"ccf":87,"score":84}
cfg    {"ty":"cfg","board":"esp32","fw":"1.0.0","prof":1,"tof":1,"src":2,"unit":"mm",
        "gmin":50,"dmin":60,"gkg":6.0,"dkg":12.0,"ovkg":18.0,"rmin":100,"rmax":120,
        "map":5,"metro":110}
ack    {"ty":"ack","cmd":"tare","ok":1,"msg":"ok"}
```
`src`/`ph`/`ld`/`rl`/`dl`/`mode`/`prof` are the enum integer values. `unit` is
`"mm"` for TofMm/MappedMm and `"kg"` for ForceKg. `dur` is seconds. Client →
device messages are the plain command lines from §3.14 (WebSocket text frame or
Serial line).

## 6. ESP32 firmware (`src/esp32/`)

Pins (from `cpr_esp32.ino`): HX711 DT 16, SCK 17; buzzer 25; LED POS 26,
GOOD 27, SHALLOW 18, DEEP 19; foil switch 33 (INPUT_PULLUP, closed = LOW);
I²C SDA 21, SCL 22. HX711 VCC must be 3.3 V on this board.

Files: `main.cpp`, `pins.h`, `sensors.h/.cpp`, `feedback.h/.cpp` (LEDs, buzzer),
`storage.h/.cpp` (Preferences namespace `"cpr"`, key `"cal"` blob, key
`"wifipass"`), `net.h/.cpp` (AP, DNS, HTTP, WebSocket), `web_assets.h`
(generated, not committed: add to .gitignore).

Tasks:
- `sampleTask` pinned to core 1, priority 5: owns HX711, ToF, engine, LEDs,
  buzzer. Builds JSON lines and pushes them into a FreeRTOS queue of 24 × 208-byte
  slots. If the queue is full, drop frames but never events.
- Arduino `loop()` (core 1, lower priority) or a `netTask` on core 0: drains the
  queue to `ws.textAll()` and to `Serial`. Also limits WebSocket clients to 4 and
  calls `ws.cleanupClients()` every second.
- Commands from WebSocket and Serial go through a FreeRTOS queue of 8 × 64-byte
  lines into `sampleTask`, which runs them (never touch the engine from the
  network callbacks).

Networking:
- Soft AP, SSID `CPR-Trainer-XXXX` where XXXX is the last 2 MAC bytes in hex,
  password default `cprtrain123` (overridable via `wifi pass`), channel 6, IP
  192.168.4.1.
- `DNSServer` answering every name with 192.168.4.1 (captive portal), on by
  default, `-DCPR_CAPTIVE_DNS=0` to turn off. Handle `/generate_204`,
  `/hotspot-detect.html`, `/connecttest.txt` by redirecting to `/`.
- Library: **ESP32Async/ESPAsyncWebServer 3.12.1 + ESP32Async/AsyncTCP 3.6.1**
  (maintained org, releases Sep and Nov 2026, supports Arduino core 3.x which
  platform espressif32 8.0.0 ships as 3.3.12). WebSocket at `/ws`.
- HTTP: serve each file in `web/` (index.html, app.css, app.js, fonts/*.woff2,
  demo/*.jsonl listed in web/demo/manifest.json) from the gzip PROGMEM arrays
  with `Content-Encoding: gzip`, the right content type and
  `Cache-Control: max-age=86400` (index.html: `no-cache`). Unknown paths → `/`.
- On WebSocket connect, send `cfg` to that client.

`tools/embed_web.py` (PlatformIO `pre:` script, Python 3, stdlib only): walks
`web/`, gzips each file (level 9, mtime 0 for reproducible output), writes
`src/esp32/web_assets.h` with `const uint8_t ... PROGMEM` arrays and a table
`{path, mime, data, len}`. Skips `.gitkeep`. Prints the total gzipped size.

Sample-rate warning: `lowSps` from the engine goes out in frames; also log
`# HX711 is running at 10 SPS, see docs: RATE pin` once on serial.

## 7. Nano firmware (`src/nano/`)

Pins (from `cpr_nano.ino`): HX711 DT 3, SCK 2; buzzer 8; LED POS 4, GOOD 5,
SHALLOW 6, DEEP 7; foil 9 (INPUT_PULLUP); VL53L0X on A4/A5. Serial at
**115200** (the old sketch used 9600, which is too slow for 20 Hz JSON; the docs
mention the change). Same engine, same JSON lines, same commands over Serial.
EEPROM at address 0, `calibSerialize` layout. `-DCPR_CAL_PAIRS=16`.
`wifi pass` replies `ack ok:0 msg:"not on nano"`. No `String`, all constant
strings in `F()`/PROGMEM. Static RAM use must stay under 1500 of 2048 bytes
(leave room for the stack); report the number. Keep one 208-byte JSON line buffer, not several.

## 8. Sensors notes

- HX711 boards often tie RATE to GND → 10 SPS. At 110/min a press lasts about
  270 ms, so 10 SPS gives 2–3 samples per press: peaks get missed. Lifting the
  RATE pin from GND and tying it to DVDD gives 80 SPS. The firmware measures
  the real rate and warns; docs explain the mod with a photo-free description.
- On ESP32 the HX711 board must be fed 3.3 V so DT never goes above 3.3 V.
- Pololu VL53L0X library (pololu/VL53L0X 1.3.1) instead of Adafruit_VL53L0X
  used by the old sketches: it is far smaller on the Nano (the ST API port in
  the Adafruit library costs several KB flash and a lot of RAM), it supports
  `setMeasurementTimingBudget(20000)` and continuous mode, and it builds for
  both AVR and ESP32. HX711: bogde/HX711 0.7.5, same as the old sketches.

## 9. Simulator (`lib/cpr_sim`, `tools/sim`)

`lib/cpr_sim` (native only, never linked into firmware):

```cpp
namespace cprsim {
enum class Scenario : uint8_t { Good, Shallow, Deep, Fast, Slow, NoRecoil, WrongHand, Noisy, Cycle30x2 };
struct SimOptions { Scenario sc; uint16_t sps; bool tof; uint32_t seed; uint16_t seconds; cpr::Profile profile; };
// Deterministic generator: next() returns false when the scenario ends.
class Generator {
 public:
  void begin(const SimOptions& o);
  bool next(cpr::Sample& s);
};
const char* scenarioName(Scenario s);   // "good","shallow","deep","fast","slow","norecoil","wronghand","noisy","cycle"
bool scenarioFromName(const char* n, Scenario& out);
}
```
Waveform: each press is a raised-cosine down-stroke (~45% of the period) and
up-stroke, peak depth per scenario (good 54 mm, shallow 35 mm, deep 68 mm),
rate (good 110, fast 135, slow 85), no-recoil keeps 12 mm between presses,
wrong-hand opens the switch on 70% of presses, noisy adds ±3 mm gaussian noise
plus occasional single-sample spikes, cycle does 30 presses then a 5 s pause,
repeated. kg = mm through a fixed map `kg = 0.7*(0.2*mm + 0.0012*mm*mm)` so kg
and mm agree and the default kg bands of §2.2 give the same levels as the mm
bands for the three depth scenarios (35 mm ≈ 6.0 kg shallow, 54 mm ≈ 10 kg good,
68 mm ≈ 13.4 kg deep). The 0.7 is the generator's default `kgScale`
(`setKgScale()` changes it); `setRate(bpm)` overrides the scenario rate, which
the tests use for 90/110/130 cpm. (Changed after P1: see OPEN_ISSUES.md.) Use a small xorshift PRNG; no `<random>` so results are the
same on every compiler.

`tools/sim/main.cpp`: `cpr_sim --scenario good --seconds 40 --sps 80 [--no-tof]
[--profile child] [--mode free|cycle|test] --out recordings/good.jsonl`. Runs
samples through `cpr::Engine` exactly as firmware does (process every sample,
emit a frame every 50 ms of sample time, emit every event, emit `cfg` first and
`sum` at the end). Each line is the exact JSON the firmware would send, plus the
recording adds nothing else. `tools/sim/build.sh` compiles with `g++ -std=c++17
-O2` into `.simbin/cpr_sim`.

`tools/sim/make_recordings.sh` produces `recordings/<scenario>.jsonl` for all
nine scenarios (40 s each, cycle 75 s, test mode 120 s as `recordings/test2min.jsonl`),
then copies `good`, `shallow`, `cycle` and `test2min` into `web/demo/` and writes
`web/demo/manifest.json` (`[{"id":"good","title":"กดถูกต้อง","file":"good.jsonl"},...]`).
All nine are listed for the dev server via `recordings/manifest.json`.

`tools/devserver.py` (Python 3 stdlib only, hand-written RFC 6455 handshake and
framing): `python3 tools/devserver.py --port 8080 --recording recordings/good.jsonl
[--loop]`. Serves `web/` at `/`, `recordings/*.jsonl` at `/recordings/`, and at
`/ws` replays the recording with the original timing (`t` field deltas). Text
frames from the client are answered with an `ack` and printed.

## 10. Unit tests (`test/test_core/`)

Unity, `pio test -e native`. At least these, each a separate `RUN_TEST`:

- filter: spike removal, NAN handling, first-sample init
- detector: one clean press → one result with correct peak; hysteresis does not
  double count a noisy plateau; leaning re-rise starts a new press with
  recoilOk false; recoil timeout path
- rate: 90 / 110 / 130 cpm generator runs give bpm within ±3 and the right
  RateLevel
- depth: shallow / good / deep scenarios classify ≥ 90% of presses correctly
  in mm (TofMm) and in kg (ForceKg via default kg bands, using a kg-only sample stream)
- mapped mode: a calibration map makes kg-only input classify like mm
- recoil: norecoil scenario gives recoilOk false on ≥ 90% of presses; good gives true on ≥ 90%
- hand: wronghand scenario hand% between 20 and 40
- noise: noisy scenario count within ±1 of the generated count
- 30:2: cycle scenario gives the right number of CycleDone events and pause lengths ≈ 5 s
- pause: hands-off time and CCF for a known pause pattern
- Test2Min SessionEnd at 120 s
- low SPS: 10 SPS stream sets lowSps
- metronome: 110 bpm gives 110 ticks in 60 s, no drift after 10 min
- calibration: serialize/deserialize round trip, CRC failure rejected, fit is
  monotonic, map interpolation
- command parser: every command in §3.14 plus garbage
- JSON: frame/event/summary match exact expected strings for fixed inputs;
  every message ≤ 200 bytes; NAN → null
- size: `sizeof(Engine) - sizeof(CalRecorder) <= 600`
- score: known stats give a known score

---

## 11. Dashboard UI spec

The user asked for a beautiful UI. Visual quality is a requirement, not a nice
to have. Plain HTML/CSS/JS in `web/index.html`, `web/app.css`, `web/app.js`. No
CDN, no framework, no build step, works offline from the ESP32 AP. All visible
text is Thai. Target total gzipped size of html+css+js under 40 KB, font under
45 KB.

### 11.1 Look

Modern medical-training instrument: calm dark navy surfaces, one bright accent,
big confident numbers. Think of a good fitness app or a patient monitor, not a
hobby project. Generous whitespace, clear hierarchy, nothing cramped.

### 11.2 Tokens (CSS custom properties on `:root` and `[data-theme="light"]`)

Dark (default):
```
--bg:        #0B1220   page
--surface:   #111A2E   cards
--surface-2: #17233D   inset areas, gauge track
--border:    #22304D
--text:      #E8EEF8
--text-2:    #9AA8C3   secondary
--text-3:    #64748F   captions
--accent:    #4F8CFF   brand / primary buttons / links
--ok:        #22C55E   ok-bg: rgba(34,197,94,.14)
--warn:      #F59E0B   warn-bg: rgba(245,158,11,.14)   (too shallow / too slow)
--bad:       #EF4444   bad-bg: rgba(239,68,68,.14)     (too deep / too fast / overload)
--idle:      #64748F
--shadow:    0 1px 0 rgba(255,255,255,.04) inset, 0 8px 24px rgba(0,0,0,.35)
```
Light:
```
--bg #F4F6FB  --surface #FFFFFF  --surface-2 #EEF2F8  --border #DCE3EE
--text #0F172A --text-2 #475569 --text-3 #94A3B8 --accent #2563EB
--ok #16A34A --warn #D97706 --bad #DC2626 --idle #94A3B8
--shadow 0 1px 2px rgba(15,23,42,.06), 0 8px 24px rgba(15,23,42,.08)
```
State color always comes with a Thai label and an icon shape; never color alone.

### 11.3 Type

Font: Anuphan (OFL 1.1, Cadson Demak, from google/fonts `ofl/anuphan`),
subset to Thai + Basic Latin + digits + punctuation (U+0020–007E, U+0E00–0E7F,
U+2013, U+2014, U+2019, U+201C, U+201D, U+2022, U+00B0, U+00D7), weights kept
variable 400–700, woff2. Fallback stack:
`"Anuphan", "Noto Sans Thai", "Leelawadee UI", "Thonburi", system-ui, sans-serif`.
`font-display: swap`. Numbers use `font-variant-numeric: tabular-nums`.

Scale (rem, root 16px):
```
--fs-hero    clamp(4.5rem, 12vw, 8rem)  700  depth value in the gauge
--fs-display 2.5rem   700  big stat numbers
--fs-h1      1.5rem   700
--fs-h2      1.125rem 600
--fs-body    1rem     400  line-height 1.6 (Thai needs room)
--fs-small   .875rem  500
--fs-caption .75rem   500  letter-spacing .02em
```

### 11.4 Spacing, radius, size

Spacing scale 4/8/12/16/24/32/48 px (`--sp-1`…`--sp-7`). Radius: cards 20px,
buttons 14px, chips 999px. Touch targets at least 48×48 px. Card padding 24px
desktop, 16px phone. Grid gap 16px (24px ≥ 1024 px).

### 11.5 Layout

1280 × 800:
```
┌──────────────────────────────────────────────────────────────────────────┐
│ [logo] หุ่นฝึก CPR      (ฝึกอิสระ|รอบ 30:2|ทดสอบ 2 นาที)   ● เชื่อมต่อแล้ว [⚙] │  top bar 64px
├───────────────────────────────┬──────────────────────────────────────────┤
│  HERO DEPTH GAUGE  (card)     │  RATE (card)          │ RECOIL  (card)    │
│  vertical bar, target band    │  112 ครั้ง/นาที        │ ✓ คืนตัวสุด        │
│  big "5.2 ซม."                │  arc meter 80–140     │                   │
│  label chip "ลึกพอดี"          ├───────────────────────┼───────────────────┤
│                               │  HAND POSITION        │ COUNTER           │
│                               │  ✓ วางมือถูกตำแหน่ง     │ 17  /30  รอบที่ 2   │
│                               ├───────────────────────┴───────────────────┤
│                               │  ROLLING DEPTH GRAPH (canvas, last 10 s)  │
│                               │  target band shaded, dots per compression │
├───────────────────────────────┴──────────────────────────────────────────┤
│ [▶ เริ่ม] [■ จบรอบ]  [♪ จังหวะ 110]    ผู้ใหญ่|เด็ก     source: ToF · 80 SPS │ bottom bar
└──────────────────────────────────────────────────────────────────────────┘
```
Hero card spans the full height of the left column (about 40% width).

390 × 844 (single column, sticky bottom action bar):
```
┌──────────────────────────┐
│ หุ่นฝึก CPR      ● [⚙]   │
│ (ฝึกอิสระ|30:2|2 นาที)    │ segmented control, scrolls horizontally if needed
├──────────────────────────┤
│  HERO GAUGE (horizontal  │ big number + chip, bar below
│  or vertical bar 220px)  │
├────────────┬─────────────┤
│ RATE       │ RECOIL      │ 2-up small cards
├────────────┼─────────────┤
│ HAND       │ COUNT       │
├────────────┴─────────────┤
│ GRAPH (160px tall)       │
├──────────────────────────┤
│ [▶ เริ่ม]  [♪]  [■]        │ sticky bottom bar, safe-area padding
└──────────────────────────┘
```

### 11.6 Components

- Top bar: logo mark (inline SVG heart + pulse line), title, mode segmented
  control, connection pill (dot + text: "เดโม", "เชื่อมต่อแล้ว", "กำลังเชื่อมต่อ…",
  "ไม่ได้เชื่อมต่อ"), settings button.
- Connect sheet (first screen when not connected): three big choice cards
  "Wi-Fi (ESP32)", "สาย USB (Web Serial)", "โหมดเดโม" with one-line hints. Web
  Serial card disabled with a reason when `navigator.serial` is missing.
- Hero depth gauge: vertical track with the target band (goodMin..deepMin)
  drawn as a soft green zone with tick labels in cm, a live fill bar that
  animates with the depth, a marker for the last peak, the big value
  ("5.2" + "ซม."; in kg mode "7.1" + "กก."), and a status chip: "ตื้นไป" warn,
  "ลึกพอดี" ok, "ลึกเกินไป" bad, "พร้อม" idle. Chip and bar color follow the
  state. Readable from 2 m: hero number at least 96 px tall at 1280.
- Rate card: half-arc meter 60–160 with the 100–120 band highlighted, needle
  eased, value "112" + "ครั้ง/นาที", chip "ช้าไป"/"พอดี"/"เร็วไป".
- Recoil card: icon + "คืนตัวสุด" ok / "ยังค้างน้ำหนัก" warn, plus a row of the
  last 10 compressions as small dots.
- Hand card: icon + "วางมือถูกตำแหน่ง" ok / "วางมือผิดตำแหน่ง" bad.
- Counter card: total count; in 30:2 mode a ring progress "17/30", cycle number,
  and "ให้ลมหายใจ 2 ครั้ง" banner when awaiting breaths; in test mode a countdown
  "1:23".
- Graph: canvas, last 10 s of depth (y down = deeper, like the chest), target
  band shaded, each compression's peak dot colored by its depth level.
  DevicePixelRatio aware, redraw in rAF only when data changed.
- Pause banner: when `ps` is set, an amber banner "หยุดกดแล้ว 4 วินาที" that turns
  red after 10 s "หยุดนานเกิน 10 วินาที".
- Warnings strip: low SPS ("เซนเซอร์อ่านได้แค่ 10 ครั้ง/วินาที ดูวิธีแก้ในคู่มือ"),
  overload ("กดแรงเกิน ระวังเซนเซอร์เสีย").
- Summary sheet (after `sum` or when the user stops): a shareable card with the
  big score ring, grade word ("ยอดเยี่ยม" ≥ 85, "ดี" ≥ 70, "พอใช้" ≥ 50, else
  "ฝึกอีกนิด"), date and mode, four metric tiles (ความลึกถูก %, จังหวะถูก %,
  คืนตัวสุด %, มือถูกตำแหน่ง %) each with a thin bar, then CCF, mean rate, mean
  depth, count, duration, longest pause, cycles. A small footnote explains the
  score weights and "สื่อการเรียนรู้ ไม่ใช่เครื่องมือแพทย์". Buttons: "ดาวน์โหลด CSV",
  "ฝึกอีกครั้ง". The card should look good enough that students screenshot it.
- Settings sheet: theme (มืด/สว่าง), profile (ผู้ใหญ่/เด็ก), metronome, sound on the
  device, connection, open calibration wizard, about.
- Calibration wizard (sheet, stepper with 4 steps):
  1. "ยกมือออกจากหุ่น" → button "ตั้งศูนย์" (sends `tare`).
  2. "วางของที่รู้น้ำหนัก" → input kg + "บันทึก" (`cal weight`).
  3. With ToF: "กดช้าๆ หลายๆ ครั้ง ให้ลึกสุดประมาณ 6 ซม." live counter of pairs,
     button "เสร็จ" (`cal start` / `cal finish`). Without ToF: "กดให้ลึก 5 ซม.
     (วัดด้วยไม้บรรทัด) แล้วกดปุ่ม" → `cal peak` twice to set good and deep kg,
     then `cal bands`.
  4. "บันทึก" (`cal save`) with a success state.
  Works in Demo mode too: the demo transport fakes acks so the wizard can be
  shown.

CSV (client-side, Blob download): one row per compression: `index,time_s,peak,
unit,depth_level,bpm,rate_level,recoil_ok,hand_ok,all_ok`, then a blank line and
the summary rows. UTF-8 with BOM so Excel shows Thai.

### 11.7 Motion

- One rAF loop; frames arriving at 20 Hz are interpolated so the gauge moves at
  60 fps (critically damped spring or lerp with factor ~0.25 per frame).
- Transitions 150–250 ms, `cubic-bezier(.2,.8,.2,1)`. Chips cross-fade color.
- A compression event pulses the counter (scale 1 → 1.06 → 1, 180 ms).
- No bouncing, no confetti, no gradients that move.
- `prefers-reduced-motion: reduce` turns off pulses and uses direct values.

### 11.8 Transports (`app.js`)

A small `Transport` interface: `connect()`, `disconnect()`, `send(line)`,
`onMessage(obj)`, `onStatus(str)`.
- WebSocket: `ws://<location.host>/ws`; reconnect with backoff 1 s → 5 s.
- Web Serial: 115200 baud, `TextDecoderStream`, split by `\n`, ignore lines
  not starting with `{` (log lines start with `#`).
- Demo: fetch `demo/manifest.json`, pick a recording (selector in the connect
  sheet), replay lines with original timing, loop. Commands get fake acks; `cal`
  commands get plausible acks. When the page is served by the dev server,
  `?ws=1` uses the dev server's `/ws` instead. URL flags for screenshots:
  `?demo=good|shallow|cycle|test2min`, `&speed=4` (replay speed),
  `&seek=<seconds>` (jump ahead instantly), `&theme=light`, `&sheet=summary|calib|connect`.

The dashboard never re-implements the algorithm. Everything it shows comes from
`f`, `c`, `sum` and the other device messages.

## 12. Thai documentation (P6)

`README.md` (Thai), `docs/wiring.md`, `docs/assembly.md`, `docs/calibration.md`,
`docs/dashboard.md`, `docs/troubleshooting.md`, `docs/report.md`,
`docs/PROTOCOL.md` (may be English: it's for developers). Human tone, short
sentences, no marketing words.

## 13. Phases

D0 design (this file); P1 `lib/cpr_core` + `lib/cpr_sim` generator + unit
tests; P2 ESP32 firmware + `tools/embed_web.py` + PROTOCOL.md; P3 Nano firmware;
P4 dashboard + font subset; P5 simulator CLI, recordings, Demo mode wiring, dev
server, screenshot script; P6 Thai docs; P7 review and fixes.

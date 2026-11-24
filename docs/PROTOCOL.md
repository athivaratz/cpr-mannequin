# CPR trainer protocol

This document expands the protocol summary in `DESIGN.md` §5. It is for people
writing dashboards, tools, or another firmware. The firmware is the source of
truth for the values it sends; the dashboard never re-implements the detector.

The trainer is a teaching aid, not a medical device.

## 1. Transports

Both transports carry the same newline-separated text. One message or command
per line. Client commands may end in `\n`, `\r\n`, or `\r`; the firmware also
accepts a WebSocket text frame that contains one command without a newline.

### WebSocket

- URL: `ws://192.168.4.1/ws` (the device's soft-AP address).
- Client to device: plain command lines from §4, sent as WebSocket text frames.
- Device to client: one JSON object per text frame. Free-text log lines start
  with `#`.
- On a successful connection the device sends the current `cfg` to that client.
- At most four WebSocket clients are kept. Older or stale connections are
  closed during the once-per-second cleanup.

### Serial (USB)

- 115200 baud, 8 data bits, no parity, 1 stop bit.
- Device to host: JSON objects and `#` log lines, one per line.
- Host to device: command lines from §4.

### Network

- Soft AP SSID: `CPR-Trainer-XXXX`, where `XXXX` is the last two MAC bytes in
  uppercase hex.
- Default password: `cprtrain123`; change it with `wifi pass <password>` and
  reboot.
- Channel 6, device IP `192.168.4.1`.
- Captive DNS answers every name with `192.168.4.1` by default. Build with
  `-DCPR_CAPTIVE_DNS=0` to disable it.
- HTTP serves the dashboard from the firmware image. `/generate_204`,
  `/hotspot-detect.html`, and `/connecttest.txt` redirect to `/`.

## 2. Timing

- `f` frames are sent at 20 Hz (one every 50 ms of device time).
- `c`, `ps`, `pe`, `cy`, `ov`, and `end` are sent as soon as the engine emits
  them, normally on the same sample.
- `cfg` is sent at boot, on a WebSocket connection, after `config`, and after
  commands that change the profile or calibration.
- `sum` follows the `end` message for a session.
- Commands are queued to the sampling task and answered asynchronously. An
  `ack` normally arrives within one sample period. `cal weight` may take up to
  three seconds because it averages raw load-cell readings.
- All `t` values are monotonic milliseconds since boot and may wrap after about
  49 days. Treat differences, not absolute values, as meaningful.

## 3. Shared values

### Enumerations

| Field | Value | Meaning |
|---|---|---|
| `src` | 0 | `ForceKg`: load cell in kg |
| | 1 | `MappedMm`: load cell converted through the calibration map to mm |
| | 2 | `TofMm`: VL53L0X depth in mm |
| `ph` | 0 | `Idle` |
| | 1 | `Pressing` |
| | 2 | `Peak` |
| | 3 | `Releasing` |
| `ld`, `dl` | 0 | `None` |
| | 1 | `Shallow` |
| | 2 | `Good` |
| | 3 | `Deep` |
| `rl` | 0 | `None` (no rate yet) |
| | 1 | `Slow` |
| | 2 | `Good` |
| | 3 | `Fast` |
| `mode` | 0 | free training |
| | 1 | 30:2 cycles |
| | 2 | 2-minute test |
| `prof` | 0 | adult |
| | 1 | child |

### Numbers and units

- `kg` is load-cell weight in kilograms, already tared and scaled. It is `null`
  when there is no load-cell reading.
- `mm` is depth below the resting plate, at least 0. It is `-1` when no ToF or
  mapped depth is available.
- `v` and compression `pk` are in the current active unit: `mm` for `TofMm` or
  `MappedMm`, `kg` for `ForceKg`. `cfg.unit` says which.
- One-decimal floats are written as JSON numbers (`48.2`); a not-a-number value
  is written as `null`.
- `bpm` is compressions per minute, rounded and clamped to 0–250. `0` means the
  rate is not known yet.
- `hand`/`hd`/`rc`/`ok`/`long`/`ov`/`ps`/`lsps`/`act`/`tof` are JSON numbers
  `1` or `0`, not booleans.
- `el` and `dur` are seconds. `off`, `lp`, `ms`, and all `t` fields are
  milliseconds.

## 4. Client to device commands

Commands are ASCII, one per line, maximum 48 characters. Verbs and arguments are
case-insensitive. Blank lines are ignored.

| Command | Effect |
|---|---|
| `tare` | Tare the load cell and re-read the ToF rest distance. |
| `profile adult` / `profile child` | Load that profile's defaults, keep the calibration. |
| `mode free` / `mode cycle` / `mode test` | Select free, 30:2, or 2-minute mode. |
| `start` / `stop` | Start or end a session. A session really starts on the first press. |
| `metro on` / `metro off` | Buzzer metronome at 110 beats per minute. |
| `cal start` | Start recording force/depth pairs. Requires ToF. |
| `cal finish` | Fit a force-to-depth map from the recorded pairs. Requires ToF. |
| `cal cancel` | Discard the recorded pairs. |
| `cal weight <kg>` | Set the load-cell scale from a known weight, e.g. `cal weight 2.0`. |
| `cal bands <goodKg> <deepKg>` | Set the kg depth bands manually, for use without ToF. |
| `cal peak` | Return the last compression peak in kg. Used by the no-ToF wizard. |
| `cal save` | Write the calibration to NVS. |
| `cal reset` | Restore default calibration. |
| `config` | Return the current configuration. |
| `wifi pass <8-63 chars>` | Store a new AP password. Applied after reboot. ESP32 only. |

### Acknowledgements

Every command is answered with an `ack`:

```
{"ty":"ack","cmd":"tare","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"profile","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"mode","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"start","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"stop","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"metro","ok":1,"msg":"on"}
{"ty":"ack","cmd":"cal start","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal finish","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal cancel","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal weight","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal bands","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal save","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"cal reset","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"config","ok":1,"msg":"ok"}
{"ty":"ack","cmd":"wifi pass","ok":1,"msg":"saved, reboot"}
```

`cmd` is the canonical command name without its argument. `ok` is `1` on
success and `0` on failure. Common failures are `"no tof"`, `"no hx711"`,
`"reading failed"`, `"bad weight"`, `"bad bands"`, `"need 2 points"`,
`"sensor not ready"`, `"write failed"`, and `"unknown command"`.

Malformed or unknown input is answered with:

```
{"ty":"ack","cmd":"unknown","ok":0,"msg":"unknown command"}
```

`cal peak` puts the peak value in `msg`, in kg with one decimal, for example:

```
{"ty":"ack","cmd":"cal peak","ok":1,"msg":"7.1"}
{"ty":"ack","cmd":"cal peak","ok":0,"msg":"no peak"}
```

Commands that change configuration or calibration are followed by a fresh
`cfg`: `tare`, `profile`, `cal weight`, `cal bands`, `cal finish`, `cal save`,
`cal reset`, and `config`.

`stop` queues the session end. The `ack` is followed by an `end` and a `sum` as
the sampling task drains its events.

## 5. Device to client messages

### `f` — live frame

Sent at 20 Hz.

```
{"ty":"f","t":12345,"v":48.2,"kg":7.1,"mm":48,"src":2,"ph":1,"ld":2,
 "bpm":110,"rl":2,"hand":1,"ov":0,"ps":0,"n":17,"cc":17,"cy":0,
 "sps":80,"lsps":0,"act":1,"el":9}
```

| Field | Meaning |
|---|---|
| `t` | sample time, ms |
| `v` | filtered signal in the active unit |
| `kg` | filtered load-cell kg, or `null` |
| `mm` | latest depth in mm, or `-1` |
| `src` | active source enum |
| `ph` | detector phase enum |
| `ld` | live depth level while pressing, `None` when idle |
| `bpm` | current rate estimate, 0 if unknown |
| `rl` | current rate level |
| `hand` | foil switch closed |
| `ov` | overload active |
| `ps` | in a pause (no press for longer than the hands-off time) |
| `n` | compressions in this session |
| `cc` | compressions in the current 30:2 cycle, 0–30 |
| `cy` | completed 30:2 cycles |
| `sps` | measured sample rate, samples per second |
| `lsps` | low sample rate flag, after two full 1-second buckets |
| `act` | session active |
| `el` | seconds since the session's first compression |

### `c` — compression

```
{"ty":"c","t":12400,"i":17,"pk":52.3,"src":2,"dl":2,"rl":2,"bpm":110,
 "rc":1,"hd":1,"ok":1}
```

| Field | Meaning |
|---|---|
| `t` | event time, ms |
| `i` | 1-based compression index in the session |
| `pk` | peak value in the active unit |
| `src` | source used for this compression |
| `dl` | depth level of the peak |
| `rl` | rate level at the peak |
| `bpm` | rate at the peak, 0 if unknown |
| `rc` | recoil was full |
| `hd` | hand position was good for at least half the press |
| `ok` | depth Good, rate Good or None, recoil good, and hand good |

### `ps` — pause start

```
{"ty":"ps","t":13000}
```

A session is running and no press has started for longer than the hands-off
time.

### `pe` — pause end

```
{"ty":"pe","t":17200,"ms":4200,"long":0}
```

| Field | Meaning |
|---|---|
| `t` | event time, ms |
| `ms` | pause length, ms |
| `long` | pause is longer than the maximum allowed pause |

### `cy` — 30:2 cycle done

```
{"ty":"cy","t":18200,"cy":3,"ms":5100,"long":0}
```

| Field | Meaning |
|---|---|
| `t` | event time, ms |
| `cy` | cycles completed so far |
| `ms` | breath pause length, ms |
| `long` | the pause is too long |

### `ov` — overload

```
{"ty":"ov","t":15400}
```

One message per rising edge of the overload condition.

### `end` — session end

```
{"ty":"end","t":84000}
```

Sent on `stop`, on the 2-minute test timeout, or after 30 seconds with no press.
A `sum` follows.

### `sum` — session summary

```
{"ty":"sum","mode":1,"prof":1,"n":120,"dg":100,"dsh":12,"dd":8,
 "rr":118,"rg":101,"rs":9,"rf":8,"rc":110,"hd":115,"ok":90,
 "dur":72,"off":9100,"lp":5200,"plong":0,"pk":51.2,"bpm":112,
 "cy":4,"ccf":87,"score":84}
```

| Field | Meaning |
|---|---|
| `mode` | training mode enum |
| `prof` | profile enum |
| `n` | compressions |
| `dg`, `dsh`, `dd` | depth Good, Shallow, Deep counts |
| `rr` | compressions with a known rate |
| `rg`, `rs`, `rf` | rate Good, Slow, Fast counts |
| `rc` | recoil-good count |
| `hd` | hand-good count |
| `ok` | all-good count |
| `dur` | session duration, seconds |
| `off` | total hands-off time, ms |
| `lp` | longest pause, ms |
| `plong` | number of pauses longer than the allowed maximum |
| `pk` | mean compression peak, active unit |
| `bpm` | mean rate over rated compressions |
| `cy` | completed cycles |
| `ccf` | chest compression fraction, percent |
| `score` | teaching score, 0–100 |

### `cfg` — configuration

```
{"ty":"cfg","board":"esp32","fw":"1.0.0","prof":1,"tof":1,"src":2,
 "unit":"mm","gmin":50,"dmin":60,"gkg":6.0,"dkg":12.0,"ovkg":18.0,
 "rmin":100,"rmax":120,"map":5,"metro":110}
```

| Field | Meaning |
|---|---|
| `board` | `"esp32"` or `"nano"` |
| `fw` | firmware version string |
| `prof` | profile enum |
| `tof` | ToF sensor is present |
| `src` | source the device expects to use |
| `unit` | `"mm"` or `"kg"` |
| `gmin`, `dmin` | mm band start for Good and Deep |
| `gkg`, `dkg` | kg band start for Good and Deep |
| `ovkg` | overload kg |
| `rmin`, `rmax` | good rate range, per minute |
| `map` | number of force-to-depth map points |
| `metro` | metronome rate, beats per minute |

### `ack` — command answer

See §4.

## 6. Log lines

Lines that start with `#` are free text for humans. Dashboards should ignore
them.

- `# HX711 not found` — once at boot if the load cell is not ready within two
  seconds. Frames then carry `"kg":null`.
- `# VL53L0X not found` or `# VL53L0X no reading` — the optional ToF sensor is
  missing or unusable; the device continues with load cell only.
- `# HX711 is running at 10 SPS, see docs: RATE pin` — once when the measured
  sample rate is below the configured warning level.

// Native CPR simulator CLI. Feeds a generated sample stream through the same
// cpr::Engine and JSON writers the firmware uses, and writes a recording.

#include "cpr.h"
#include "cpr_sim.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const char* kFirmware = "1.3.0";
const char* kBoard = "sim";
const uint32_t kFrameMs = 50;

void usage(const char* prog) {
  fprintf(stderr,
          "usage: %s --scenario <name> --out <file> [options]\n"
          "  --scenario good|shallow|deep|fast|slow|norecoil|wronghand|noisy|cycle\n"
          "  --out <file>        recording path (one JSON message per line)\n"
          "  --seconds <n>       sample duration, default 40\n"
          "  --sps <n>           samples per second, default 80\n"
          "  --profile child|adult (default child)\n"
          "  --mode free|cycle|test\n"
          "  --no-tof            generate load-cell-only samples\n"
          "  --esp32             use the ESP32 user thresholds and fast filter\n"
          "  --seed <n>          generator seed, default 1\n",
          prog);
}

bool parseU32(const char* text, uint32_t& out) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  unsigned long value = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value > 0xFFFFFFFFUL) return false;
  out = (uint32_t)value;
  return true;
}

bool parseU16(const char* text, uint16_t& out) {
  uint32_t value = 0;
  if (!parseU32(text, value) || value > 0xFFFFU) return false;
  out = (uint16_t)value;
  return true;
}

bool match(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool writeLine(FILE* out, const char* buf, size_t len) {
  if (len == 0) return false;
  if (fwrite(buf, 1, len, out) != len) return false;
  return fputc('\n', out) != EOF;
}

bool writeEvent(FILE* out, const cpr::Event& ev) {
  char buf[cpr::kMaxJsonLine + 8];
  size_t len = cpr::jsonEvent(buf, sizeof(buf), ev);
  return writeLine(out, buf, len);
}

bool writeSummary(FILE* out, const cpr::Engine& engine, cpr::Mode mode,
                  cpr::Profile profile) {
  char buf[cpr::kMaxJsonLine + 8];
  size_t len = cpr::jsonSummary(buf, sizeof(buf), engine.stats(), mode, profile);
  return writeLine(out, buf, len);
}

bool writeFrame(FILE* out, const cpr::Engine& engine) {
  char buf[cpr::kMaxJsonLine + 8];
  size_t len = cpr::jsonFrame(buf, sizeof(buf), engine.live());
  return writeLine(out, buf, len);
}

} // namespace

int main(int argc, char** argv) {
  cprsim::Scenario scenario = cprsim::Scenario::Good;
  cpr::Profile profile = cpr::Profile::Child;
  cpr::Mode mode = cpr::Mode::Free;
  uint16_t sps = 80;
  uint32_t seconds = 40;
  uint32_t seed = 1;
  bool tof = true;
  bool useEsp32 = false;
  bool haveScenario = false;
  const char* outPath = nullptr;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (match(arg, "--help") || match(arg, "-h")) {
      usage(argv[0]);
      return 0;
    } else if (match(arg, "--no-tof")) {
      tof = false;
    } else if (match(arg, "--esp32")) {
      useEsp32 = true;
    } else if (match(arg, "--scenario") || match(arg, "--out") ||
               match(arg, "--seconds") || match(arg, "--sps") ||
               match(arg, "--profile") || match(arg, "--mode") ||
               match(arg, "--seed")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "error: %s needs a value\n", arg);
        usage(argv[0]);
        return 2;
      }
      const char* value = argv[++i];
      if (match(arg, "--scenario")) {
        if (!cprsim::scenarioFromName(value, scenario)) {
          fprintf(stderr, "error: unknown scenario '%s'\n", value);
          usage(argv[0]);
          return 2;
        }
        haveScenario = true;
      } else if (match(arg, "--out")) {
        outPath = value;
      } else if (match(arg, "--seconds")) {
        if (!parseU32(value, seconds) || seconds == 0) {
          fprintf(stderr, "error: bad --seconds '%s'\n", value);
          return 2;
        }
      } else if (match(arg, "--sps")) {
        if (!parseU16(value, sps) || sps == 0) {
          fprintf(stderr, "error: bad --sps '%s'\n", value);
          return 2;
        }
      } else if (match(arg, "--profile")) {
        if (match(value, "adult")) profile = cpr::Profile::Adult;
        else if (match(value, "child")) profile = cpr::Profile::Child;
        else {
          fprintf(stderr, "error: bad --profile '%s'\n", value);
          usage(argv[0]);
          return 2;
        }
      } else if (match(arg, "--mode")) {
        if (match(value, "free")) mode = cpr::Mode::Free;
        else if (match(value, "cycle")) mode = cpr::Mode::Cycle30x2;
        else if (match(value, "test")) mode = cpr::Mode::Test2Min;
        else {
          fprintf(stderr, "error: bad --mode '%s'\n", value);
          usage(argv[0]);
          return 2;
        }
      } else if (match(arg, "--seed")) {
        if (!parseU32(value, seed) || seed == 0) {
          fprintf(stderr, "error: bad --seed '%s'\n", value);
          return 2;
        }
      }
    } else {
      fprintf(stderr, "error: unknown argument '%s'\n", arg);
      usage(argv[0]);
      return 2;
    }
  }

  if (!haveScenario || outPath == nullptr) {
    fprintf(stderr, "error: --scenario and --out are required\n");
    usage(argv[0]);
    return 2;
  }

  FILE* out = fopen(outPath, "wb");
  if (out == nullptr) {
    fprintf(stderr, "error: cannot open '%s'\n", outPath);
    return 1;
  }

  cpr::Config cfg = cpr::defaultConfig(profile);
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);

  if (useEsp32) {
    cpr::UserTh th;
    cpr::userThDefaults(th);
    cpr::userThApply(th, cfg);
  }

  cpr::Engine engine;
  engine.begin(cfg, cal);
  engine.setMode(mode);
  engine.setTofPresent(tof);
  if (useEsp32 && sps <= 20) engine.setFastFilter(true);

  cprsim::SimOptions opt;
  opt.sc = scenario;
  opt.sps = sps;
  opt.tof = tof;
  opt.seed = seed;
  opt.seconds = (uint16_t)(seconds > 0xFFFFU ? 0xFFFFU : seconds);
  opt.profile = profile;

  cprsim::Generator gen;
  gen.begin(opt);

  char cfgBuf[cpr::kMaxJsonLine + 8];
  size_t cfgLen = cpr::jsonConfig(cfgBuf, sizeof(cfgBuf), cfg, cal, tof,
                                  kBoard, kFirmware, true, true);
  if (!writeLine(out, cfgBuf, cfgLen)) {
    fprintf(stderr, "error: write failed\n");
    fclose(out);
    return 1;
  }

  bool finished = false;
  bool writeError = false;
  uint32_t lastT = 0;
  uint32_t nextFrameMs = 0;
  cpr::Sample sample;

  while (gen.next(sample)) {
    lastT = sample.tMs;
    engine.process(sample);

    cpr::Event ev;
    while (engine.pollEvent(ev)) {
      if (!writeEvent(out, ev)) {
        writeError = true;
        break;
      }
      if (ev.type == cpr::EventType::SessionEnd) {
        if (!writeSummary(out, engine, mode, cfg.profile)) writeError = true;
        finished = true;
        break;
      }
    }
    if (writeError || finished) break;

    if (sample.tMs >= nextFrameMs) {
      if (!writeFrame(out, engine)) {
        writeError = true;
        break;
      }
      nextFrameMs = (sample.tMs / kFrameMs + 1) * kFrameMs;
    }
  }

  if (!writeError && !finished) {
    engine.endSession(lastT);
    cpr::Event ev;
    while (engine.pollEvent(ev)) {
      if (!writeEvent(out, ev)) {
        writeError = true;
        break;
      }
      if (ev.type == cpr::EventType::SessionEnd) {
        if (!writeSummary(out, engine, mode, cfg.profile)) writeError = true;
        finished = true;
        break;
      }
    }
  }

  if (fclose(out) != 0) writeError = true;
  if (writeError) {
    fprintf(stderr, "error: write failed for '%s'\n", outPath);
    return 1;
  }
  if (!finished) {
    fprintf(stderr, "error: session did not end\n");
    return 1;
  }
  return 0;
}

#include <unity.h>

#include "cpr.h"
#include "cpr_sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

struct RunOut {
  cpr::SessionStats stats;
  cpr::LiveState live;
  uint16_t compEvents;
  uint32_t firstStartMs;
  uint32_t lastStartMs;
  uint16_t cycleDone;
  uint32_t cyclePauseMs[8];
  uint8_t cyclePauseCount;
  uint16_t pauseStarts;
  uint16_t pauseEnds;
  uint16_t endEvents;
  uint32_t endTMs;
  uint16_t overEvents;
};

RunOut runScenario(cprsim::Scenario sc, uint16_t seconds, uint16_t sps, bool tof,
                   uint16_t rateOverride = 0, float kgScale = 1.0f,
                   cpr::Mode mode = cpr::Mode::Free,
                   const cpr::CalibrationData* map = nullptr) {
  cpr::Engine engine;
  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Adult);
  cpr::CalibrationData cal;
  if (map != nullptr) cal = *map;
  else cpr::calibDefaults(cal);

  engine.begin(cfg, cal);
  engine.setMode(mode);
  engine.setTofPresent(tof);

  cprsim::SimOptions opt;
  opt.sc = sc;
  opt.sps = sps;
  opt.tof = tof;
  opt.seed = 4242;
  opt.seconds = seconds;
  opt.profile = cpr::Profile::Adult;

  cprsim::Generator gen;
  gen.begin(opt);
  if (rateOverride != 0) gen.setRate(rateOverride);
  gen.setKgScale(kgScale);

  RunOut out;
  memset(&out, 0, sizeof(out));

  cpr::Sample s;
  while (gen.next(s)) {
    engine.process(s);
    cpr::Event ev;
    while (engine.pollEvent(ev)) {
      if (ev.type == cpr::EventType::Compression) {
        if (out.compEvents == 0) out.firstStartMs = ev.c.tStartMs;
        out.lastStartMs = ev.c.tStartMs;
        out.compEvents++;
      } else if (ev.type == cpr::EventType::CycleDone) {
        if (out.cyclePauseCount < 8) {
          out.cyclePauseMs[out.cyclePauseCount++] = ev.pauseMs;
        }
        out.cycleDone++;
      } else if (ev.type == cpr::EventType::PauseStart) {
        out.pauseStarts++;
      } else if (ev.type == cpr::EventType::PauseEnd) {
        out.pauseEnds++;
      } else if (ev.type == cpr::EventType::SessionEnd) {
        out.endEvents++;
        out.endTMs = ev.tMs;
      } else if (ev.type == cpr::EventType::Overload) {
        out.overEvents++;
      }
    }
  }

  out.stats = engine.stats();
  out.live = engine.live();
  return out;
}

float simKg(float mm) { return 0.2f * mm + 0.0012f * mm * mm; }

uint8_t percentOf(uint16_t part, uint16_t whole) { return cpr::pct(part, whole); }

int absDiffInt(int a, int b) { return a > b ? a - b : b - a; }

} // namespace

void setUp() {}
void tearDown() {}

void test_filter() {
  cpr::Filter f;
  f.setAlpha(0.5f);

  TEST_ASSERT_EQUAL_FLOAT(42.0f, f.push(42.0f));

  cpr::Filter spike;
  spike.setAlpha(0.5f);
  spike.push(10.0f);
  spike.push(10.0f);
  spike.push(10.0f);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, spike.push(100.0f));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, spike.value());

  float before = spike.value();
  TEST_ASSERT_EQUAL_FLOAT(before, spike.push(NAN));
  TEST_ASSERT_EQUAL_FLOAT(before, spike.value());
}

void test_filter_median_off() {
  cpr::Filter median;
  median.setAlpha(0.5f);
  median.push(0.0f);
  median.push(0.0f);
  median.push(0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, median.push(100.0f));

  cpr::Filter fast;
  fast.setAlpha(0.5f);
  fast.setMedian(false);
  fast.push(0.0f);
  fast.push(0.0f);
  fast.push(0.0f);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, fast.push(100.0f));

  fast.setMedian(true);
  fast.push(100.0f);
  fast.push(100.0f);
  fast.push(100.0f);
  TEST_ASSERT_TRUE(fast.value() > 50.0f);
  TEST_ASSERT_TRUE(fast.value() <= 100.0f);
}

void test_settings_defaults() {
  cpr::UserTh t;
  cpr::userThDefaults(t);
  TEST_ASSERT_TRUE(cpr::userThValid(t));
  TEST_ASSERT_EQUAL_FLOAT(6.0f, t.good);
  TEST_ASSERT_EQUAL_FLOAT(12.0f, t.deep);
  TEST_ASSERT_EQUAL_FLOAT(18.0f, t.over);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, t.contact);
  TEST_ASSERT_EQUAL_FLOAT(0.2f, t.recoil);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, t.filt);
}

void test_settings_ranges() {
  cpr::UserTh base;
  cpr::userThDefaults(base);

  cpr::UserTh t = base;
  t.recoil = -0.01f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.recoil = t.contact;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.contact = 0.09f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.contact = 10.1f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.good = t.contact;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.good = 0.49f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.deep = t.good;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.over = t.deep;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.over = 60.1f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.filt = 0.19f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.filt = 1.01f;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.good = NAN;
  TEST_ASSERT_FALSE(cpr::userThValid(t));

  t = base;
  t.recoil = 0.0f;
  t.contact = 0.1f;
  t.good = 0.5f;
  t.deep = 0.6f;
  t.over = 60.0f;
  t.filt = 0.2f;
  TEST_ASSERT_TRUE(cpr::userThValid(t));

  t.filt = 1.0f;
  TEST_ASSERT_TRUE(cpr::userThValid(t));
}

void test_settings_apply() {
  cpr::UserTh t;
  cpr::userThDefaults(t);
  t.good = 5.5f;
  t.deep = 11.0f;
  t.over = 17.0f;
  t.contact = 0.4f;
  t.recoil = 0.1f;
  t.filt = 0.6f;

  TEST_ASSERT_TRUE(cpr::userThValid(t));
  const float release = cpr::userThRelease(t);
  TEST_ASSERT_TRUE(release > t.recoil);
  TEST_ASSERT_TRUE(release < t.contact);

  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Adult);
  cpr::userThApply(t, cfg);
  TEST_ASSERT_EQUAL_FLOAT(t.good, cfg.th.goodMinKg);
  TEST_ASSERT_EQUAL_FLOAT(t.deep, cfg.th.deepMinKg);
  TEST_ASSERT_EQUAL_FLOAT(t.over, cfg.th.overloadKg);
  TEST_ASSERT_EQUAL_FLOAT(t.contact, cfg.th.contactKg);
  TEST_ASSERT_EQUAL_FLOAT(t.recoil, cfg.th.recoilKg);
  TEST_ASSERT_EQUAL_FLOAT(release, cfg.th.releaseKg);
  TEST_ASSERT_EQUAL_FLOAT(t.filt, cfg.filterAlpha);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, cfg.th.restartDeltaKg);

  t.contact = 0.2f;
  t.recoil = 0.05f;
  cpr::userThApply(t, cfg);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, cfg.th.restartDeltaKg);
}

void test_detector_clean_press() {
  cpr::Detector d;
  d.setThresholds(10.0f, 6.0f, 5.0f, 8.0f, 0.15f, 2000);

  const float v[] = {0, 0, 0, 12, 25, 45, 60, 45, 25, 10, 5, 2, 0};
  uint32_t t = 0;
  int results = 0;
  bool started = false;
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
    if (d.update(t, v[i], true)) {
      results++;
      TEST_ASSERT_EQUAL_FLOAT(60.0f, d.result().peak);
      TEST_ASSERT_TRUE(d.result().recoilOk);
      TEST_ASSERT_TRUE(d.result().handOk);
      TEST_ASSERT_EQUAL_UINT32(120, d.result().tPeakMs);
    }
    if (d.pressStartedThisSample()) started = true;
    t += 20;
  }
  TEST_ASSERT_EQUAL_INT(1, results);
  TEST_ASSERT_TRUE(started);
  TEST_ASSERT_EQUAL_UINT32(60, d.lastPressStartMs());
}

void test_detector_hysteresis_plateau() {
  cpr::Detector d;
  d.setThresholds(10.0f, 6.0f, 5.0f, 8.0f, 0.15f, 2000);

  const float v[] = {0, 12, 30, 60, 50, 55, 52, 48, 51, 50, 40, 20, 8, 5, 0, 0};
  uint32_t t = 0;
  int results = 0;
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
    if (d.update(t, v[i], true)) {
      results++;
      TEST_ASSERT_EQUAL_FLOAT(60.0f, d.result().peak);
    }
    t += 20;
  }
  TEST_ASSERT_EQUAL_INT(1, results);
}

void test_detector_leaning_restart() {
  cpr::Detector d;
  d.setThresholds(10.0f, 6.0f, 5.0f, 8.0f, 0.15f, 2000);

  const float v[] = {0, 12, 30, 60, 45, 30, 20, 28, 50};
  uint32_t t = 0;
  int results = 0;
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
    bool ready = d.update(t, v[i], true);
    if (ready) {
      results++;
      TEST_ASSERT_EQUAL_FLOAT(60.0f, d.result().peak);
      TEST_ASSERT_FALSE(d.result().recoilOk);
      TEST_ASSERT_TRUE(d.pressStartedThisSample());
      TEST_ASSERT_EQUAL_UINT32(t, d.lastPressStartMs());
      TEST_ASSERT_EQUAL(cpr::Phase::Pressing, d.phase());
    }
    t += 20;
  }
  TEST_ASSERT_EQUAL_INT(1, results);
}

void test_detector_recoil_timeout() {
  cpr::Detector d;
  d.setThresholds(10.0f, 6.0f, 5.0f, 8.0f, 0.15f, 2000);

  const float v[] = {0, 12, 30, 60, 45, 20, 6};
  uint32_t t = 0;
  int results = 0;
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
    if (d.update(t, v[i], true)) results++;
    t += 20;
  }
  TEST_ASSERT_EQUAL_INT(0, results);

  TEST_ASSERT_FALSE(d.update(2119, 6.0f, true));
  TEST_ASSERT_EQUAL_INT(0, results);

  TEST_ASSERT_TRUE(d.update(2120, 6.0f, true));
  results++;
  TEST_ASSERT_EQUAL_INT(1, results);
  TEST_ASSERT_FALSE(d.result().recoilOk);
}

void test_rate() {
  struct Case { uint16_t bpm; cpr::RateLevel level; };
  const Case cases[] = {
      {90, cpr::RateLevel::Slow},
      {110, cpr::RateLevel::Good},
      {130, cpr::RateLevel::Fast},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    RunOut out = runScenario(cprsim::Scenario::Good, 20, 80, true, cases[i].bpm);
    TEST_ASSERT_TRUE(out.stats.count > 10);
    TEST_ASSERT_TRUE(absDiffInt((int)out.live.bpm, (int)cases[i].bpm) <= 3);
    TEST_ASSERT_EQUAL(cases[i].level, out.live.rate);
  }
}

void test_depth_mm() {
  struct Case { cprsim::Scenario sc; };
  const Case cases[] = {
      {cprsim::Scenario::Shallow},
      {cprsim::Scenario::Good},
      {cprsim::Scenario::Deep},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    RunOut out = runScenario(cases[i].sc, 20, 80, true);
    TEST_ASSERT_TRUE(out.stats.count > 10);
    uint8_t wanted;
    if (cases[i].sc == cprsim::Scenario::Shallow) wanted = percentOf(out.stats.depthShallow, out.stats.count);
    else if (cases[i].sc == cprsim::Scenario::Good) wanted = percentOf(out.stats.depthGood, out.stats.count);
    else wanted = percentOf(out.stats.depthDeep, out.stats.count);
    TEST_ASSERT_TRUE(wanted >= 90);
  }
}

void test_depth_kg() {
  struct Case { cprsim::Scenario sc; };
  const Case cases[] = {
      {cprsim::Scenario::Shallow},
      {cprsim::Scenario::Good},
      {cprsim::Scenario::Deep},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    RunOut out = runScenario(cases[i].sc, 20, 80, false, 0, 0.7f);
    TEST_ASSERT_TRUE(out.stats.count > 10);
    TEST_ASSERT_EQUAL(cpr::Source::ForceKg, out.live.source);
    uint8_t wanted;
    if (cases[i].sc == cprsim::Scenario::Shallow) wanted = percentOf(out.stats.depthShallow, out.stats.count);
    else if (cases[i].sc == cprsim::Scenario::Good) wanted = percentOf(out.stats.depthGood, out.stats.count);
    else wanted = percentOf(out.stats.depthDeep, out.stats.count);
    TEST_ASSERT_TRUE(wanted >= 90);
  }
}

void test_mapped_mode() {
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);
  const float mm[] = {0.0f, 35.0f, 54.0f, 68.0f};
  cal.mapCount = 4;
  for (uint8_t i = 0; i < 4; ++i) {
    cal.map[i].kg = simKg(mm[i]);
    cal.map[i].mm = mm[i];
  }

  struct Case { cprsim::Scenario sc; };
  const Case cases[] = {
      {cprsim::Scenario::Shallow},
      {cprsim::Scenario::Good},
      {cprsim::Scenario::Deep},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    RunOut out = runScenario(cases[i].sc, 20, 80, false, 0, 1.0f,
                             cpr::Mode::Free, &cal);
    TEST_ASSERT_TRUE(out.stats.count > 10);
    TEST_ASSERT_EQUAL(cpr::Source::MappedMm, out.live.source);
    uint8_t wanted;
    if (cases[i].sc == cprsim::Scenario::Shallow) wanted = percentOf(out.stats.depthShallow, out.stats.count);
    else if (cases[i].sc == cprsim::Scenario::Good) wanted = percentOf(out.stats.depthGood, out.stats.count);
    else wanted = percentOf(out.stats.depthDeep, out.stats.count);
    TEST_ASSERT_TRUE(wanted >= 90);
  }
}

void test_recoil() {
  RunOut good = runScenario(cprsim::Scenario::Good, 20, 80, true);
  TEST_ASSERT_TRUE(good.stats.count > 10);
  TEST_ASSERT_TRUE(percentOf(good.stats.recoilGood, good.stats.count) >= 90);

  RunOut no = runScenario(cprsim::Scenario::NoRecoil, 20, 80, true);
  TEST_ASSERT_TRUE(no.stats.count > 10);
  TEST_ASSERT_TRUE(percentOf(no.stats.recoilGood, no.stats.count) <= 10);
}

void test_hand() {
  RunOut out = runScenario(cprsim::Scenario::WrongHand, 20, 80, true);
  TEST_ASSERT_TRUE(out.stats.count > 10);
  uint8_t p = percentOf(out.stats.handGood, out.stats.count);
  TEST_ASSERT_TRUE(p >= 20);
  TEST_ASSERT_TRUE(p <= 40);
}

void test_noise() {
  const uint16_t seconds = 20;
  RunOut out = runScenario(cprsim::Scenario::Noisy, seconds, 80, true);
  uint32_t expected = (uint32_t)seconds * 1000UL * 110UL / 60000UL;
  TEST_ASSERT_TRUE(absDiffInt((int)out.stats.count, (int)expected) <= 1);
}

void test_cycle_30_2() {
  RunOut out = runScenario(cprsim::Scenario::Cycle30x2, 50, 80, true,
                           0, 1.0f, cpr::Mode::Cycle30x2);
  TEST_ASSERT_EQUAL_INT(2, out.cycleDone);
  TEST_ASSERT_EQUAL_INT(2, out.cyclePauseCount);
  for (uint8_t i = 0; i < out.cyclePauseCount; ++i) {
    TEST_ASSERT_TRUE(out.cyclePauseMs[i] >= 4500);
    TEST_ASSERT_TRUE(out.cyclePauseMs[i] <= 5600);
  }
  TEST_ASSERT_EQUAL_INT(2, out.stats.cycles);
}

void test_pause() {
  cpr::Engine engine;
  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Adult);
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);
  engine.begin(cfg, cal);
  engine.setTofPresent(true);

  cprsim::SimOptions opt;
  opt.sc = cprsim::Scenario::Good;
  opt.sps = 80;
  opt.tof = true;
  opt.seed = 77;
  opt.seconds = 20;
  opt.profile = cpr::Profile::Adult;

  cprsim::Generator gen;
  gen.begin(opt);

  uint16_t pauseStarts = 0;
  uint16_t pauseEnds = 0;
  cpr::Sample s;
  while (gen.next(s)) {
    if (s.tMs >= 5000 && s.tMs < 13000) {
      s.kg = 0.0f;
      s.mm = 0;
    }
    engine.process(s);
    cpr::Event ev;
    while (engine.pollEvent(ev)) {
      if (ev.type == cpr::EventType::PauseStart) pauseStarts++;
      if (ev.type == cpr::EventType::PauseEnd) pauseEnds++;
    }
  }

  const cpr::SessionStats& st = engine.stats();
  TEST_ASSERT_TRUE(pauseStarts >= 1);
  TEST_ASSERT_TRUE(pauseEnds >= 1);
  TEST_ASSERT_TRUE(st.handsOffMs >= 7000);
  TEST_ASSERT_TRUE(st.handsOffMs <= 9500);
  TEST_ASSERT_TRUE(st.longestPauseMs >= 7000);
  TEST_ASSERT_TRUE(st.longestPauseMs <= 9500);
  uint8_t ccf = cpr::ccfPercent(st);
  TEST_ASSERT_TRUE(ccf >= 45);
  TEST_ASSERT_TRUE(ccf <= 70);
}

void test_test2min() {
  RunOut out = runScenario(cprsim::Scenario::Good, 125, 20, true,
                           0, 1.0f, cpr::Mode::Test2Min);
  TEST_ASSERT_EQUAL_INT(1, out.endEvents);
  TEST_ASSERT_TRUE(out.stats.count > 0);
  uint32_t delta = out.endTMs - out.firstStartMs;
  TEST_ASSERT_TRUE(delta >= 120000);
  TEST_ASSERT_TRUE(delta <= 120100);
}

void test_low_sps() {
  RunOut slow = runScenario(cprsim::Scenario::Good, 6, 5, true);
  TEST_ASSERT_TRUE(slow.live.sps <= 8);
  TEST_ASSERT_TRUE(slow.live.lowSps);
  TEST_ASSERT_TRUE(slow.live.sps >= 4);

  RunOut stock = runScenario(cprsim::Scenario::Good, 6, 10, true);
  TEST_ASSERT_FALSE(stock.live.lowSps);
}

void test_metronome() {
  cpr::Metronome m;
  m.start(1000, 110);
  int ticks = 0;
  for (uint32_t t = 1000; t <= 61000; ++t) {
    if (m.tick(t)) ticks++;
  }
  TEST_ASSERT_EQUAL_INT(110, ticks);

  cpr::Metronome longRun;
  longRun.start(0, 110);
  int longTicks = 0;
  for (uint32_t t = 0; t <= 600000; t += 10) {
    if (longRun.tick(t)) longTicks++;
  }
  TEST_ASSERT_EQUAL_INT(1100, longTicks);
}

void test_calibration_roundtrip() {
  cpr::CalibrationData d;
  cpr::calibDefaults(d);

  cpr::CalPoint pairs[20];
  uint16_t n = 0;
  for (float mm = 5.0f; mm <= 70.0f && n < 20; mm += 5.0f) {
    pairs[n].kg = simKg(mm);
    pairs[n].mm = mm;
    n++;
  }
  TEST_ASSERT_TRUE(cpr::calibFit(pairs, n, d) >= 2);

  d.profile = (uint8_t)cpr::Profile::Child;
  d.scaleFactor = -9500.0f;
  d.goodMinKg = 5.5f;
  d.deepMinKg = 11.5f;

  uint8_t buf[96];
  size_t len = cpr::calibSerialize(d, buf, sizeof(buf));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= 96);

  cpr::CalibrationData back;
  TEST_ASSERT_TRUE(cpr::calibDeserialize(buf, len, back));
  TEST_ASSERT_EQUAL_INT(d.magic, back.magic);
  TEST_ASSERT_EQUAL_INT(d.version, back.version);
  TEST_ASSERT_EQUAL_INT(d.profile, back.profile);
  TEST_ASSERT_EQUAL_FLOAT(d.scaleFactor, back.scaleFactor);
  TEST_ASSERT_EQUAL_INT(d.mapCount, back.mapCount);
  TEST_ASSERT_EQUAL_FLOAT(d.goodMinKg, back.goodMinKg);
  TEST_ASSERT_EQUAL_FLOAT(d.deepMinKg, back.deepMinKg);
  for (uint8_t i = 0; i < d.mapCount; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(d.map[i].kg, back.map[i].kg);
    TEST_ASSERT_EQUAL_FLOAT(d.map[i].mm, back.map[i].mm);
  }
  TEST_ASSERT_EQUAL_INT(cpr::crc16(buf, len - 2), back.crc);
}

void test_calibration_default_child() {
  cpr::CalibrationData d;
  cpr::calibDefaults(d);
  TEST_ASSERT_EQUAL_INT((int)cpr::Profile::Child, (int)d.profile);

  cpr::Engine engine;
  TEST_ASSERT_EQUAL_INT((int)cpr::Profile::Child,
                        (int)engine.config().profile);
}

void test_calibration_crc_reject() {
  cpr::CalibrationData d;
  cpr::calibDefaults(d);
  uint8_t buf[96];
  size_t len = cpr::calibSerialize(d, buf, sizeof(buf));
  TEST_ASSERT_TRUE(len > 0);

  uint8_t bad[96];
  memcpy(bad, buf, len);
  bad[10] ^= 0x01;
  cpr::CalibrationData back;
  TEST_ASSERT_FALSE(cpr::calibDeserialize(bad, len, back));
  TEST_ASSERT_FALSE(cpr::calibDeserialize(buf, len - 1, back));
}

void test_calibration_fit_monotonic() {
  cpr::CalPoint pairs[40];
  uint16_t n = 0;
  for (float mm = 2.0f; mm <= 70.0f && n < 40; mm += 2.0f) {
    pairs[n].kg = simKg(mm) + ((n % 3) == 0 ? 0.05f : 0.0f);
    pairs[n].mm = mm;
    n++;
  }

  cpr::CalibrationData d;
  cpr::calibDefaults(d);
  uint8_t count = cpr::calibFit(pairs, n, d);
  TEST_ASSERT_TRUE(count >= 2);
  TEST_ASSERT_TRUE(count <= cpr::kMaxCalPoints);
  TEST_ASSERT_EQUAL_INT(count, d.mapCount);
  for (uint8_t i = 1; i < count; ++i) {
    TEST_ASSERT_TRUE(d.map[i].kg >= d.map[i - 1].kg);
    TEST_ASSERT_TRUE(d.map[i].mm > d.map[i - 1].mm);
  }
}

void test_calibration_map_interp() {
  cpr::CalibrationData d;
  cpr::calibDefaults(d);
  d.mapCount = 3;
  d.map[0].kg = 0.0f;  d.map[0].mm = 0.0f;
  d.map[1].kg = 10.0f; d.map[1].mm = 20.0f;
  d.map[2].kg = 20.0f; d.map[2].mm = 50.0f;

  TEST_ASSERT_EQUAL_FLOAT(10.0f, cpr::calibMapKgToMm(d, 5.0f));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, cpr::calibMapKgToMm(d, 10.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, cpr::calibMapKgToMm(d, -1.0f));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, cpr::calibMapKgToMm(d, 100.0f));

  d.mapCount = 1;
  TEST_ASSERT_TRUE(isnan(cpr::calibMapKgToMm(d, 5.0f)));
}

void test_command_parser() {
  cpr::Command c;

  TEST_ASSERT_TRUE(cpr::parseCommand("tare", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Tare, c.cmd);

  TEST_ASSERT_TRUE(cpr::parseCommand("Profile Child", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Profile, c.cmd);
  TEST_ASSERT_EQUAL_INT((int)cpr::Profile::Child, c.arg);

  TEST_ASSERT_TRUE(cpr::parseCommand("profile adult", c));
  TEST_ASSERT_EQUAL_INT((int)cpr::Profile::Adult, c.arg);

  TEST_ASSERT_TRUE(cpr::parseCommand("mode free", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Mode, c.cmd);
  TEST_ASSERT_EQUAL_INT((int)cpr::Mode::Free, c.arg);
  TEST_ASSERT_TRUE(cpr::parseCommand("mode cycle", c));
  TEST_ASSERT_EQUAL_INT((int)cpr::Mode::Cycle30x2, c.arg);
  TEST_ASSERT_TRUE(cpr::parseCommand("mode test", c));
  TEST_ASSERT_EQUAL_INT((int)cpr::Mode::Test2Min, c.arg);

  TEST_ASSERT_TRUE(cpr::parseCommand("start", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Start, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("stop", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Stop, c.cmd);

  TEST_ASSERT_TRUE(cpr::parseCommand("metro on", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::MetroOn, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("metro off", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::MetroOff, c.cmd);

  TEST_ASSERT_TRUE(cpr::parseCommand("cal start", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalStart, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal finish", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalFinish, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal cancel", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalCancel, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal weight 2.0", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalWeight, c.cmd);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, c.f1);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal bands 5.5 11.0", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalBands, c.cmd);
  TEST_ASSERT_EQUAL_FLOAT(5.5f, c.f1);
  TEST_ASSERT_EQUAL_FLOAT(11.0f, c.f2);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal peak", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalPeak, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal save", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalSave, c.cmd);
  TEST_ASSERT_TRUE(cpr::parseCommand("cal reset", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::CalReset, c.cmd);

  TEST_ASSERT_TRUE(cpr::parseCommand("config", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Config, c.cmd);

  TEST_ASSERT_TRUE(cpr::parseCommand("wifi pass abcd1234", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::WifiPass, c.cmd);
  TEST_ASSERT_EQUAL_STRING("abcd1234", c.text);
  TEST_ASSERT_FALSE(cpr::parseCommand("wifi pass short", c));

  TEST_ASSERT_FALSE(cpr::parseCommand("", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Unknown, c.cmd);
  TEST_ASSERT_FALSE(cpr::parseCommand("bogus command", c));
  TEST_ASSERT_EQUAL(cpr::Cmd::Unknown, c.cmd);
  TEST_ASSERT_FALSE(cpr::parseCommand("profile", c));
  TEST_ASSERT_FALSE(cpr::parseCommand("mode banana", c));
  TEST_ASSERT_FALSE(cpr::parseCommand("cal weight nope", c));
}

void test_json() {
  cpr::LiveState s;
  memset(&s, 0, sizeof(s));
  s.tMs = 12345;
  s.value = 48.5f;
  s.kg = 7.5f;
  s.mm = 48;
  s.source = cpr::Source::TofMm;
  s.phase = cpr::Phase::Pressing;
  s.liveDepth = cpr::DepthLevel::Good;
  s.bpm = 110;
  s.rate = cpr::RateLevel::Good;
  s.handOk = true;
  s.overload = false;
  s.inPause = false;
  s.count = 17;
  s.cycleCount = 17;
  s.cycles = 0;
  s.sps = 80;
  s.lowSps = false;
  s.sessionActive = true;
  s.elapsedS = 9;

  char buf[320];
  size_t len = cpr::jsonFrame(buf, sizeof(buf), s);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"ty\":\"f\",\"t\":12345,\"v\":48.5,\"kg\":7.5,\"mm\":48,\"src\":2,"
      "\"ph\":1,\"ld\":2,\"bpm\":110,\"rl\":2,\"hand\":1,\"ov\":0,\"ps\":0,"
      "\"n\":17,\"cc\":17,\"cy\":0,\"sps\":80,\"lsps\":0,\"act\":1,\"el\":9}",
      buf);

  cpr::Event e;
  memset(&e, 0, sizeof(e));
  e.type = cpr::EventType::Compression;
  e.tMs = 12400;
  e.c.index = 17;
  e.c.peak = 52.5f;
  e.c.source = cpr::Source::TofMm;
  e.c.depth = cpr::DepthLevel::Good;
  e.c.rate = cpr::RateLevel::Good;
  e.c.bpm = 110;
  e.c.recoilOk = true;
  e.c.handOk = true;
  e.c.allOk = true;
  len = cpr::jsonEvent(buf, sizeof(buf), e);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"ty\":\"c\",\"t\":12400,\"i\":17,\"pk\":52.5,\"src\":2,\"dl\":2,"
      "\"rl\":2,\"bpm\":110,\"rc\":1,\"hd\":1,\"ok\":1}",
      buf);

  cpr::SessionStats st;
  memset(&st, 0, sizeof(st));
  st.count = 4;
  st.depthGood = 3;
  st.depthShallow = 1;
  st.depthDeep = 0;
  st.rateRated = 4;
  st.rateGood = 3;
  st.rateSlow = 1;
  st.rateFast = 0;
  st.recoilGood = 3;
  st.handGood = 4;
  st.allGood = 2;
  st.firstMs = 1000;
  st.lastMs = 61000;
  st.handsOffMs = 6000;
  st.longestPauseMs = 6000;
  st.pausesTooLong = 0;
  st.sumPeak = 200.0f;
  st.sumBpm = 440;
  st.cycles = 1;

  len = cpr::jsonSummary(buf, sizeof(buf), st, cpr::Mode::Cycle30x2,
                         cpr::Profile::Adult);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"ty\":\"sum\",\"mode\":1,\"prof\":0,\"n\":4,\"dg\":3,\"dsh\":1,"
      "\"dd\":0,\"rr\":4,\"rg\":3,\"rs\":1,\"rf\":0,\"rc\":3,\"hd\":4,"
      "\"ok\":2,\"dur\":60,\"off\":6000,\"lp\":6000,\"plong\":0,\"pk\":50.0,"
      "\"bpm\":110,\"cy\":1,\"ccf\":90,\"score\":81}",
      buf);

  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Adult);
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);

  len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, false, "esp32", "1.0.0");
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"ty\":\"cfg\",\"board\":\"esp32\",\"fw\":\"1.0.0\",\"prof\":0,"
      "\"tof\":0,\"hx\":1,\"hand\":1,\"src\":0,\"unit\":\"kg\",\"gmin\":50,"
      "\"dmin\":60,\"gkg\":6.0,\"dkg\":12.0,\"ovkg\":18.0,\"ckg\":1.5,"
      "\"rkg\":0.5,\"flt\":0.50,\"rmin\":100,"
      "\"rmax\":120,\"map\":0,\"metro\":110}",
      buf);

  len = cpr::jsonAck(buf, sizeof(buf), "tare", true, "ok");
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("{\"ty\":\"ack\",\"cmd\":\"tare\",\"ok\":1,\"msg\":\"ok\"}", buf);

  cpr::LiveState nanState = s;
  nanState.value = NAN;
  nanState.kg = NAN;
  len = cpr::jsonFrame(buf, sizeof(buf), nanState);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"v\":null"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"kg\":null"));

  TEST_ASSERT_TRUE(cpr::jsonFrame(buf, sizeof(buf), s) <= cpr::kMaxJsonLine);
  TEST_ASSERT_TRUE(cpr::jsonEvent(buf, sizeof(buf), e) <= cpr::kMaxJsonLine);
  TEST_ASSERT_TRUE(cpr::jsonSummary(buf, sizeof(buf), st, cpr::Mode::Free,
                                    cpr::Profile::Child) <= cpr::kMaxJsonLine);
  TEST_ASSERT_TRUE(cpr::jsonConfig(buf, sizeof(buf), cfg, cal, true, "esp32",
                                   "1.0.0") <= cpr::kMaxJsonLine);
  TEST_ASSERT_TRUE(cpr::jsonAck(buf, sizeof(buf), "tare", false, "bad") <= cpr::kMaxJsonLine);

  char tiny[8];
  TEST_ASSERT_EQUAL_UINT32(0, cpr::jsonFrame(tiny, sizeof(tiny), s));
}

void test_json_config_extra() {
  char buf[cpr::kMaxJsonLine + 32];
  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Adult);
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);

  size_t len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, false, "esp32",
                               "1.0.0", false, false);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hx\":0"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hand\":0"));

  len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, false, "esp32",
                        "1.0.0", true, true);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hx\":1"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hand\":1"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"tof\":0,\"hx\":1,\"hand\":1,\"src\":0"));
}

void test_json_worst_case() {
  char buf[cpr::kMaxJsonLine + 32];

  cpr::SessionStats st;
  memset(&st, 0, sizeof(st));
  st.count = 65535;
  st.depthGood = 65535;
  st.depthShallow = 65535;
  st.depthDeep = 65535;
  st.rateRated = 65535;
  st.rateGood = 65535;
  st.rateSlow = 65535;
  st.rateFast = 65535;
  st.recoilGood = 65535;
  st.handGood = 65535;
  st.allGood = 65535;
  st.firstMs = 4294967295UL;
  st.lastMs = 4294967295UL;
  st.handsOffMs = 4294967295UL;
  st.longestPauseMs = 4294967295UL;
  st.pausesTooLong = 65535;
  st.sumPeak = 999.9f * 65535.0f;
  st.sumBpm = 4294967295UL;
  st.cycles = 255;

  size_t len = cpr::jsonSummary(buf, sizeof(buf), st, cpr::Mode::Test2Min,
                                cpr::Profile::Child);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);

  // A long session can make the duration field wider even when the rest is
  // already at its maximum.
  st.firstMs = 0;
  len = cpr::jsonSummary(buf, sizeof(buf), st, cpr::Mode::Cycle30x2,
                         cpr::Profile::Child);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);

  cpr::LiveState s;
  memset(&s, 0, sizeof(s));
  s.tMs = 4294967295UL;
  s.value = 429496000.0f;
  s.kg = 429496000.0f;
  s.mm = 32767;
  s.source = (cpr::Source)255;
  s.phase = (cpr::Phase)255;
  s.liveDepth = (cpr::DepthLevel)255;
  s.bpm = 255;
  s.rate = (cpr::RateLevel)255;
  s.handOk = true;
  s.overload = true;
  s.inPause = true;
  s.count = 65535;
  s.cycleCount = 255;
  s.cycles = 255;
  s.sps = 255;
  s.lowSps = true;
  s.sessionActive = true;
  s.elapsedS = 65535;

  len = cpr::jsonFrame(buf, sizeof(buf), s);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);

  const cpr::EventType types[] = {
      cpr::EventType::Compression, cpr::EventType::PauseStart,
      cpr::EventType::PauseEnd, cpr::EventType::CycleDone,
      cpr::EventType::SessionEnd, cpr::EventType::Overload};
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
    cpr::Event e;
    memset(&e, 0, sizeof(e));
    e.type = types[i];
    e.tMs = 4294967295UL;
    e.c.index = 65535;
    e.c.tStartMs = 4294967295UL;
    e.c.tPeakMs = 4294967295UL;
    e.c.peak = 429496000.0f;
    e.c.source = (cpr::Source)255;
    e.c.depth = (cpr::DepthLevel)255;
    e.c.rate = (cpr::RateLevel)255;
    e.c.bpm = 255;
    e.c.recoilOk = true;
    e.c.handOk = true;
    e.c.allOk = true;
    e.pauseMs = 4294967295UL;
    e.cycle = 255;
    e.pauseTooLong = true;

    len = cpr::jsonEvent(buf, sizeof(buf), e);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);
  }

  cpr::Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.profile = (cpr::Profile)255;
  cfg.th.goodMinMm = 999.9f;
  cfg.th.deepMinMm = 999.9f;
  cfg.th.contactMm = 999.9f;
  cfg.th.releaseMm = 999.9f;
  cfg.th.recoilMm = 999.9f;
  cfg.th.restartDeltaMm = 999.9f;
  cfg.th.contactKg = 429496000.0f;
  cfg.th.releaseKg = 429496000.0f;
  cfg.th.recoilKg = 429496000.0f;
  cfg.th.goodMinKg = 429496000.0f;
  cfg.th.deepMinKg = 429496000.0f;
  cfg.th.overloadKg = 429496000.0f;
  cfg.th.restartDeltaKg = 429496000.0f;
  cfg.th.peakDropFrac = 429496000.0f;
  cfg.th.rateMin = 65535;
  cfg.th.rateMax = 65535;
  cfg.th.handsOffMs = 65535;
  cfg.th.maxPauseMs = 65535;
  cfg.th.cycleCompressions = 255;
  cfg.th.lowSpsWarn = 255;
  cfg.filterAlpha = 429496000.0f;
  cfg.metronomeBpm = 65535;

  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);
  cal.mapCount = 255;
  cal.goodMinKg = 429496000.0f;
  cal.deepMinKg = 429496000.0f;

  len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, true, "esp32", "1.0.0");
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);
}

void test_json_config_worst_case() {
  char buf[cpr::kMaxJsonLine + 32];

  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Child);

  // Widest valid user thresholds: the bands, contact and recoil are near the
  // top of their ranges and the filter is at 1.00.
  cpr::UserTh th;
  th.good = 59.0f;
  th.deep = 59.5f;
  th.over = 60.0f;
  th.contact = 10.0f;
  th.recoil = 9.9f;
  th.filt = 1.0f;
  TEST_ASSERT_TRUE(cpr::userThValid(th));
  cpr::userThApply(th, cfg);

  cfg.profile = (cpr::Profile)255;
  cfg.th.goodMinMm = 999.9f;
  cfg.th.deepMinMm = 999.9f;
  cfg.th.rateMin = 65535;
  cfg.th.rateMax = 65535;
  cfg.metronomeBpm = 65535;

  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);
  cal.mapCount = 255;

  size_t len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, true, "esp32",
                               "1.0.0");
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"ckg\":10.0"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"rkg\":9.9"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"flt\":1.00"));

  len = cpr::jsonConfig(buf, sizeof(buf), cfg, cal, false, "esp32",
                        "1.0.0");
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len <= cpr::kMaxJsonLine);
}

void test_size() {
  size_t engine = sizeof(cpr::Engine);
  size_t recorder = sizeof(cpr::CalRecorder);
  size_t without = engine - recorder;
  printf("sizeof(Engine)=%zu sizeof(CalRecorder)=%zu engine_without_recorder=%zu\n",
         engine, recorder, without);
  TEST_ASSERT_TRUE(without <= 600);
}

void test_score() {
  cpr::SessionStats st;
  memset(&st, 0, sizeof(st));
  st.count = 4;
  st.depthGood = 3;
  st.depthShallow = 1;
  st.rateRated = 4;
  st.rateGood = 3;
  st.rateSlow = 1;
  st.recoilGood = 3;
  st.handGood = 4;
  st.firstMs = 1000;
  st.lastMs = 61000;
  st.handsOffMs = 6000;

  TEST_ASSERT_EQUAL_INT(75, cpr::pct(3, 4));
  TEST_ASSERT_EQUAL_INT(0, cpr::pct(1, 0));
  TEST_ASSERT_EQUAL_INT(90, cpr::ccfPercent(st));
  TEST_ASSERT_EQUAL_INT(81, cpr::score(st));
}

void test_autostart_off() {
  cpr::Engine engine;
  cpr::Config cfg = cpr::defaultConfig(cpr::Profile::Child);
  cpr::CalibrationData cal;
  cpr::calibDefaults(cal);
  engine.begin(cfg, cal);
  engine.setAutoStart(false);
  TEST_ASSERT_FALSE(engine.autoStart());

  // Pressing without startSession() must not activate a session.
  const float press[] = {0.0f, 3.0f, 8.0f, 8.0f, 4.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t t = 0;
  for (unsigned i = 0; i < sizeof(press) / sizeof(press[0]); ++i) {
    cpr::Sample s = {t, press[i], -1, true};
    engine.process(s);
    t += 50;
  }
  TEST_ASSERT_FALSE(engine.live().sessionActive);
  TEST_ASSERT_EQUAL_UINT16(0, engine.stats().count);

  // Calling startSession() arms the engine so the next press starts it.
  engine.startSession(t);
  for (unsigned i = 0; i < sizeof(press) / sizeof(press[0]); ++i) {
    cpr::Sample s = {t, press[i], -1, true};
    engine.process(s);
    t += 50;
  }
  TEST_ASSERT_TRUE(engine.live().sessionActive);
  TEST_ASSERT_EQUAL_UINT16(1, engine.stats().count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_filter);
  RUN_TEST(test_filter_median_off);
  RUN_TEST(test_settings_defaults);
  RUN_TEST(test_settings_ranges);
  RUN_TEST(test_settings_apply);
  RUN_TEST(test_detector_clean_press);
  RUN_TEST(test_detector_hysteresis_plateau);
  RUN_TEST(test_detector_leaning_restart);
  RUN_TEST(test_detector_recoil_timeout);
  RUN_TEST(test_rate);
  RUN_TEST(test_depth_mm);
  RUN_TEST(test_depth_kg);
  RUN_TEST(test_mapped_mode);
  RUN_TEST(test_recoil);
  RUN_TEST(test_hand);
  RUN_TEST(test_noise);
  RUN_TEST(test_cycle_30_2);
  RUN_TEST(test_pause);
  RUN_TEST(test_test2min);
  RUN_TEST(test_low_sps);
  RUN_TEST(test_metronome);
  RUN_TEST(test_calibration_roundtrip);
  RUN_TEST(test_calibration_default_child);
  RUN_TEST(test_calibration_crc_reject);
  RUN_TEST(test_calibration_fit_monotonic);
  RUN_TEST(test_calibration_map_interp);
  RUN_TEST(test_command_parser);
  RUN_TEST(test_json);
  RUN_TEST(test_json_config_extra);
  RUN_TEST(test_json_worst_case);
  RUN_TEST(test_json_config_worst_case);
  RUN_TEST(test_size);
  RUN_TEST(test_score);
  RUN_TEST(test_autostart_off);
  return UNITY_END();
}

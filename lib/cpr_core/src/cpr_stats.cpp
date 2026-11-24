#include "cpr_stats.h"

namespace cpr {

Stats::Stats() { reset(); }

void Stats::reset() {
  s_.count = 0;
  s_.depthGood = 0;
  s_.depthShallow = 0;
  s_.depthDeep = 0;
  s_.rateRated = 0;
  s_.rateGood = 0;
  s_.rateSlow = 0;
  s_.rateFast = 0;
  s_.recoilGood = 0;
  s_.handGood = 0;
  s_.allGood = 0;
  s_.firstMs = 0;
  s_.lastMs = 0;
  s_.handsOffMs = 0;
  s_.longestPauseMs = 0;
  s_.pausesTooLong = 0;
  s_.sumPeak = 0.0f;
  s_.sumBpm = 0;
  s_.cycles = 0;
}

void Stats::addCompression(const CompressionEvent& c) {
  if (s_.count == 0) s_.firstMs = c.tStartMs;
  s_.count++;
  s_.lastMs = c.tPeakMs;

  if (c.depth == DepthLevel::Good) s_.depthGood++;
  else if (c.depth == DepthLevel::Shallow) s_.depthShallow++;
  else if (c.depth == DepthLevel::Deep) s_.depthDeep++;

  if (c.rate != RateLevel::None) {
    s_.rateRated++;
    s_.sumBpm += c.bpm;
    if (c.rate == RateLevel::Good) s_.rateGood++;
    else if (c.rate == RateLevel::Slow) s_.rateSlow++;
    else if (c.rate == RateLevel::Fast) s_.rateFast++;
  }

  if (c.recoilOk) s_.recoilGood++;
  if (c.handOk) s_.handGood++;
  if (c.allOk) s_.allGood++;

  s_.sumPeak += c.peak;
}

void Stats::addPause(uint32_t pauseMs, const Thresholds& th) {
  if (pauseMs > th.handsOffMs) s_.handsOffMs += pauseMs;
  if (pauseMs > s_.longestPauseMs) s_.longestPauseMs = pauseMs;
  if (pauseMs > th.maxPauseMs) s_.pausesTooLong++;
}

void Stats::touch(uint32_t tMs) {
  if (tMs > s_.lastMs) s_.lastMs = tMs;
}

void Stats::setCycles(uint8_t cycles) { s_.cycles = cycles; }

const SessionStats& Stats::data() const { return s_; }

uint8_t pct(uint16_t part, uint16_t whole) {
  if (whole == 0) return 0;
  uint32_t v = ((uint32_t)part * 100UL + whole / 2) / whole;
  if (v > 100) v = 100;
  return (uint8_t)v;
}

uint8_t ccfPercent(const SessionStats& s) {
  if (s.lastMs <= s.firstMs) return 100;
  uint32_t duration = s.lastMs - s.firstMs;
  if (s.handsOffMs >= duration) return 0;
  uint32_t active = duration - s.handsOffMs;
  uint32_t v = (active * 100UL + duration / 2) / duration;
  if (v > 100) v = 100;
  return (uint8_t)v;
}

uint8_t score(const SessionStats& s) {
  uint8_t depthPct = pct(s.depthGood, s.count);
  uint8_t ratePct = pct(s.rateGood, s.rateRated);
  uint8_t recoilPct = pct(s.recoilGood, s.count);
  uint8_t handPct = pct(s.handGood, s.count);
  uint32_t ccfPct = ccfPercent(s);
  uint32_t ccfScore = (ccfPct * 100UL) / 60UL;
  if (ccfScore > 100) ccfScore = 100;

  uint32_t total = (uint32_t)depthPct * 30UL +
                   (uint32_t)ratePct * 25UL +
                   (uint32_t)recoilPct * 20UL +
                   (uint32_t)handPct * 10UL +
                   ccfScore * 15UL;
  uint32_t v = (total + 50UL) / 100UL;
  if (v > 100) v = 100;
  return (uint8_t)v;
}

} // namespace cpr

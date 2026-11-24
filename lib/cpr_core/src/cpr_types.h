#ifndef CPR_TYPES_H
#define CPR_TYPES_H

#include <stdint.h>

namespace cpr {

enum class DepthLevel : uint8_t { None = 0, Shallow = 1, Good = 2, Deep = 3 };
enum class RateLevel  : uint8_t { None = 0, Slow = 1, Good = 2, Fast = 3 };
enum class Phase      : uint8_t { Idle = 0, Pressing = 1, Peak = 2, Releasing = 3 };
enum class Profile    : uint8_t { Adult = 0, Child = 1 };
enum class Mode       : uint8_t { Free = 0, Cycle30x2 = 1, Test2Min = 2 };
enum class Source     : uint8_t { ForceKg = 0, MappedMm = 1, TofMm = 2 };

struct Sample {
  uint32_t tMs;
  float    kg;
  int16_t  mm;
  bool     handOk;
};

struct Thresholds {
  float goodMinMm, deepMinMm;
  float contactMm, releaseMm, recoilMm, restartDeltaMm;
  float contactKg, releaseKg, recoilKg, goodMinKg, deepMinKg, overloadKg, restartDeltaKg;
  float peakDropFrac;
  uint16_t rateMin, rateMax;
  uint16_t handsOffMs, maxPauseMs;
  uint8_t  cycleCompressions;
  uint8_t  lowSpsWarn;
};

struct Config {
  Profile    profile;
  Thresholds th;
  float      filterAlpha;
  uint16_t   metronomeBpm;
};

struct CompressionEvent {
  uint16_t   index;
  uint32_t   tStartMs;
  uint32_t   tPeakMs;
  float      peak;
  Source     source;
  DepthLevel depth;
  RateLevel  rate;
  uint8_t    bpm;
  bool       recoilOk;
  bool       handOk;
  bool       allOk;
};

enum class EventType : uint8_t { Compression = 0, PauseStart = 1, PauseEnd = 2,
                                 CycleDone = 3, SessionEnd = 4, Overload = 5 };

struct Event {
  EventType type;
  uint32_t  tMs;
  CompressionEvent c;
  uint32_t  pauseMs;
  uint8_t   cycle;
  bool      pauseTooLong;
};

struct LiveState {
  uint32_t   tMs;
  float      value;
  float      kg;
  int16_t    mm;
  Source     source;
  Phase      phase;
  DepthLevel liveDepth;
  uint8_t    bpm;
  RateLevel  rate;
  bool       handOk;
  bool       overload;
  bool       inPause;
  uint16_t   count;
  uint8_t    cycleCount;
  uint8_t    cycles;
  uint8_t    sps;
  bool       lowSps;
  bool       sessionActive;
  uint16_t   elapsedS;
};

struct SessionStats {
  uint16_t count;
  uint16_t depthGood, depthShallow, depthDeep;
  uint16_t rateRated, rateGood, rateSlow, rateFast;
  uint16_t recoilGood;
  uint16_t handGood;
  uint16_t allGood;
  uint32_t firstMs, lastMs;
  uint32_t handsOffMs;
  uint32_t longestPauseMs;
  uint16_t pausesTooLong;
  float    sumPeak;
  uint32_t sumBpm;
  uint8_t  cycles;
};

} // namespace cpr

#endif

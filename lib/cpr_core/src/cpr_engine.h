#ifndef CPR_ENGINE_H
#define CPR_ENGINE_H

#include "cpr_calib.h"
#include "cpr_detector.h"
#include "cpr_filter.h"
#include "cpr_rate.h"
#include "cpr_samplerate.h"
#include "cpr_stats.h"
#include "cpr_training.h"
#include "cpr_types.h"

namespace cpr {

class Engine {
 public:
  Engine();
  void begin(const Config& cfg, const CalibrationData& cal);
  void setConfig(const Config& cfg);
  void setCalibration(const CalibrationData& cal);
  void setFastFilter(bool on);
  void setMode(Mode m);
  void setTofPresent(bool present);
  void startSession(uint32_t tMs);
  void endSession(uint32_t tMs);
  void process(const Sample& s);
  bool pollEvent(Event& out);
  const LiveState& live() const;
  const SessionStats& stats() const;
  const Config& config() const;
  Source source() const;

  void calStart();
  void calCancel();
  bool calActive() const;
  uint16_t calCount() const;
  bool calFinish(CalibrationData& out);

 private:
  void configureDetector();
  void pushEvent(const Event& e);
  DepthLevel depthFor(float value, Source src) const;
  void handleResult(uint32_t tMs);
  void handlePressStart(uint32_t tMs);

  CalibrationData cal_;
  Config cfg_;
  bool tofPresent_;
  Mode mode_;
  Source source_;
  Source pressSource_;

  Detector det_;
  RateEstimator rate_;
  Stats stats_;
  Training training_;
  SampleRateMeter spsMeter_;
  Filter valueFilter_;
  Filter kgFilter_;

  LiveState live_;

  struct QueuedEvent {
    EventType type;
    uint8_t cycle;
    bool pauseTooLong;
    uint32_t tMs;
    uint32_t pauseMs;
    uint16_t index;
    uint8_t source;
    uint8_t depth;
    uint8_t rate;
    uint8_t bpm;
    uint8_t flags;
    uint32_t tStartMs;
    uint32_t tPeakMs;
    float peak;
  };
  QueuedEvent events_[4];
  uint8_t eventHead_;
  uint8_t eventTail_;
  uint8_t eventCount_;

  bool meterStarted_ : 1;
  bool kgSeen_ : 1;
  bool overload_ : 1;
  bool sessionActive_ : 1;
  bool inPause_ : 1;
  bool calActive_ : 1;
  bool pressInProgress_ : 1;

  uint32_t sessionStartMs_;
  uint32_t lastPressActivityMs_;
  uint32_t lastPressFinishMs_;
  uint16_t compIndex_;

  CalRecorder recorder_;
};

} // namespace cpr

#endif

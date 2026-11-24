#include "cpr_json.h"

#include "cpr_stats.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace cpr {

namespace {

class JsonBuf {
 public:
  JsonBuf(char* buf, size_t cap) : buf_(buf), cap_(cap), len_(0), ok_(cap > 1) {
    if (buf_ != nullptr && cap_ > 0) buf_[0] = '\0';
  }

  bool ok() const { return ok_; }
  size_t len() const { return len_; }

  void ch(char c) {
    if (!ok_) return;
    if (len_ + 1 >= cap_) {
      ok_ = false;
      return;
    }
    buf_[len_++] = c;
    buf_[len_] = '\0';
  }

  void str(const char* s) {
    if (s == nullptr) s = "";
    while (*s) ch(*s++);
  }

  void u32(uint32_t v) {
    char tmp[10];
    uint8_t n = 0;
    if (v == 0) {
      ch('0');
      return;
    }
    while (v != 0) {
      tmp[n++] = (char)('0' + (v % 10));
      v /= 10;
    }
    while (n > 0) ch(tmp[--n]);
  }

  void i32(int32_t v) {
    if (v < 0) {
      ch('-');
      u32((uint32_t)(-v));
    } else {
      u32((uint32_t)v);
    }
  }

  void f1(float v) {
    if (isnan(v)) {
      str("null");
      return;
    }
    bool neg = v < 0.0f;
    if (neg) v = -v;
    if (v > 429496000.0f) {
      str("null");
      return;
    }
    uint32_t scaled = (uint32_t)(v * 10.0f + 0.5f);
    uint32_t whole = scaled / 10;
    uint8_t frac = (uint8_t)(scaled % 10);
    if (neg && scaled != 0) ch('-');
    u32(whole);
    ch('.');
    ch((char)('0' + frac));
  }

  void boolean(bool v) { ch(v ? '1' : '0'); }

  void jsonString(const char* s) {
    if (s == nullptr) s = "";
    ch('"');
    for (const char* p = s; *p; ++p) {
      unsigned char c = (unsigned char)*p;
      if (c == '"' || c == '\\') {
        ch('\\');
        ch((char)c);
      } else if (c == '\n') {
        str("\\n");
      } else if (c == '\r') {
        str("\\r");
      } else if (c == '\t') {
        str("\\t");
      } else if (c < 0x20) {
        ch('?');
      } else {
        ch((char)c);
      }
    }
    ch('"');
  }

 private:
  char* buf_;
  size_t cap_;
  size_t len_;
  bool ok_;
};

int32_t roundInt(float v) {
  if (isnan(v)) return 0;
  return (int32_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

void key(JsonBuf& b, const char* name) {
  b.ch(',');
  b.ch('"');
  b.str(name);
  b.str("\":");
}

} // namespace

size_t jsonFrame(char* buf, size_t cap, const LiveState& s) {
  JsonBuf b(buf, cap);
  b.str("{\"ty\":\"f\"");
  key(b, "t"); b.u32(s.tMs);
  key(b, "v"); b.f1(s.value);
  key(b, "kg"); b.f1(s.kg);
  key(b, "mm"); b.i32(s.mm);
  key(b, "src"); b.u32((uint32_t)s.source);
  key(b, "ph"); b.u32((uint32_t)s.phase);
  key(b, "ld"); b.u32((uint32_t)s.liveDepth);
  key(b, "bpm"); b.u32(s.bpm);
  key(b, "rl"); b.u32((uint32_t)s.rate);
  key(b, "hand"); b.boolean(s.handOk);
  key(b, "ov"); b.boolean(s.overload);
  key(b, "ps"); b.boolean(s.inPause);
  key(b, "n"); b.u32(s.count);
  key(b, "cc"); b.u32(s.cycleCount);
  key(b, "cy"); b.u32(s.cycles);
  key(b, "sps"); b.u32(s.sps);
  key(b, "lsps"); b.boolean(s.lowSps);
  key(b, "act"); b.boolean(s.sessionActive);
  key(b, "el"); b.u32(s.elapsedS);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonEvent(char* buf, size_t cap, const Event& e) {
  JsonBuf b(buf, cap);
  switch (e.type) {
    case EventType::Compression:
      b.str("{\"ty\":\"c\"");
      key(b, "t"); b.u32(e.tMs);
      key(b, "i"); b.u32(e.c.index);
      key(b, "pk"); b.f1(e.c.peak);
      key(b, "src"); b.u32((uint32_t)e.c.source);
      key(b, "dl"); b.u32((uint32_t)e.c.depth);
      key(b, "rl"); b.u32((uint32_t)e.c.rate);
      key(b, "bpm"); b.u32(e.c.bpm);
      key(b, "rc"); b.boolean(e.c.recoilOk);
      key(b, "hd"); b.boolean(e.c.handOk);
      key(b, "ok"); b.boolean(e.c.allOk);
      break;
    case EventType::PauseStart:
      b.str("{\"ty\":\"ps\"");
      key(b, "t"); b.u32(e.tMs);
      break;
    case EventType::PauseEnd:
      b.str("{\"ty\":\"pe\"");
      key(b, "t"); b.u32(e.tMs);
      key(b, "ms"); b.u32(e.pauseMs);
      key(b, "long"); b.boolean(e.pauseTooLong);
      break;
    case EventType::CycleDone:
      b.str("{\"ty\":\"cy\"");
      key(b, "t"); b.u32(e.tMs);
      key(b, "cy"); b.u32(e.cycle);
      key(b, "ms"); b.u32(e.pauseMs);
      key(b, "long"); b.boolean(e.pauseTooLong);
      break;
    case EventType::SessionEnd:
      b.str("{\"ty\":\"end\"");
      key(b, "t"); b.u32(e.tMs);
      break;
    case EventType::Overload:
      b.str("{\"ty\":\"ov\"");
      key(b, "t"); b.u32(e.tMs);
      break;
  }
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonSummary(char* buf, size_t cap, const SessionStats& s, Mode m, Profile p) {
  JsonBuf b(buf, cap);
  uint32_t dur = (s.lastMs > s.firstMs) ? (s.lastMs - s.firstMs) / 1000UL : 0UL;
  float meanPeak = (s.count != 0) ? (s.sumPeak / (float)s.count) : 0.0f;
  uint32_t meanBpm = (s.rateRated != 0)
                         ? ((s.sumBpm + s.rateRated / 2) / s.rateRated)
                         : 0;

  b.str("{\"ty\":\"sum\"");
  key(b, "mode"); b.u32((uint32_t)m);
  key(b, "prof"); b.u32((uint32_t)p);
  key(b, "n"); b.u32(s.count);
  key(b, "dg"); b.u32(s.depthGood);
  key(b, "dsh"); b.u32(s.depthShallow);
  key(b, "dd"); b.u32(s.depthDeep);
  key(b, "rr"); b.u32(s.rateRated);
  key(b, "rg"); b.u32(s.rateGood);
  key(b, "rs"); b.u32(s.rateSlow);
  key(b, "rf"); b.u32(s.rateFast);
  key(b, "rc"); b.u32(s.recoilGood);
  key(b, "hd"); b.u32(s.handGood);
  key(b, "ok"); b.u32(s.allGood);
  key(b, "dur"); b.u32(dur);
  key(b, "off"); b.u32(s.handsOffMs);
  key(b, "lp"); b.u32(s.longestPauseMs);
  key(b, "plong"); b.u32(s.pausesTooLong);
  key(b, "pk"); b.f1(meanPeak);
  key(b, "bpm"); b.u32(meanBpm);
  key(b, "cy"); b.u32(s.cycles);
  key(b, "ccf"); b.u32(ccfPercent(s));
  key(b, "score"); b.u32(score(s));
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonConfig(char* buf, size_t cap, const Config& c, const CalibrationData& cal,
                  bool tofPresent, const char* board, const char* fw) {
  Source src;
  if (tofPresent) src = Source::TofMm;
  else if (cal.mapCount >= 2) src = Source::MappedMm;
  else src = Source::ForceKg;
  const char* unit = (src == Source::ForceKg) ? "kg" : "mm";

  JsonBuf b(buf, cap);
  b.str("{\"ty\":\"cfg\"");
  key(b, "board"); b.jsonString(board);
  key(b, "fw"); b.jsonString(fw);
  key(b, "prof"); b.u32((uint32_t)c.profile);
  key(b, "tof"); b.boolean(tofPresent);
  key(b, "src"); b.u32((uint32_t)src);
  key(b, "unit"); b.jsonString(unit);
  key(b, "gmin"); b.i32(roundInt(c.th.goodMinMm));
  key(b, "dmin"); b.i32(roundInt(c.th.deepMinMm));
  key(b, "gkg"); b.f1(c.th.goodMinKg);
  key(b, "dkg"); b.f1(c.th.deepMinKg);
  key(b, "ovkg"); b.f1(c.th.overloadKg);
  key(b, "rmin"); b.u32(c.th.rateMin);
  key(b, "rmax"); b.u32(c.th.rateMax);
  key(b, "map"); b.u32(cal.mapCount);
  key(b, "metro"); b.u32(c.metronomeBpm);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonAck(char* buf, size_t cap, const char* cmd, bool ok, const char* msg) {
  JsonBuf b(buf, cap);
  b.str("{\"ty\":\"ack\"");
  key(b, "cmd"); b.jsonString(cmd);
  key(b, "ok"); b.boolean(ok);
  key(b, "msg"); b.jsonString(msg);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

} // namespace cpr

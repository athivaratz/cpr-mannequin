#include "cpr_json.h"

#include "cpr_stats.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// On AVR every literal otherwise lands in RAM. Keep the protocol field names
// in flash and read them one byte at a time.
#if defined(__AVR__)
#define CPR_PROGMEM __attribute__((__progmem__))
#define CPR_PSTR(s) \
  (__extension__({ static const char cpr_pstr[] CPR_PROGMEM = (s); cpr_pstr; }))
static inline char cprReadPgmChar(const char* p) {
  uint8_t v;
  __asm__ __volatile__("lpm %0, Z" : "=r"(v) : "z"(p));
  return (char)v;
}
#define CPR_PGM_CHAR(p) cprReadPgmChar(p)
#else
#define CPR_PSTR(s) (s)
#define CPR_PGM_CHAR(p) (*(const char*)(p))
#endif

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

  void strP(const char* s) {
    if (s == nullptr) return;
    char c = CPR_PGM_CHAR(s++);
    while (c) {
      ch(c);
      c = CPR_PGM_CHAR(s++);
    }
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
      strP(CPR_PSTR("null"));
      return;
    }
    bool neg = v < 0.0f;
    if (neg) v = -v;
    if (v > 429496000.0f) {
      strP(CPR_PSTR("null"));
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

  void f2(float v) {
    if (isnan(v)) {
      strP(CPR_PSTR("null"));
      return;
    }
    bool neg = v < 0.0f;
    if (neg) v = -v;
    if (v > 42949600.0f) {
      strP(CPR_PSTR("null"));
      return;
    }
    uint32_t scaled = (uint32_t)(v * 100.0f + 0.5f);
    uint32_t whole = scaled / 100;
    uint8_t frac = (uint8_t)(scaled % 100);
    if (neg && scaled != 0) ch('-');
    u32(whole);
    ch('.');
    ch((char)('0' + frac / 10));
    ch((char)('0' + frac % 10));
  }

  void jsonString(const char* s) {
    if (s == nullptr) s = "";
    ch('"');
    for (const char* p = s; *p; ++p) {
      unsigned char c = (unsigned char)*p;
      if (c == '"' || c == '\\') {
        ch('\\');
        ch((char)c);
      } else if (c == '\n') {
        strP(CPR_PSTR("\\n"));
      } else if (c == '\r') {
        strP(CPR_PSTR("\\r"));
      } else if (c == '\t') {
        strP(CPR_PSTR("\\t"));
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
  b.strP(name);
  b.strP(CPR_PSTR("\":"));
}

} // namespace

size_t jsonFrame(char* buf, size_t cap, const LiveState& s) {
  JsonBuf b(buf, cap);
  b.strP(CPR_PSTR("{\"ty\":\"f\""));
  key(b, CPR_PSTR("t")); b.u32(s.tMs);
  key(b, CPR_PSTR("v")); b.f1(s.value);
  key(b, CPR_PSTR("kg")); b.f1(s.kg);
  key(b, CPR_PSTR("mm")); b.i32(s.mm);
  key(b, CPR_PSTR("src")); b.u32((uint32_t)s.source);
  key(b, CPR_PSTR("ph")); b.u32((uint32_t)s.phase);
  key(b, CPR_PSTR("ld")); b.u32((uint32_t)s.liveDepth);
  key(b, CPR_PSTR("bpm")); b.u32(s.bpm);
  key(b, CPR_PSTR("rl")); b.u32((uint32_t)s.rate);
  key(b, CPR_PSTR("hand")); b.boolean(s.handOk);
  key(b, CPR_PSTR("ov")); b.boolean(s.overload);
  key(b, CPR_PSTR("ps")); b.boolean(s.inPause);
  key(b, CPR_PSTR("n")); b.u32(s.count);
  key(b, CPR_PSTR("cc")); b.u32(s.cycleCount);
  key(b, CPR_PSTR("cy")); b.u32(s.cycles);
  key(b, CPR_PSTR("sps")); b.u32(s.sps);
  key(b, CPR_PSTR("lsps")); b.boolean(s.lowSps);
  key(b, CPR_PSTR("act")); b.boolean(s.sessionActive);
  key(b, CPR_PSTR("el")); b.u32(s.elapsedS);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonEvent(char* buf, size_t cap, const Event& e) {
  JsonBuf b(buf, cap);
  switch (e.type) {
    case EventType::Compression:
      b.strP(CPR_PSTR("{\"ty\":\"c\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
      key(b, CPR_PSTR("i")); b.u32(e.c.index);
      key(b, CPR_PSTR("pk")); b.f1(e.c.peak);
      key(b, CPR_PSTR("src")); b.u32((uint32_t)e.c.source);
      key(b, CPR_PSTR("dl")); b.u32((uint32_t)e.c.depth);
      key(b, CPR_PSTR("rl")); b.u32((uint32_t)e.c.rate);
      key(b, CPR_PSTR("bpm")); b.u32(e.c.bpm);
      key(b, CPR_PSTR("rc")); b.boolean(e.c.recoilOk);
      key(b, CPR_PSTR("hd")); b.boolean(e.c.handOk);
      key(b, CPR_PSTR("ok")); b.boolean(e.c.allOk);
      break;
    case EventType::PauseStart:
      b.strP(CPR_PSTR("{\"ty\":\"ps\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
      break;
    case EventType::PauseEnd:
      b.strP(CPR_PSTR("{\"ty\":\"pe\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
      key(b, CPR_PSTR("ms")); b.u32(e.pauseMs);
      key(b, CPR_PSTR("long")); b.boolean(e.pauseTooLong);
      break;
    case EventType::CycleDone:
      b.strP(CPR_PSTR("{\"ty\":\"cy\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
      key(b, CPR_PSTR("cy")); b.u32(e.cycle);
      key(b, CPR_PSTR("ms")); b.u32(e.pauseMs);
      key(b, CPR_PSTR("long")); b.boolean(e.pauseTooLong);
      break;
    case EventType::SessionEnd:
      b.strP(CPR_PSTR("{\"ty\":\"end\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
      break;
    case EventType::Overload:
      b.strP(CPR_PSTR("{\"ty\":\"ov\""));
      key(b, CPR_PSTR("t")); b.u32(e.tMs);
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

  b.strP(CPR_PSTR("{\"ty\":\"sum\""));
  key(b, CPR_PSTR("mode")); b.u32((uint32_t)m);
  key(b, CPR_PSTR("prof")); b.u32((uint32_t)p);
  key(b, CPR_PSTR("n")); b.u32(s.count);
  key(b, CPR_PSTR("dg")); b.u32(s.depthGood);
  key(b, CPR_PSTR("dsh")); b.u32(s.depthShallow);
  key(b, CPR_PSTR("dd")); b.u32(s.depthDeep);
  key(b, CPR_PSTR("rr")); b.u32(s.rateRated);
  key(b, CPR_PSTR("rg")); b.u32(s.rateGood);
  key(b, CPR_PSTR("rs")); b.u32(s.rateSlow);
  key(b, CPR_PSTR("rf")); b.u32(s.rateFast);
  key(b, CPR_PSTR("rc")); b.u32(s.recoilGood);
  key(b, CPR_PSTR("hd")); b.u32(s.handGood);
  key(b, CPR_PSTR("ok")); b.u32(s.allGood);
  key(b, CPR_PSTR("dur")); b.u32(dur);
  key(b, CPR_PSTR("off")); b.u32(s.handsOffMs);
  key(b, CPR_PSTR("lp")); b.u32(s.longestPauseMs);
  key(b, CPR_PSTR("plong")); b.u32(s.pausesTooLong);
  key(b, CPR_PSTR("pk")); b.f1(meanPeak);
  key(b, CPR_PSTR("bpm")); b.u32(meanBpm);
  key(b, CPR_PSTR("cy")); b.u32(s.cycles);
  key(b, CPR_PSTR("ccf")); b.u32(ccfPercent(s));
  key(b, CPR_PSTR("score")); b.u32(score(s));
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonConfig(char* buf, size_t cap, const Config& c, const CalibrationData& cal,
                  bool tofPresent, const char* board, const char* fw,
                  bool hxPresent, bool handSensor) {
  Source src;
  if (tofPresent) src = Source::TofMm;
  else if (cal.mapCount >= 2) src = Source::MappedMm;
  else src = Source::ForceKg;
  const char* unit = (src == Source::ForceKg) ? "kg" : "mm";

  JsonBuf b(buf, cap);
  b.strP(CPR_PSTR("{\"ty\":\"cfg\""));
  key(b, CPR_PSTR("board")); b.jsonString(board);
  key(b, CPR_PSTR("fw")); b.jsonString(fw);
  key(b, CPR_PSTR("prof")); b.u32((uint32_t)c.profile);
  key(b, CPR_PSTR("tof")); b.boolean(tofPresent);
#if CPR_JSON_EXTRA
  key(b, CPR_PSTR("hx")); b.boolean(hxPresent);
  key(b, CPR_PSTR("hand")); b.boolean(handSensor);
#endif
  key(b, CPR_PSTR("src")); b.u32((uint32_t)src);
  key(b, CPR_PSTR("unit")); b.jsonString(unit);
  key(b, CPR_PSTR("gmin")); b.i32(roundInt(c.th.goodMinMm));
  key(b, CPR_PSTR("dmin")); b.i32(roundInt(c.th.deepMinMm));
  key(b, CPR_PSTR("gkg")); b.f1(c.th.goodMinKg);
  key(b, CPR_PSTR("dkg")); b.f1(c.th.deepMinKg);
  key(b, CPR_PSTR("ovkg")); b.f1(c.th.overloadKg);
#if CPR_JSON_EXTRA
  key(b, CPR_PSTR("ckg")); b.f1(c.th.contactKg);
  key(b, CPR_PSTR("rkg")); b.f1(c.th.recoilKg);
  key(b, CPR_PSTR("flt")); b.f2(c.filterAlpha);
#endif
  key(b, CPR_PSTR("rmin")); b.u32(c.th.rateMin);
  key(b, CPR_PSTR("rmax")); b.u32(c.th.rateMax);
  key(b, CPR_PSTR("map")); b.u32(cal.mapCount);
  key(b, CPR_PSTR("metro")); b.u32(c.metronomeBpm);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

size_t jsonAck(char* buf, size_t cap, const char* cmd, bool ok, const char* msg) {
  JsonBuf b(buf, cap);
  b.strP(CPR_PSTR("{\"ty\":\"ack\""));
  key(b, CPR_PSTR("cmd")); b.jsonString(cmd);
  key(b, CPR_PSTR("ok")); b.boolean(ok);
  key(b, CPR_PSTR("msg")); b.jsonString(msg);
  b.ch('}');
  return b.ok() ? b.len() : 0;
}

} // namespace cpr

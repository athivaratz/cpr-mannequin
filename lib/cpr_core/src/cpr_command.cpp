#include "cpr_command.h"

#include <string.h>

// Keep command words in flash on AVR; the parser compares them byte by byte.
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
#define EQ(a, lit) strEqP((a), CPR_PSTR(lit))

namespace cpr {

namespace {

bool strEqP(const char* a, const char* b) {
  while (*a) {
    if (*a != CPR_PGM_CHAR(b)) return false;
    ++a;
    ++b;
  }
  return CPR_PGM_CHAR(b) == '\0';
}

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

const char* skipWs(const char* p) {
  while (*p && isSpace(*p)) p++;
  return p;
}

void lower(char* s) {
  for (char* p = s; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
  }
}

bool readWord(const char*& p, char* out, size_t cap) {
  p = skipWs(p);
  if (*p == '\0') {
    out[0] = '\0';
    return false;
  }
  size_t n = 0;
  while (*p && !isSpace(*p)) {
    if (n + 1 >= cap) return false;
    out[n++] = *p++;
  }
  out[n] = '\0';
  return true;
}

bool noMore(const char* p) { return *skipWs(p) == '\0'; }

bool parseFloatTok(const char* s, float& out) {
  if (s == nullptr || *s == '\0') return false;
  const char* p = s;

  bool neg = false;
  if (*p == '+' || *p == '-') {
    neg = (*p == '-');
    p++;
  }

  bool haveDigits = false;
  double v = 0.0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10.0 + (double)(*p - '0');
    haveDigits = true;
    p++;
  }

  if (*p == '.') {
    p++;
    double scale = 0.1;
    while (*p >= '0' && *p <= '9') {
      v += (double)(*p - '0') * scale;
      scale *= 0.1;
      haveDigits = true;
      p++;
    }
  }

  if (!haveDigits) return false;

  if (*p == 'e' || *p == 'E') {
    p++;
    bool expNeg = false;
    if (*p == '+' || *p == '-') {
      expNeg = (*p == '-');
      p++;
    }
    if (*p < '0' || *p > '9') return false;

    int exponent = 0;
    while (*p >= '0' && *p <= '9') {
      if (exponent < 1000) exponent = exponent * 10 + (*p - '0');
      p++;
    }
    while (exponent-- > 0) {
      v = expNeg ? v / 10.0 : v * 10.0;
    }
  }

  if (*p != '\0') return false;
  if (!(v == v)) return false;

  out = (float)(neg ? -v : v);
  return true;
}

bool oneFloat(const char*& p, float& out) {
  char tok[32];
  if (!readWord(p, tok, sizeof(tok))) return false;
  if (!parseFloatTok(tok, out)) return false;
  return noMore(p);
}

bool twoFloats(const char*& p, float& a, float& b) {
  char tok[32];
  if (!readWord(p, tok, sizeof(tok))) return false;
  if (!parseFloatTok(tok, a)) return false;
  if (!readWord(p, tok, sizeof(tok))) return false;
  if (!parseFloatTok(tok, b)) return false;
  return noMore(p);
}

} // namespace

bool parseCommand(const char* line, Command& out) {
  out.cmd = Cmd::Unknown;
  out.arg = 0;
  out.f1 = 0.0f;
  out.f2 = 0.0f;
  out.text[0] = '\0';

  if (line == nullptr) return false;
  const char* p = line;
  char verb[16];
  if (!readWord(p, verb, sizeof(verb))) return false;
  lower(verb);

  if (EQ(verb, "tare")) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Tare;
    return true;
  }

  if (EQ(verb, "profile")) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (EQ(a, "adult")) {
      out.cmd = Cmd::Profile;
      out.arg = (uint8_t)Profile::Adult;
      return true;
    }
    if (EQ(a, "child")) {
      out.cmd = Cmd::Profile;
      out.arg = (uint8_t)Profile::Child;
      return true;
    }
    return false;
  }

  if (EQ(verb, "mode")) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (EQ(a, "free")) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Free;
      return true;
    }
    if (EQ(a, "cycle")) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Cycle30x2;
      return true;
    }
    if (EQ(a, "test")) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Test2Min;
      return true;
    }
    return false;
  }

  if (EQ(verb, "start")) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Start;
    return true;
  }

  if (EQ(verb, "stop")) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Stop;
    return true;
  }

  if (EQ(verb, "metro")) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (EQ(a, "on")) {
      out.cmd = Cmd::MetroOn;
      return true;
    }
    if (EQ(a, "off")) {
      out.cmd = Cmd::MetroOff;
      return true;
    }
    return false;
  }

  if (EQ(verb, "cal")) {
    char sub[16];
    if (!readWord(p, sub, sizeof(sub))) return false;
    lower(sub);

    if (EQ(sub, "start")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalStart;
      return true;
    }
    if (EQ(sub, "finish")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalFinish;
      return true;
    }
    if (EQ(sub, "cancel")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalCancel;
      return true;
    }
    if (EQ(sub, "peak")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalPeak;
      return true;
    }
    if (EQ(sub, "save")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalSave;
      return true;
    }
    if (EQ(sub, "reset")) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalReset;
      return true;
    }
    if (EQ(sub, "weight")) {
      if (!oneFloat(p, out.f1)) return false;
      out.cmd = Cmd::CalWeight;
      return true;
    }
    if (EQ(sub, "bands")) {
      if (!twoFloats(p, out.f1, out.f2)) return false;
      out.cmd = Cmd::CalBands;
      return true;
    }
    return false;
  }

  if (EQ(verb, "config")) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Config;
    return true;
  }

  if (EQ(verb, "wifi")) {
    char sub[16];
    if (!readWord(p, sub, sizeof(sub))) return false;
    lower(sub);
    if (!EQ(sub, "pass")) return false;

    const char* pw = skipWs(p);
    size_t len = strlen(pw);
    while (len > 0 && isSpace(pw[len - 1])) len--;
    if (len < 8 || len > 63) return false;
    memcpy(out.text, pw, len);
    out.text[len] = '\0';
    out.cmd = Cmd::WifiPass;
    return true;
  }

  out.cmd = Cmd::Unknown;
  return false;
}

} // namespace cpr

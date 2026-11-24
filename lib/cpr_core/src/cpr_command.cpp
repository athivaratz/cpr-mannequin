#include "cpr_command.h"

#include <stdlib.h>
#include <string.h>

namespace cpr {

namespace {

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
  char* end = nullptr;
  double v = strtod(s, &end);
  if (end == s || *end != '\0') return false;
  if (!(v == v)) return false;
  out = (float)v;
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

  if (strcmp(verb, "tare") == 0) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Tare;
    return true;
  }

  if (strcmp(verb, "profile") == 0) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (strcmp(a, "adult") == 0) {
      out.cmd = Cmd::Profile;
      out.arg = (uint8_t)Profile::Adult;
      return true;
    }
    if (strcmp(a, "child") == 0) {
      out.cmd = Cmd::Profile;
      out.arg = (uint8_t)Profile::Child;
      return true;
    }
    return false;
  }

  if (strcmp(verb, "mode") == 0) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (strcmp(a, "free") == 0) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Free;
      return true;
    }
    if (strcmp(a, "cycle") == 0) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Cycle30x2;
      return true;
    }
    if (strcmp(a, "test") == 0) {
      out.cmd = Cmd::Mode;
      out.arg = (uint8_t)Mode::Test2Min;
      return true;
    }
    return false;
  }

  if (strcmp(verb, "start") == 0) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Start;
    return true;
  }

  if (strcmp(verb, "stop") == 0) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Stop;
    return true;
  }

  if (strcmp(verb, "metro") == 0) {
    char a[16];
    if (!readWord(p, a, sizeof(a)) || !noMore(p)) return false;
    lower(a);
    if (strcmp(a, "on") == 0) {
      out.cmd = Cmd::MetroOn;
      return true;
    }
    if (strcmp(a, "off") == 0) {
      out.cmd = Cmd::MetroOff;
      return true;
    }
    return false;
  }

  if (strcmp(verb, "cal") == 0) {
    char sub[16];
    if (!readWord(p, sub, sizeof(sub))) return false;
    lower(sub);

    if (strcmp(sub, "start") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalStart;
      return true;
    }
    if (strcmp(sub, "finish") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalFinish;
      return true;
    }
    if (strcmp(sub, "cancel") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalCancel;
      return true;
    }
    if (strcmp(sub, "peak") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalPeak;
      return true;
    }
    if (strcmp(sub, "save") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalSave;
      return true;
    }
    if (strcmp(sub, "reset") == 0) {
      if (!noMore(p)) return false;
      out.cmd = Cmd::CalReset;
      return true;
    }
    if (strcmp(sub, "weight") == 0) {
      if (!oneFloat(p, out.f1)) return false;
      out.cmd = Cmd::CalWeight;
      return true;
    }
    if (strcmp(sub, "bands") == 0) {
      if (!twoFloats(p, out.f1, out.f2)) return false;
      out.cmd = Cmd::CalBands;
      return true;
    }
    return false;
  }

  if (strcmp(verb, "config") == 0) {
    if (!noMore(p)) return false;
    out.cmd = Cmd::Config;
    return true;
  }

  if (strcmp(verb, "wifi") == 0) {
    char sub[16];
    if (!readWord(p, sub, sizeof(sub))) return false;
    lower(sub);
    if (strcmp(sub, "pass") != 0) return false;

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

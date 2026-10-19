#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <hyprland/src/debug/log/Logger.hpp>

#include "../config/config.hpp"

namespace hyprview::debug {

namespace {

// Rotated on SIZE, not at session start: truncating at load throws away the failed session's log.
constexpr long kMaxBytes = 8L * 1024 * 1024;
constexpr int  kKeep     = 3;

// Size is checked on a counter, not per line: ftell is a syscall and the frame log is per-frame.
constexpr int kCheckEvery = 2048;

FILE *openRotated() {
  for (int i = kKeep; i >= 1; --i) {
    const std::string from = i == 1 ? "/tmp/hyprview.log" : "/tmp/hyprview.log." + std::to_string(i - 1);
    if (::rename(from.c_str(), ("/tmp/hyprview.log." + std::to_string(i)).c_str()) != 0 && i == 1)
      return nullptr;
  }
  return fopen("/tmp/hyprview.log", "w");
}

FILE *openSink() {
  FILE *f = fopen("/tmp/hyprview.log", "a");
  if (!f)
    return nullptr;
  if (fseek(f, 0, SEEK_END) == 0 && ftell(f) > kMaxBytes) {
    fclose(f);
    f = openRotated();
  }
  return f;
}

// The trace and the frame log share this handle: one place to tail after a crash.
FILE *sink() {
  static FILE *f = nullptr;
  static int since = 0;
  if (!f) {
    f = openSink();
    since = 0;
    if (!f)
      return nullptr;
  }
  if (++since >= kCheckEvery) {
    since = 0;
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) > kMaxBytes) {
      fclose(f);
      f = openSink();
      if (!f)
        return nullptr;
    }
  }
  return f;
}

void emit(const std::string &line) {
  if (FILE *f = sink()) {
    fprintf(f, "%s\n", line.c_str());
    fflush(f); // a harness tails this while the session is still up
  }
}

std::string box(const LRect &r) {
  // Rounded to integers on purpose: sub-pixel noise makes every run a diff.
  return std::to_string(static_cast<int>(r.x)) + "," +
         std::to_string(static_cast<int>(r.y)) + "," +
         std::to_string(static_cast<int>(r.w)) + "," +
         std::to_string(static_cast<int>(r.h));
}

// Fields are values, not sentences: no spaces, no '|' -- a class or curve name violating that
// would silently shift every later field.
std::string tok(const char *s) {
  std::string out;
  if (!s)
    return out;
  for (const char *p = s; *p && out.size() < 40; ++p)
    out.push_back((*p == '|' || *p == ' ' || *p == '\n') ? '_' : *p);
  return out.empty() ? "?" : out;
}

int g_seq = 0;

} // namespace

bool enabled() { return cfg::debug.logs != 0; }

void dbg(const std::string &msg) {
  static int bootstrap = 200000;
  const int gate = cfg::debug.logs;
  if (gate != 0)
    bootstrap = -1;
  else if (bootstrap <= 0 || --bootstrap < 0)
    return;
  if (Log::logger)
    Log::logger->log(Log::INFO, "[hyprview] {}", msg);
  // The same handle emit() uses: two append handles on one path would interleave.
  static bool banner = false;
  FILE *f = sink();
  if (f) {
    if (!banner) {
      banner = true;
      fprintf(f, "\n=== SESSION %s ===\n",
              getenv("HYPRLAND_INSTANCE_SIGNATURE")
                  ? getenv("HYPRLAND_INSTANCE_SIGNATURE")
                  : "?");
      fflush(f);
    }
    fprintf(f, "[gate=%d] %s\n", gate, msg.c_str());
    fflush(f);
  }
}

namespace trace {

bool enabled() { return cfg::debug.trace != 0; }

bool on(int bit) { return selects(cfg::debug.trace, bit); }

namespace {
// The record family IS the tag: the same string selects the family and labels the record.
int famBit(const char *tag) {
  if (std::strcmp(tag, "DRAG") == 0) return fam::kDrag;
  if (std::strcmp(tag, "LIFE") == 0) return fam::kLife;
  if (std::strcmp(tag, "SCROLL") == 0) return fam::kScroll;
  if (std::strcmp(tag, "SWAP") == 0) return fam::kSwap;
  return fam::kAll; // an unrecognised tag is never suppressed: it may be the one worth seeing
}
} // namespace

int nextSeq() { return enabled() ? ++g_seq : 0; }

void animBegin(int seq, const char *op, const char *leaf, const char *style,
               double ms, const char *curve, const char *win, const char *surface,
               const LRect &from, const LRect &to) {
  if (!on(fam::kAnim) || seq == 0)
    return;
  emit("ANIM|begin|seq=" + std::to_string(seq) + "|op=" + tok(op) + "|leaf=" +
       tok(leaf) + "|style=" + tok(style) + "|ms=" +
       std::to_string(static_cast<int>(ms)) + "|curve=" + tok(curve) + "|win=" +
       tok(win) + "|surface=" + tok(surface) + "|from=" + box(from) + "|to=" +
       box(to));
}

void animEnd(int seq, double measuredMs) {
  if (!on(fam::kAnim) || seq == 0)
    return;
  emit("ANIM|end|seq=" + std::to_string(seq) + "|ms=" +
       std::to_string(static_cast<int>(measuredMs)));
}

void emitState(const std::string &dump) {
  if (!enabled())
    return;
  std::string cur;
  for (const char c : dump) {
    if (c == '\n') {
      emit(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  if (!cur.empty())
    emit(cur);
}

void animReject(const char *op, const char *why) {
  if (!on(fam::kAnim))
    return;
  emit("ANIM|reject|op=" + tok(op) + "|why=" + tok(why));
}

void record(const char *tag, const char *event, const std::string &state) {
  if (!on(famBit(tag)))
    return;
  emit(tok(tag) + "|" + tok(event) + "|" + state);
}

void ring(const char *kind, const LRect &b, int radiusPx, double alpha) {
  if (!on(fam::kAnim))
    return;
  std::string s = "RING|kind=" + tok(kind) + "|x=" + box(b) + "|r=" +
                  std::to_string(radiusPx) + "|a=" + std::to_string(alpha);
  emit(s);
}

void animStart(const char *op, const char *leaf, const char *style, double ms,
               const char *curve) {
  if (!on(fam::kAnim))
    return;
  emit("ANIM|begin|seq=" + std::to_string(nextSeq()) + "|op=" + tok(op) + "|leaf=" +
       tok(leaf) + "|style=" + tok(style) + "|ms=" + std::to_string(ms) + "|curve=" +
       tok(curve));
}

void dragBranch(const char *name, const std::string &extra) {
  if (!on(fam::kDrag))
    return;
  std::string s = "DRAG|branch|name=" + tok(name);
  if (!extra.empty())
    s += "|" + extra;
  emit(std::move(s));
}

void scroll(const char *event, const std::string &state) {
  record("SCROLL", event, state);
}

} // namespace trace

} // namespace hyprview::debug

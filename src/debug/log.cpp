#include "log.hpp"

#include <cstdio>
#include <string>

#include <hyprland/src/debug/log/Logger.hpp>

#include "../config/config.hpp"

namespace gloview::debug {

namespace {

// The trace and the frame log share the file handle: one place to tail, one
// place to look after a crash. A trace run therefore needs no second path.
FILE *sink() {
  static FILE *f = nullptr;
  if (!f)
    f = fopen("/tmp/gloview.log", "a");
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

// Fields are values, not sentences: no spaces, no '|'. A class name or curve
// name violating that would silently shift every later field, so they are
// sanitised here rather than trusted at the call site.
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
  // free lines: a dead config path logs nothing
  static int bootstrap = 200000;
  const int gate = cfg::debug.logs;
  if (gate != 0)
    bootstrap = -1;
  else if (bootstrap <= 0 || --bootstrap < 0)
    return;
  if (Log::logger)
    Log::logger->log(Log::INFO, "[gloview] {}", msg);
  static FILE *f = nullptr;
  if (!f) {
    f = fopen("/tmp/gloview.log", "a");
    if (f) {
      fprintf(f, "\n=== SESSION %s ===\n",
              getenv("HYPRLAND_INSTANCE_SIGNATURE")
                  ? getenv("HYPRLAND_INSTANCE_SIGNATURE")
                  : "?");
      fflush(f);
    }
  }
  if (f) {
    fprintf(f, "[gate=%d] %s\n", gate, msg.c_str());
    fflush(f);
  }
}

namespace trace {

bool enabled() { return cfg::debug.trace != 0; }

int nextSeq() { return enabled() ? ++g_seq : 0; }

void animBegin(int seq, const char *op, const char *leaf, const char *style,
               double ms, const char *curve, const char *win, const char *surface,
               const LRect &from, const LRect &to) {
  if (!enabled() || seq == 0)
    return;
  emit("ANIM|begin|seq=" + std::to_string(seq) + "|op=" + tok(op) + "|leaf=" +
       tok(leaf) + "|style=" + tok(style) + "|ms=" +
       std::to_string(static_cast<int>(ms)) + "|curve=" + tok(curve) + "|win=" +
       tok(win) + "|surface=" + tok(surface) + "|from=" + box(from) + "|to=" +
       box(to));
}

void animEnd(int seq, double measuredMs) {
  if (!enabled() || seq == 0)
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
  if (!enabled())
    return;
  emit("ANIM|reject|op=" + tok(op) + "|why=" + tok(why));
}

void record(const char *tag, const char *event, const std::string &state) {
  if (!enabled())
    return;
  emit(tok(tag) + "|" + tok(event) + "|" + state);
}

void dragBranch(const char *name, const std::string &extra) {
  if (!enabled())
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

} // namespace gloview::debug
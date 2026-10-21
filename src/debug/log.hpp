#pragma once

#include <string>

#include "../layout.hpp"

namespace hyprview::debug {

bool enabled();

void dbg(const std::string &msg);

namespace trace {

// `debug_trace` is a mask: 0 off, 1 everything, else a bit test; the control channel (CMD|,
// TUNING|, STATE|) is deliberately outside it.
namespace fam {
enum : int {
    kAnim   = 1,
    kDrag   = 2,
    kLife   = 4,
    kScroll = 8,
    kSwap   = 16,
};
inline constexpr int kAll = 0x7FFFFFFF;
}

// Reachable from a test: a mask selecting nothing looks like a trace never switched on.
[[nodiscard]] constexpr bool selects(int gate, int bit) {
  return gate != 0 && (gate == 1 || (gate & bit) != 0);
}

bool on(int bit);

bool enabled();

// Monotonic id, 0 when off. Stored on the animation so the begin is emitted once.
int nextSeq();

// `to` is the box really drawn, not the one asked for: from == to is a real outcome.
void animBegin(int seq, const char *op, const char *leaf, const char *style,
               double ms, const char *curve, const char *win, const char *surface,
               const LRect &from, const LRect &to);

void animEnd(int seq, double measuredMs);

void animReject(const char *op, const char *why);

// Multi-line on purpose: a harness diffs it, and a joined line would hide which tile changed.
void emitState(const std::string &dump);

// `state` is the subsystem's own describe(): a field added to the FSM cannot be missing here.
void record(const char *tag, const char *event, const std::string &state);

inline void drag(const char *event, const std::string &state) {
  record("DRAG", event, state);
}

// Emitted from the transitions: another route to the same state cannot forget it.
inline void life(const char *event, const std::string &state) {
  record("LIFE", event, state);
}

// Applied-but-unrecorded is indistinguishable from not run (scan_anim_coverage).
void animStart(const char *op, const char *leaf, const char *style, double ms,
               const char *curve);

void ring(const char *kind, const LRect &box, int radiusPx, double alpha);
void dragBranch(const char *name, const std::string &extra = {});

// Per EVENT, not per frame: advance() runs every tick while a glide is in flight.
void scroll(const char *event, const std::string &state);

}

} // namespace hyprview::debug

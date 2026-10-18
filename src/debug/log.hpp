#pragma once

#include <string>

#include "../layout.hpp"

namespace gloview::debug {

bool enabled();

void dbg(const std::string &msg);

// Machine-readable records, one per line, gated by `debug_trace` rather than `debug_logs`.
// key=value fields, no spaces, no '|'; `seq` pairs begin/end and is unique per process.
// Parseable by tools/anim_matrix.py, which is why the format must not drift.
namespace trace {

// `debug_trace` is a mask: 0 off, 1 everything, else a bit test. The control channel
// (CMD|, TUNING|, STATE|) is deliberately outside it -- it is the only proof tracing is on.
namespace fam {
enum : int {
    kAnim   = 1,
    kDrag   = 2,
    kLife   = 4,
    kScroll = 8,
    kSwap   = 16,
};
inline constexpr int kAll = 0x7FFFFFFF;
} // namespace fam

// Pulled out of log.cpp so a test can reach it: a mask that selects nothing looks exactly
// like a trace that was never switched on.
[[nodiscard]] constexpr bool selects(int gate, int bit) {
  return gate != 0 && (gate == 1 || (gate & bit) != 0);
}

// `debug_trace == 1` selects everything, so this doubles as "trace is on".
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

// One line per tile, multi-line on purpose: a harness diffs it, and a joined line would
// hide which tile changed.
void emitState(const std::string &dump);

// One record per subsystem transition; `state` is the subsystem's own describe(), so a field
// added to the FSM cannot be missing here. Grammar is ANIM|'s, so one parser reads all.
void record(const char *tag, const char *event, const std::string &state);

inline void drag(const char *event, const std::string &state) {
  record("DRAG", event, state);
}

// Emitted from the transitions themselves, so a caller reaching the same state by another
// route cannot forget it -- that was exactly this subsystem's failure.
inline void life(const char *event, const std::string &state) {
  record("LIFE", event, state);
}

// Which branch a release took: `name` first, `extra` in the same key=value grammar.
//
// Non-flight animations (entry, glide, master clock) get the same leading grammar as ANIM|
// without win/surface/boxes, which only a flight has. An animation applied but never
// recorded is indistinguishable from one that did not run -- scan_anim_coverage requires
// every m_clocks.*.begin() beside one of these or on an exemption list with a reason.
void animStart(const char *op, const char *leaf, const char *style, double ms,
               const char *curve);

// Which ring was drawn, where, how round, how opaque. Thirteen ring sites share a frame and
// two share a tile; `kind` is the site's own word, so the record names the code, not the look.
void ring(const char *kind, const LRect &box, int radiusPx, double alpha);

void dragBranch(const char *name, const std::string &extra = {});

// Per EVENT, not per frame: advance() runs every tick while a glide is in flight.
// setMax is where the position is pulled back into a range that just shrank.
void scroll(const char *event, const std::string &state);

} // namespace trace

} // namespace gloview::debug
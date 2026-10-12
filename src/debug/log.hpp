#pragma once

#include <string>

#include "../layout.hpp"

namespace gloview::debug {

bool enabled();

void dbg(const std::string &msg);

// Machine-readable animation records, one per line, gated by `debug_trace`
// instead of `debug_logs`.
//
// The frame log is for a human reading a burst; this is for a program deciding
// whether an animation happened and matched what was asked for. Every field is
// key=value, values contain no spaces and no '|', and lines are either
//
//   ANIM|begin|seq=<n>|op=<...>|leaf=<...>|style=<...>|ms=<n>|curve=<...>
//        |win=<class>|surface=<grid|strip|from=<x>,<y>,<w>,<h>|to=<...>
//   ANIM|end|seq=<n>|ms=<measured>
//
// or ANIM|reject|seq=<n>|why=<...> when a flight was requested but could not run.
// `seq` pairs the two ends and is unique per process. See tools/anim_matrix.py,
// which is the reason this format exists and the reason it must stay parseable.
namespace trace {

// `debug_trace` is a mask, not a flag: 0 is off, 1 is every family (so the old spelling
// still means what it meant), anything else selects. Debugging a drag release did not
// used to require also reading four hundred SCROLL records a second.
//
// The control channel -- CMD|, TUNING|, STATE| -- is deliberately NOT in the mask. It is
// how you find out whether a command did anything at all, so suppressing it would remove
// the only signal that tracing is on and the family you asked for.
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

// Which family a gate value selects. `1` predates the mask and means everything, so the
// old spelling keeps meaning what it meant; anything else is a bit test. Pulled out of
// log.cpp so a test can reach it -- a mask that selects nothing looks exactly like a trace
// that was never switched on.
[[nodiscard]] constexpr bool selects(int gate, int bit) {
  return gate != 0 && (gate == 1 || (gate & bit) != 0);
}

// True when the family is selected. `debug_trace == 1` selects everything, so this is
// also just "trace is on" for callers that want no filtering.
bool on(int bit);

bool enabled();

// A monotonically increasing id, 0 when tracing is off. Callers store it on the
// animation itself so the begin record is emitted exactly once, from the frame
// that actually resolved the geometry.
int nextSeq();

// `to` is the box that was really drawn -- not the one that was asked for. That
// distinction is the whole point: a flight whose target equals its origin
// resolves to from == to and is reported as such.
void animBegin(int seq, const char *op, const char *leaf, const char *style,
               double ms, const char *curve, const char *win, const char *surface,
               const LRect &from, const LRect &to);

void animEnd(int seq, double measuredMs);

void animReject(const char *op, const char *why);

// A multi-line scene dump, prefixed so it stays greppable alongside ANIM| records.
// Multi-line on purpose: one line per tile is what a harness can diff against what it
// asked for, and a single joined line would hide which tile changed.
void emitState(const std::string &dump);

// The drag FSM's transitions, one line per transition:
//
//   DRAG|press|press=tile|button=1|idx=3|lifted=0|x=..|y=..|travelSq=..
//   DRAG|release|press=stripwin|..|travelSq=576.000000
//
// `state` is model::Drag::describe(), so the field set is owned by the type rather than by
// this emitter -- a field added to the FSM cannot be missing from the record. Same
// key=value|pipe grammar as ANIM| for the same reason: one parser reads both.
//
// Emitted from the only file that mutates the FSM, so the record is complete by
// construction: a transition that did not happen cannot be logged, because there is no
// other way to reach one.
// One machine-readable record for a subsystem transition. `tag` is the record family
// (DRAG, LIFE), `event` is what happened (release, promote, deactivate), `state` is the
// subsystem's own describe(). The grammar is the ANIM| one, so one parser reads all three.
void record(const char *tag, const char *event, const std::string &state);

inline void drag(const char *event, const std::string &state) {
  record("DRAG", event, state);
}

// Screen ownership: park, promote, deactivate. The record is emitted from the transitions
// themselves, so it cannot be forgotten by a caller that reaches the same state by a
// different route -- which is exactly the failure this subsystem had.
inline void life(const char *event, const std::string &state) {
  record("LIFE", event, state);
}

// Which branch a release took. These were `[REL] branch=Tile->stripCard(rmb=0 kind=1)`
// under debug_logs -- a human log, off unless someone turned it on, and unparseable
// because the branch name sat inside prose. Now:
//
//   DRAG|branch|name=Tile->stripCard|rmb=0|kind=1
//
// `name` is first and carries the whole decision; `extra` is the branch's own fields, in
// the same key=value grammar. A harness can count branches without turning on the frame
// log at all.
// An animation that is applied but never recorded cannot be told apart from one that did
// not run at all -- both are silent. This is the non-flight form: the entry, the glide,
// the master clock. Same leading grammar as the swap record, without win/surface/boxes,
// which only a flight has.
//
//   ANIM|begin|seq=<n>|op=<open|close|entry|glide|card|...>|leaf=<...>|style=<...>
//        |ms=<n>|curve=<...>
//   ANIM|end|seq=<n>|ms=<measured>
//
// scan_anim_coverage requires every m_clocks.*.begin() to sit next to one of these, or to
// be on an exemption list carrying a reason. Before it, beginSwapFX was the only emitter,
// so an entry that never started wrote nothing at all -- indistinguishable from one that
// started with the wrong style.
void animStart(const char *op, const char *leaf, const char *style, double ms,
               const char *curve);

// Which ring was drawn, where, how round and how opaque.
//
// "A frame is doing something odd" was answerable three different ways in one session and
// wrong twice: the render has thirteen ring sites, they are not the same colour, two of
// them are drawn on the same tile on the same frame, and two of them change radius as
// the box animates while the content's radius does not. Without a record the only way to
// tell them apart is to read the source and guess, and a ring that is not the one you
// guessed produces a confident wrong fix.
//
//   RING|kind=tile_focus|x=..|y=..|w=..|h=..|r=..|a=..
//
// `kind` is the site's own word, so the record names the code that drew it rather than a
// description of what it looked like.
void ring(const char *kind, const LRect &box, int radiusPx, double alpha);

void dragBranch(const char *name, const std::string &extra = {});

// Strip scroll events. Per EVENT, not per frame: advance() runs every tick while a glide
// is in flight, and a record per frame is the log spam this format exists to avoid.
//
//   SCROLL|setMax|scrollable=1|max=400.0|...   the strip was rebuilt
//   SCROLL|glide|from=..|target=..|...       endpoints set
//   SCROLL|snap|...                          a move too small to animate, or the rebuild
//
// setMax is the interesting one: it is where the current position is pulled back into a
// range that just got smaller, which is the invariant four free doubles did not hold.
void scroll(const char *event, const std::string &state);

} // namespace trace

} // namespace gloview::debug
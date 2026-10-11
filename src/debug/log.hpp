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
void drag(const char *event, const std::string &state);

// Which branch a release took. These were `[REL] branch=Tile->stripCard(rmb=0 kind=1)`
// under debug_logs -- a human log, off unless someone turned it on, and unparseable
// because the branch name sat inside prose. Now:
//
//   DRAG|branch|name=Tile->stripCard|rmb=0|kind=1
//
// `name` is first and carries the whole decision; `extra` is the branch's own fields, in
// the same key=value grammar. A harness can count branches without turning on the frame
// log at all.
void dragBranch(const char *name, const std::string &extra = {});

} // namespace trace

} // namespace gloview::debug
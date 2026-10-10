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

} // namespace trace

} // namespace gloview::debug
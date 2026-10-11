#pragma once

// The strip's scroll position, as four doubles that were allowed to disagree.
//
// m_stripScrollMax (how far the card group can move), m_stripScrollFrom and
// m_stripScrollTarget (the glide's endpoints) and m_stripScroll (where it is right now).
// Three files wrote them.
//
// The invariant is that the two ENDPOINTS live in [0, max], and nothing held the CURRENT
// value: animateStripTo() clamped its two arguments, but nothing re-clamped where the strip
// actually is. It survived a strip that got shorter only because build.cpp happened to
// write `m_stripScroll = 0.0` on the line before calling animateStripTo() -- and that line
// is immediately overwritten by the clock tick, so it protects nothing, it just happens to
// run first.
//
// setMax() re-clamps, so that ordering stops mattering.
//
// current is NOT clamped during a glide, on purpose. curves.cpp bounds the bezier solver's
// t to [0,1] but not the value it returns, so a cubic with a control point outside [0,1]
// eases past 1. That overshoot was reachable and visible before this type existed; clamping
// it here would remove an effect a config can ask for. The invariant covers the endpoints
// and the re-anchor, not the transit.
//
// The second thing the type absorbs: animateStripTo() took a `from` that meant different
// things at its two call sites -- the wheel passed the current position, the rebuild passed
// the previous TARGET -- and neither read mattered, because a running glide overwrites it
// with the current value anyway and a settled one has current == target. It stays a
// parameter here rather than being guessed, so neither call site changes.

#include <algorithm>
#include <string>

namespace gloview::model {

class ScrollState {
public:
  // Called when the strip is rebuilt. Shrinking the range re-clamps the endpoints AND the
  // current position, which is the part that used to need the caller to remember.
  void setMax(double max) {
    m_max     = std::max(0.0, max);
    m_from    = clamp(m_from);
    m_target  = clamp(m_target);
    m_current = clamp(m_current);
  }

  [[nodiscard]] double max() const { return m_max; }
  [[nodiscard]] double current() const { return m_current; }
  [[nodiscard]] double target() const { return m_target; }
  [[nodiscard]] double from() const { return m_from; }
  // Nothing to scroll: the cards fit the band. The wheel handler swallows input on this,
  // so it asks before deciding what the notches mean.
  [[nodiscard]] bool scrollable() const { return m_max > 0.0; }

  // Set the endpoints of a glide. Returns whether anything actually moved, so the caller
  // can snap instead of arming a clock for a 0.4px change -- the same 0.5 the old code
  // compared, kept here because the threshold is part of "is this worth animating".
  bool beginGlide(double from, double to) {
    const double f = clamp(from), t = clamp(to);
    if (std::abs(t - f) < 0.5)
      return false;
    m_from   = f;
    m_target = t;
    return true;
  }

  // No glide worth running: everything lands on one value.
  void snap(double v) {
    m_from = m_target = m_current = clamp(v);
  }

  // One frame of the glide. `eased` is the curve's output, 0..1.
  void advance(double eased) { m_current = m_from + (m_target - m_from) * eased; }

  // Endpoints and current in one write, for the rebuild path that re-anchors the strip and
  // has no glide to preserve.
  void reseat(double position) { snap(position); }

  [[nodiscard]] std::string describe() const {
    return "scrollable=" + std::string(scrollable() ? "1" : "0") +
           "|max=" + std::to_string(m_max) + "|from=" + std::to_string(m_from) +
           "|target=" + std::to_string(m_target) + "|current=" + std::to_string(m_current);
  }

private:
  [[nodiscard]] double clamp(double v) const { return std::clamp(v, 0.0, m_max); }

  double m_max = 0.0;
  double m_from = 0.0, m_target = 0.0, m_current = 0.0;
};

} // namespace gloview::model
#pragma once

// The strip's scroll position, as four doubles that were allowed to disagree. The invariant is
// that the ENDPOINTS live in [0, max] and nothing held the current value; setMax() re-clamps
// all three, so the ordering that used to survive by accident (build.cpp zeroing the current
// position a line before a call the clock tick then overwrote) stops mattering.
//
// current is NOT clamped during a glide, on purpose: curves.cpp bounds the bezier solver's t
// to [0,1] but not its output, so a cubic with a control point outside eases past 1. That
// overshoot is reachable and was visible before this type existed, and clamping it here would
// remove an effect a config can ask for. The invariant covers the endpoints and the re-anchor,
// not the transit.

#include <algorithm>
#include <string>

namespace gloview::model {

class ScrollState {
public:
  // On rebuild. Shrinking the range re-clamps the endpoints AND the current position.
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
  // Nothing to scroll: the cards fit the band. The wheel swallows input on this.
  [[nodiscard]] bool scrollable() const { return m_max > 0.0; }

  // Returns whether anything moved, so the caller snaps instead of arming a clock for a
  // 0.4px change. The 0.5 threshold is part of "is this worth animating".
  bool beginGlide(double from, double to) {
    const double f = clamp(from), t = clamp(to);
    if (std::abs(t - f) < 0.5)
      return false;
    m_from   = f;
    m_target = t;
    return true;
  }

  // No glide worth running.
  void snap(double v) {
    m_from = m_target = m_current = clamp(v);
  }

  // One frame of the glide. `eased` is the curve's output, 0..1.
  void advance(double eased) { m_current = m_from + (m_target - m_from) * eased; }

  // Endpoints and current in one write, for a rebuild that has no glide to preserve.
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
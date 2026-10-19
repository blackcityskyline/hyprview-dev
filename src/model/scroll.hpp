#pragma once

// Invariant: the ENDPOINTS live in [0, max] and setMax() re-clamps all of them. current is NOT
// clamped during a glide, on purpose: a bezier control point outside eases past 1, and that
// overshoot is reachable -- clamping it here would remove an effect a config can ask for.

#include <algorithm>
#include <string>

namespace hyprview::model {

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
  [[nodiscard]] bool scrollable() const { return m_max > 0.0; }

  // Returns whether anything moved; the 0.5px threshold is part of "worth animating", so the
  // caller snaps instead of arming a clock for a sub-pixel change.
  bool beginGlide(double from, double to) {
    const double f = clamp(from), t = clamp(to);
    if (std::abs(t - f) < 0.5)
      return false;
    m_from   = f;
    m_target = t;
    return true;
  }

  void snap(double v) {
    m_from = m_target = m_current = clamp(v);
  }

  void advance(double eased) { m_current = m_from + (m_target - m_from) * eased; }

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

} // namespace hyprview::model

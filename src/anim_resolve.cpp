// The animation precedence chain, in its own TU: anim.cpp's Lua/Config deps keep it out of
// tests, so nothing here may include hyprland/ -- a link error in tests/anim_resolve_check.cpp.

#include "anim_types.hpp"

#include <algorithm>

namespace gloview::anim {

Cfg resolve(const Inputs &in, int baseMs, const char *curveDefault) {
  Cfg c{};
  double speed = in.speed;
  int absMs = in.hasMs ? in.ms : -1;

  c.on = in.globalEnabled && in.enabled;
  if (in.rule) {
    if (in.rule->enabled) c.on = *in.rule->enabled;
    if (in.rule->speed)   speed = *in.rule->speed;
    if (in.rule->ms)      absMs = *in.rule->ms;
  }
  if (!c.on) {
    // 1ms, not 0: the clock still has to tick, it just must not move anything.
    c.ms = 1.0;
    return c;
  }
  // _ms is the base duration -- it defaults to the table's base -- and _speed still scales
  // it. A Lua rule's ms overrides both outright.
  const double base = absMs >= 0 ? static_cast<double>(absMs) : static_cast<double>(baseMs);
  c.ms = std::max(16.0, base / std::clamp(speed, 0.01, 20.0));
  if (in.rule && in.rule->curve)
    c.curve = *in.rule->curve;
  else if (!in.curve.empty())
    c.curve = in.curve;
  else if (curveDefault)
    c.curve = curveDefault;
  return c;
}

double stagger(double base, int i, int n, double spread) {
  if (n <= 1)
    return base;
  const double s = spread < 0.0 ? 0.0 : (spread > 1.0 ? 1.0 : spread);
  const double start = s * (static_cast<double>(i) / (n - 1));
  // 1 - s is floored away from zero: with s == 1 the staircase would divide by zero and the
  // last tile would resolve to a NaN box the compositor draws.
  const double denom = 1.0 - s > 0.001 ? 1.0 - s : 0.001;
  return std::clamp((base - start) / denom, 0.0, 1.0);
}

double resolveParam(bool hasValue, double value, double fallback) {
  return hasValue ? value : fallback;
}

} // namespace gloview::anim
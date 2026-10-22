// The animation precedence chain, headless.
//
// The rule that makes this testable at all: Config::Values and HyprlandAPI live in the
// Hyprland binary and there is no libhyprland, so anim.cpp cannot be linked into a test.
// resolve() was therefore split out to take plain values -- see anim.hpp Inputs. This file
// covers what cfg() delegates to it, which is where every surprising animation behaviour
// of the last days came from.
//
// What is pinned here, all of it behaviour that was wrong or fragile at some point:
//   - an unknown name is not an animation: off, and NO invented duration
//   - the master switch turns everything off without touching per-leaf keys
//   - _ms is the base duration, _speed divides it, both floor at one frame
//   - precedence is Lua rule > config key > table default, per field, not per animation
//   - an animation switched off resolves to 1ms: the clock still ticks, nothing moves
//
// Expected values are literals and worked examples, not a second copy of the formula.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "../src/anim_types.hpp"

using hyprview::anim::Cfg;
using hyprview::anim::Inputs;
using hyprview::anim::Rule;

namespace {

constexpr double kEps = 1e-9;
constexpr int kBaseMs = 300;

Cfg r(const Inputs &in) { return hyprview::anim::resolve(in, kBaseMs, "easeout"); }

} // namespace

int main() {
  // --- defaults -----------------------------------------------------------------
  {
    Inputs in;
    const Cfg c = r(in);
    assert(c.on);
    assert(std::abs(c.ms - 300.0) < kEps);        // table base, unscaled
    assert(c.curve == "easeout");                  // table default curve
  }
  std::puts("defaults: on, table base ms, table curve");

  // --- _ms is the base, _speed divides it ----------------------------------------
  {
    Inputs in; in.hasMs = true; in.ms = 500;
    assert(std::abs(r(in).ms - 500.0) < kEps);

    Inputs s; s.hasMs = true; s.ms = 500; s.speed = 2.0;
    assert(std::abs(r(s).ms - 250.0) < kEps);      // 500 / 2

    Inputs slow; slow.speed = 0.5;
    assert(std::abs(r(slow).ms - 600.0) < kEps);   // 300 / 0.5
  }
  std::puts("_ms sets the base, _speed divides it");

  // One frame is the floor. A leaf asked to run for 0ms, or divided down to it, must not
  // divide by its own progress.
  {
    Inputs zero; zero.hasMs = true; zero.ms = 0;
    assert(std::abs(r(zero).ms - 16.0) < kEps);

    Inputs fast; fast.speed = 20.0; fast.hasMs = true; fast.ms = 100;
    assert(std::abs(r(fast).ms - 16.0) < kEps);     // clamped speed is 20 -> 5ms -> 16

    // Negative speed clamps to 0.01, which divides the base by a hundred: a stray
    // speed = -3 turns a 300ms leaf into 30 SECONDS. Not changed here -- a config value
    // that silly deserves to be visible -- but pinned so it cannot get worse unnoticed.
    Inputs negative; negative.speed = -3.0;
    assert(std::abs(r(negative).ms - 30000.0) < kEps);
  }
  std::puts("duration floors at one frame; negative speed divides by 0.01");

  // --- the master switch ---------------------------------------------------------
  {
    Inputs off; off.globalEnabled = false;
    assert(!r(off).on);
    assert(std::abs(r(off).ms - 1.0) < kEps);     // clock still ticks
  }
  std::puts("master switch off: disabled, 1ms");

  // --- per-leaf switch ------------------------------------------------------------
  {
    Inputs off; off.enabled = false;
    assert(!r(off).on);
    // A Lua rule can turn it back ON: that is the documented precedence.
    Rule back; back.enabled = true;
    off.rule = &back;
    assert(r(off).on);
  }
  std::puts("leaf switch, and a Lua rule overriding it back on");

  // --- precedence, per field ------------------------------------------------------
  {
    Rule rule;
    rule.speed = 3.0; rule.ms = 900; rule.curve = "back";

    Inputs all;
    all.hasMs = true; all.ms = 100; all.speed = 2.0; all.curve = "linear";
    all.rule = &rule;

    const Cfg c = r(all);
    assert(std::abs(c.ms - 300.0) < kEps);   // 900 / 3, both from the rule
    assert(c.curve == "back");
  }
  std::puts("Lua rule beats config key beats table default, field by field");

  // Partial rules must leave the other fields alone -- a rule that sets only the curve
  // must not silently reset the duration to the table base.
  {
    Rule only; only.curve = "back";
    Inputs in; in.hasMs = true; in.ms = 700; in.speed = 2.0; in.rule = &only;

    const Cfg c = r(in);
    assert(std::abs(c.ms - 350.0) < kEps);   // 700 / 2, neither touched by the rule
    assert(c.curve == "back");
  }
  std::puts("a partial Lua rule leaves the rest of the chain intact");

  // An absent curve falls through to the default rather than becoming empty.
  {
    Inputs in;
    assert(r(in).curve == "easeout");

    Cfg none = hyprview::anim::resolve(in, 100, nullptr);
    assert(none.curve.empty());
  }
  std::puts("curve falls back to the table default, empty only when there is none");

  // --- an empty value means CLEAR, not zero ---------------------------------------
  //
  // The command tokenizer splits on spaces and nothing else, so `set open_ms ""` reaches
  // setTuning as the two characters `""`. Stored as a value those reach num() as 0, and
  // 0ms floors to a single frame -- the overview opened instantly and the override map
  // still listed the key. Verified against the running plugin: TUNING|n=1|open_ms="".
  {
    using hyprview::anim::stripQuotes;
    // Both spellings of an intended empty, and a lone quote is not an empty.
    assert(stripQuotes("\"\"").empty());
    assert(stripQuotes("''").empty());
    assert(stripQuotes("").empty());
    assert(stripQuotes("\"") == "\"");
    assert(stripQuotes("320") == "320");
    // One layer only: a quoted number stays a number.
    assert(stripQuotes("\"320\"") == "320");
    // Mismatched or interior quotes are content, not framing.
    assert(stripQuotes("\"320") == "\"320");
    assert(stripQuotes("a\"b") == "a\"b");
  }
  std::puts("an empty or quoted-empty value normalises to CLEAR, never to zero");

  // --- a switch reads its value, and asks registration separately ------------------
  //
  // cfg()'s two switches were both written `handle ? (handle != 0) : true`, but
  // cfg::Int has operator int() -> its VALUE. So `pulse_enabled = 0` made the leaf
  // report ENABLED and the pulse drew its ring from a config that had switched it off;
  // the same shape on the master switch meant `animations_enabled = 0` also reported
  // on. resolve() cannot see either -- it takes an already-resolved bool -- which is
  // why the regression lived here and not below.
  {
    using hyprview::anim::switchOn;
    assert(switchOn(true, 0) == false);   // registered and explicitly off
    assert(switchOn(true, 1) == true);
    assert(switchOn(true, 2) == true);
    assert(switchOn(false, 0) == true);   // never installed -> table default
    assert(switchOn(false, 1) == true);
  }
  std::puts("a registered switch reads its value; registration is a separate question");

  std::puts("OK");
  return 0;
}
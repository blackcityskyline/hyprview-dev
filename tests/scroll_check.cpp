// The strip scroll invariant that no field held.
//
// Four doubles -- max, from, target, current -- written from three files, with all three
// positions supposed to live in [0, max]. animateStripTo() clamped its two arguments, so
// the endpoints were safe. The CURRENT value had no guard at all: a strip that got shorter
// mid-glide was protected only by build.cpp writing `m_stripScroll = 0.0` on the line
// before the call, and that write is overwritten by the very next clock tick, so it
// protects nothing -- it merely runs first.
//
// setMax() re-clamps, so the ordering stops being load-bearing. That is the whole point of
// the type; the arithmetic around it is otherwise unchanged.

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/model/scroll.hpp"

using hyprview::model::ScrollState;

int main() {
  // --- a fresh strip cannot scroll ---------------------------------------------------
  {
    ScrollState s;
    assert(!s.scrollable());
    assert(s.max() == 0.0 && s.from() == 0.0 && s.target() == 0.0 && s.current() == 0.0);
    std::puts("a fresh scroll is zero-length and not scrollable");
  }

  // --- everything lands inside the range ----------------------------------------------
  {
    ScrollState s;
    s.setMax(400.0);
    assert(s.scrollable() && s.max() == 400.0);
    // Negative and overshooting endpoints are clamped, not accepted.
    assert(s.beginGlide(-50.0, 900.0));
    assert(s.from() == 0.0);
    assert(s.target() == 400.0);
    s.snap(-10.0);
    assert(s.current() == 0.0);
    s.snap(1e6);
    assert(s.current() == 400.0);
    std::puts("endpoints and current are clamped into [0, max]");
  }

  // --- THE invariant: shrinking the strip pulls the current value back in -------------
  // This is what the old code had no guard for. Mid-glide at 380 of 400, then the strip
  // loses a card and the range drops to 120: the position must come back with it, or the
  // group renders outside its own band.
  {
    ScrollState s;
    s.setMax(400.0);
    assert(s.beginGlide(380.0, 390.0));
    s.advance(0.5); // halfway
    assert(s.current() == 385.0);

    s.setMax(120.0);
    assert(s.current() <= 120.0);
    assert(s.target() <= 120.0);
    assert(s.from() <= 120.0);
    // ...and continuing the glide stays inside, rather than lerping back out past the end.
    s.advance(1.0);
    assert(s.current() <= 120.0);
    std::puts("setMax pulls current, from and target back into the new range");
  }

  // --- a max of zero is not scrollable, and swallows everything -------------------------
  {
    ScrollState s;
    s.setMax(300.0);
    assert(s.beginGlide(0.0, 300.0));
    s.advance(1.0);
    assert(s.current() == 300.0);
    s.setMax(0.0);
    assert(!s.scrollable());
    assert(s.current() == 0.0 && s.target() == 0.0 && s.from() == 0.0);
    // A negative max is not a range; it is zero with a sign, and must not produce a
    // negative bound that std::clamp would then use as a lower limit.
    s.setMax(-500.0);
    assert(s.max() == 0.0 && !s.scrollable());
    s.snap(-999.0);
    assert(s.current() == 0.0);
    std::puts("a strip that stops being scrollable lands at zero, not at a negative");
  }

  // --- a glide that is not worth running -----------------------------------------------
  // 0.5px is the threshold animateStripTo used. Keeping it means the caller's decision to
  // snap instead of arming a clock is unchanged.
  {
    ScrollState s;
    s.setMax(400.0);
    assert(!s.beginGlide(100.0, 100.4));
    assert(!s.beginGlide(100.0, 100.0));
    assert(s.beginGlide(100.0, 101.0));
    std::puts("a sub-pixel glide is refused, so no clock is armed for nothing");
  }

  // --- advance interpolates, and does NOT clamp an overshooting curve -----------------
  // Measured, not assumed: curves.cpp clamps the bezier SOLVER's t to [0,1] but not the
  // value it returns, so a cubic with a control point outside [0,1] produces eased > 1.
  // That is a real, reachable effect and it was unclamped before this type existed --
  // clamping it here would silently remove the overshoot easing a config can ask for.
  // So advance() stays a plain lerp, and the invariant is scoped to the endpoints.
  {
    ScrollState s;
    s.setMax(400.0);
    assert(s.beginGlide(100.0, 300.0));
    s.advance(0.0);
    assert(s.current() == 100.0);
    s.advance(0.5);
    assert(s.current() == 200.0);
    s.advance(1.0);
    assert(s.current() == 300.0);

    ScrollState o;
    o.setMax(400.0);
    assert(o.beginGlide(0.0, 400.0));
    o.advance(1.4); // an overshooting curve
    assert(o.current() > 400.0); // deliberately NOT clamped

    // ...but the next re-anchor still brings it back, which is the case the old code had
    // no guard for and the reason setMax exists.
    o.setMax(400.0);
    assert(o.current() <= 400.0);
    std::puts("advance lerps; an overshooting curve is preserved; setMax re-clamps");
  }

  // --- reseat drops any glide in flight -------------------------------------------------
  {
    ScrollState s;
    s.setMax(400.0);
    assert(s.beginGlide(0.0, 400.0));
    s.advance(0.5);
    assert(s.current() == 200.0);
    s.reseat(40.0);
    assert(s.current() == 40.0 && s.from() == 40.0 && s.target() == 40.0);
    std::puts("reseat lands all three on one value");
  }

  // --- describe is a data line, ANIM| grammar ---------------------------------------------
  {
    ScrollState s;
    assert(s.describe() ==
           "scrollable=0|max=0.000000|from=0.000000|target=0.000000|current=0.000000");
    s.setMax(400.0);
    const std::string line = s.describe();
    assert(line.rfind("scrollable=1|", 0) == 0);
    size_t start = 0;
    for (;;) {
      const size_t bar = line.find('|', start);
      const std::string field =
          line.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
      const size_t eq = field.find('=');
      assert(eq != std::string::npos && eq > 0 && field.find('=') == eq);
      assert(field.find(' ') == std::string::npos);
      if (bar == std::string::npos)
        break;
      start = bar + 1;
    }
    std::printf("describe: %s\n", line.c_str());
    std::puts("describe is one pipe-separated key=value line");
  }

  // --- GridScroll: the window and the range settle together, and follow reads the area --
  // The desktop grid's wheel and arrows read follow() through ONE field, so a rebuild that
  // sets the rows' range without the area (or the reverse) cannot happen: setWindow takes
  // both. follow() returns the area-based want; the RANGE clamp belongs to the glide
  // (beginGlide), so the arrows adjust from where the grid already means to be.
  {
    using hyprview::model::GridScroll;
    using hyprview::LRect;
    GridScroll g;
    g.setWindow(LRect{0.0, 0.0, 100.0, 100.0}, 50.0);
    assert(g.max() == 50.0);
    // A tile below the window pulls the offset to its bottom edge (170-100): follow does
    // NOT clamp by the range itself.
    g.snap(0.0);
    assert(g.follow(LRect{0.0, 150.0, 10.0, 20.0}) == 70.0);
    // ...and the glide takes it the rest of the way inside the range.
    assert(g.beginGlide(10.0, g.follow(LRect{0.0, 150.0, 10.0, 20.0})));
    assert(g.target() == 50.0);
    // A tile pushed above the window top comes back to the top edge.
    g.snap(50.0);
    assert(g.follow(LRect{0.0, 30.0, 10.0, 20.0}) == 30.0);
    // A visible tile leaves the target where it is.
    assert(g.follow(LRect{0.0, 60.0, 10.0, 20.0}) == 50.0);
    std::puts("GridScroll settles window+range together; follow reads the window");
  }

  std::puts("OK");
  return 0;
}
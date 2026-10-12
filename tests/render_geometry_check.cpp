// The render geometry that computes boxes and draws nothing.
//
// These three used to live inside Overview methods, where the box arithmetic was
// interleaved with m_drag, m_monitor and three cfg lookups -- so a card that is a
// browser tab instead of a window, or a strip that moves, would have had to be
// special-cased inside each of them. Pulled out as arguments instead.
//
// What is actually asserted here is the arithmetic that looks wrong on inspection and
// is right on purpose:
//
//   - killZoneGlow is a ramp, so it must not be a bool. A zone blank until the cursor
//     crosses it says nothing while the user is still aiming it.
//   - dragVisualBox scales and rises at progress 0 as well. A stationary hold -- card
//     held down without moving, fromBox == to -- has fromBox == to at every progress,
//     so gating the lift on movement froze the hold. That case is the one that broke
//     and it is the reason this file exists.
//   - closeButtonRect clamps its radius at both ends: an 18px cap on a large card, and a
//     0.3 floor on the size multiplier so a configured 0 cannot make it unclickable.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/render/geometry.hpp"

using gloview::LRect;
using gloview::render::captionBody;
using gloview::render::captionSprout;
using gloview::render::captionSproutRound;
using gloview::render::closeButtonRect;
using gloview::render::Corner;
using gloview::render::dragVisualBox;
using gloview::render::killZoneGlow;

namespace {

constexpr double kEps = 1e-9;

void near(double got, double want, const char* what) {
  if (std::fabs(got - want) >= kEps) {
    std::fprintf(stderr, "  %s: got %.17g, want %.17g\n", what, got, want);
  }
  assert(std::fabs(got - want) < kEps);
}

void sameRect(const LRect& got, const LRect& want, const char* what) {
  if (std::fabs(got.x - want.x) >= kEps || std::fabs(got.y - want.y) >= kEps ||
      std::fabs(got.w - want.w) >= kEps || std::fabs(got.h - want.h) >= kEps) {
    std::fprintf(stderr, "  %s: got {%.17g %.17g %.17g %.17g}, want {%.17g %.17g %.17g %.17g}\n",
                 what, got.x, got.y, got.w, got.h, want.x, want.y, want.w, want.h);
  }
  assert(std::fabs(got.x - want.x) < kEps && std::fabs(got.y - want.y) < kEps &&
         std::fabs(got.w - want.w) < kEps && std::fabs(got.h - want.h) < kEps);
}

} // namespace

int main() {
  // --- the kill zone is a distance, not a bool ----------------------------------------
  {
    const LRect zone{0.0, 900.0, 1920.0, 120.0}; // bottom band, as on a 1920x1080 monitor
    const double falloff = 150.0;

    near(killZoneGlow(zone, 960.0, 950.0, falloff), 1.0, "inside the zone is full brightness");
    near(killZoneGlow(zone, 960.0, 750.0, falloff), 0.0, "one falloff away is nothing");
    // Halfway is the symmetric point of smoothstep, and it is exact rather than
    // approximate -- so this pins the curve, not just its shape.
    near(killZoneGlow(zone, 960.0, 825.0, falloff), 0.5, "half a falloff is exactly half");

    // Brightness never rises again past the edge, and never exceeds full inside.
    assert(killZoneGlow(zone, 960.0, 700.0, falloff) == 0.0);
    assert(killZoneGlow(zone, 960.0, 1000.0, falloff) > killZoneGlow(zone, 960.0, 899.0, falloff));

    // A collapsed zone is the no-monitor / kill_zone disabled case. It must answer 0,
    // not divide by a zero width.
    near(killZoneGlow(LRect{0, 0, 0, 0}, 100.0, 100.0, falloff), 0.0, "collapsed zone");
    near(killZoneGlow(LRect{0, 0, 1920.0, 0}, 100.0, 100.0, falloff), 0.0, "zero-height zone");

    // falloff is an argument now, so the caller can hand over a config of 0. Without the
    // guard that is 0/0 at the zone centre: a NaN reaching the alpha multiply.
    near(killZoneGlow(zone, 960.0, 950.0, 0.0), 0.0, "no falloff is no glow, not a NaN");
    assert(!std::isnan(killZoneGlow(zone, 960.0, 950.0, -5.0)));
    std::puts("killZoneGlow: a ramp, 0 outside, no NaN on a degenerate falloff");
  }

  // --- a drag starts where it was picked up and ends where it was dropped ---------------
  {
    const LRect from{100.0, 200.0, 400.0, 300.0};
    const LRect to{700.0, 500.0, 500.0, 350.0};

    // progress 0, lifted: exactly where it was picked up, unscaled and unrisen.
    sameRect(dragVisualBox(from, to, true, 0.0, 0.0, 0.0), from, "progress 0 is the origin");
    // progress 1 lands on the target whatever the lift params are.
    sameRect(dragVisualBox(from, to, true, 1.0, 0.0, 0.0), to, "progress 1 is the target");
    // Not lifted: no glide at all, the card is simply wherever it is being held.
    sameRect(dragVisualBox(from, to, false, 0.5, 0.0, 0.0), to, "an unlifted drag does not glide");

    // A rise moves the box vertically and never sideways; a scale keeps the centre put.
    const LRect base = dragVisualBox(from, to, false, 1.0, 0.0, 0.0);
    const LRect rose = dragVisualBox(from, to, false, 1.0, 0.0, 40.0);
    near(rose.x, base.x, "a rise moves nothing horizontally");
    near(rose.y, base.y - 40.0, "a rise lifts by exactly liftRise");
    near(rose.w, base.w, "a rise changes no size");

    const LRect big = dragVisualBox(from, to, false, 1.0, 0.5, 0.0);
    near(big.cx(), base.cx(), "a scale keeps the centre");
    near(big.cy(), base.cy(), "a scale keeps the centre");
    near(big.w, base.w * 1.5, "a scale grows the width");
    near(big.h, base.h * 1.5, "a scale grows the height");
    std::puts("dragVisualBox: glides to the target, lifts vertically, scales about the centre");
  }

  // --- the stationary hold still animates ------------------------------------------------
  //
  // The case this refactor exists for. fromBox == to at every progress, so any lift
  // gated on "did the card move" is zero here and the hold shows nothing.
  {
    const LRect box{400.0, 300.0, 300.0, 220.0};

    // Progress 0 is the start of the animation, so the lift is legitimately zero there --
    // it ramps. What must not happen is the box staying at `box` for the WHOLE hold.
    sameRect(dragVisualBox(box, box, true, 0.0, 0.10, 20.0), box, "a hold starts at its own box");

    const LRect held = dragVisualBox(box, box, true, 1.0, 0.10, 20.0);
    near(held.w, box.w * 1.10, "a full hold is 10% larger: 1 + liftScale");
    near(held.y, box.cy() - box.h * 1.10 / 2.0 - 20.0, "a full hold is risen by liftRise");

    // Halfway through a hold it is genuinely between the two -- this is what a
    // movement-gated lift would collapse to `box`.
    const LRect half = dragVisualBox(box, box, true, 0.5, 0.10, 20.0);
    near(half.w, box.w * 1.05, "halfway through a hold is halfway lifted");
    assert(half.w > box.w && half.w < held.w);

    // And it rises monotonically, so it never dips back.
    assert(half.y < box.y && half.y > held.y);
    std::puts("a stationary hold ramps the lift -- the case that used to freeze");
  }

  // --- the close glyph is one square, in the corner that was asked for -------------------
  {
    const LRect card{100.0, 200.0, 800.0, 600.0};

    const LRect tr = closeButtonRect(card, 1.0, Corner::TopRight);
    const LRect tl = closeButtonRect(card, 1.0, Corner::TopLeft);
    const LRect br = closeButtonRect(card, 1.0, Corner::BottomRight);
    const LRect bl = closeButtonRect(card, 1.0, Corner::BottomLeft);

    near(tr.w, tr.h, "the glyph is square");
    // A card this size hits the 18px radius cap: min(800,600)*0.11 = 66, clamped to 18.
    near(tr.w, 36.0, "the radius is capped on a large card");

    assert(tr.x > tl.x);   // right is left of left
    assert(br.x > bl.x);
    assert(br.y > tr.y);   // bottom is below top
    assert(bl.y > tl.y);
    // All four are inside the card, which is the point of the inset.
    assert(card.contains(tr.cx(), tr.cy()) && card.contains(tl.cx(), tl.cy()));
    assert(card.contains(br.cx(), br.cy()) && card.contains(bl.cx(), bl.cy()));

    // A small card: 40*0.11 = 4.4, floored at 9.
    const LRect small{0.0, 0.0, 40.0, 40.0};
    near(closeButtonRect(small, 1.0, Corner::TopRight).w, 18.0, "a small card keeps a 9px radius");

    // A configured size of 0 cannot make the glyph vanish.
    const LRect floored = closeButtonRect(small, 0.0, Corner::TopRight);
    near(floored.w, closeButtonRect(small, 1.0, Corner::TopRight).w * 0.3,
         "a size multiplier below 0.3 is floored, not obeyed");

    // sizeScale above 1 is obeyed, so the knob is not silently clamped.
    assert(closeButtonRect(card, 2.0, Corner::TopRight).w > tr.w);
    std::puts("closeButtonRect: square, inside the card, all four corners distinct");
  }

  // --- the counter hangs off the caption pill as a stem ---------------------------------
  //
  // The pill is a capsule, so its bottom is FLAT across pill.w - pill.h and only the last
  // pill.h / 2 at each end is an arc. A sprout narrower than that flat span lands on the
  // pill along a straight edge with no gap, whatever radius it carries -- the rounding
  // cuts inward and the pill already covers that line. Touching rather than overlapping is
  // what keeps it a single shape: two translucent rects over the same pixels blend twice
  // and the join reads as a dark seam.
  {
    const LRect pill{800.0, 900.0, 200.0, 30.0};  // flat bottom = 200 - 30 = 170
    const double padX = 14.0, padY = 6.0;
    const LRect s = captionSprout(pill, 34.0, 18.0, padX, padY);  // h = 30, w = 62

    near(s.y, pill.y + pill.h, "the stem starts exactly where the pill ends");
    near(s.cx(), pill.cx(), "the stem is centred under the pill");
    near(s.h, 18.0 + 2 * padY, "the stem measures its own text plus padding");
    near(s.w, 34.0 + 2 * padX, "a short count gets its natural width");

    // The invariant the whole shape rests on: the whole width lands on the flat span.
    assert(s.w <= pill.w - pill.h);

    // A STEM, not a second capsule. Half the height would make it an oval hanging off an
    // oval, which is what this replaced.
    // The stem carries the PILL's radius, not one of its own. Two pills hanging off each other
    // with different corner softness read as two pills that happen to touch, which is the
    // one thing this shape is not.
    near(captionSproutRound(pill.h), pill.h * 0.5,
         "the stem is drawn with the pill's own radius");

    // Never 0: the shoulder is a rect of this height drawn under the pill, and renderRect
    // asserts on a zero dimension.
    assert(captionSproutRound(0.0) >= 1.0);

    // A wide count against a NARROW pill is the case the clamp exists for: without it the
    // stem would run past the flat span onto the pill's arc and leave a gap at the ends.
    const LRect narrow{800.0, 900.0, 60.0, 30.0};
    const LRect tight = captionSprout(narrow, 200.0, 18.0, padX, padY);
    assert(tight.w <= narrow.w - narrow.h);
    assert(tight.w < 200.0 + 2 * padX);  // clamped down to what the pill can carry
    near(tight.cx(), narrow.cx(), "a clamped stem is still centred");

    // A pill exactly as wide as it is tall has no flat bottom at all. There is no stem to
    // land on, so it must be NO stem -- and never a zero-width rect, which renderRect
    // asserts on (OpenGL.cpp RASSERT(w > 0 && h > 0)).
    const LRect stubby{800.0, 900.0, 30.0, 30.0};
    sameRect(captionSprout(stubby, 40.0, 18.0, padX, padY), LRect{0.0, 0.0, 0.0, 0.0},
             "no flat bottom, no stem");
    sameRect(captionBody(stubby, captionSprout(stubby, 40.0, 18.0, padX, padY)), stubby,
             "and the yield box is the pill alone");

    // Wider than tall the other way: a stubby pill on a huge padY, which is what a user
    // padding gets them. Same clamp, same reason.
    const LRect tallPad{800.0, 900.0, 40.0, 30.0};
    const LRect tp = captionSprout(tallPad, 60.0, 18.0, padX, 40.0);
    assert(tp.w <= std::max(0.0, tallPad.w - tallPad.h));

    // The radius never exceeds half the height, or Hyprland rounds the shape inside out.
    assert(captionSproutRound(s.h) <= s.h * 0.5 + kEps);

    // The yield box. A zero rect is NOT neutral in a min/max union -- it anchors the box at
    // the origin, and a preview nowhere near the caption then reads as contact and shoves
    // it sideways. That bug shipped once; this is the assertion that killed it.
    {
      const LRect p{600.0, 900.0, 200.0, 30.0};
      const LRect stem = captionSprout(p, 34.0, 18.0, padX, padY);
      const LRect both = captionBody(p, stem);
      // The stem is centred and narrower, so the union is the pill's width and the pill's
      // top -- it only ever grows DOWNWARD, which is the whole point of hanging a stem.
      near(both.x, p.x, "the union keeps the pill's left edge");
      near(both.w, p.w, "and its width");
      near(both.y, p.y, "and its top");
      near(both.h, p.h + stem.h, "growing down by exactly the stem's height");

      sameRect(captionBody(p, LRect{0.0, 0.0, 0.0, 0.0}), p,
               "no stem, no union: the box stays the pill, not the screen origin");
      std::puts("captionBody: pill + stem as one, and the pill alone when there is no stem");
    }

    // One card has no "1 / 1" to show: a zero-size text yields no stem at all, so the
    // caller draws one rect instead of two.
    sameRect(captionSprout(pill, 0.0, 0.0, padX, padY), LRect{0.0, 0.0, 0.0, 0.0},
             "no text, no stem");
    std::puts("captionSprout: a stem on the pill's flat edge, rounded below, centred");
  }

  std::puts("render geometry ok");
  return 0;
}
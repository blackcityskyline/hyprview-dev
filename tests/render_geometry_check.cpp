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
using gloview::render::closeButtonRect;
using gloview::render::Corner;
using gloview::render::dragVisualBox;
using gloview::render::insetWithin;
using gloview::render::killZoneGlow;
using gloview::render::pillRadius;
using gloview::render::stackCaption;

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

  // --- the counter pill and the caption pill yield as one box ---------------------------
  //
  // They are two separate pills now, sized and coloured on their own knobs, but they are
  // one caption: a preview that shoves one and not the other leaves them drifted apart, and
  // the gap reads as a mistake rather than as motion.
  {
    const LRect p{600.0, 900.0, 200.0, 30.0};
    // The counter pill: its own text plus its OWN padding, which is the whole point of the
    // split -- nothing about it is derived from the title pill above it.
    const LRect c{640.0, 930.0, 62.0, 30.0};
    const LRect both = captionBody(p, c);

    // The counter is narrower and centred, so the union is the pill's width and the pill's
    // top: it only ever grows DOWNWARD, which is what keeps the caption anchored to its card.
    near(both.x, p.x, "the union keeps the caption pill's left edge");
    near(both.w, p.w, "and its width");
    near(both.y, p.y, "and its top");
    near(both.h, c.y + c.h - p.y, "growing down through the gap and the counter pill");

    // A counter WIDER than the pill must widen the box, or a preview sitting beside the
    // counter would not register as contact at all.
    const LRect wide = captionBody(p, LRect{580.0, 930.0, 260.0, 30.0});
    near(wide.x, 580.0, "a wider counter widens the box");
    // 580..840: the counter reaches 40 past the pill's right edge on both counts.
    near(wide.w, 260.0, "reaching as far as the counter does");

    // An empty counter -- one card, or no counter on screen -- collapses to the pill. A
    // zero rect is NOT neutral in a min/max union: it anchors the box at the screen origin,
    // and a preview nowhere near the caption then reads as contact and shoves it sideways.
    // That bug shipped once; this is the assertion that killed it.
    sameRect(captionBody(p, LRect{0.0, 0.0, 0.0, 0.0}), p,
             "no counter, no union: the box stays the pill, not the screen origin");
    std::puts("captionBody: caption + counter as one box, and the pill alone with no counter");
  }

  // --- where the caption's two pills land ----------------------------------------------
  //
  // Two independent knobs, and they are independent on purpose: title_position moves the
  // WHOLE block off the card's edge, counter_position only swaps the two inside it. The
  // caller owns the block's top (it is what clamps to the screen), so this only decides the
  // order inside.
  {
    const double tH = 30.0, cH = 24.0, gap = 8.0, top = 100.0;

    const auto low = stackCaption(top, tH, cH, gap, true);
    near(low.titleY, top, "counter below: the title leads");
    near(low.counterY, top + tH + gap, "and the counter trails one gap down");

    const auto high = stackCaption(top, tH, cH, gap, false);
    near(high.counterY, top, "counter on top: the counter leads");
    near(high.titleY, top + cH + gap, "and the title trails one gap down");

    // The block's HEIGHT must not depend on the order, or the caller's screen clamp reserves
    // room for a caption that is not that tall -- which shows up as a number hanging off the
    // bottom of the panel exactly when the counter is moved up top.
    near((low.counterY + cH) - top, tH + gap + cH, "counter below: the block runs both pills plus the gap");
    near((high.titleY + tH) - top, tH + gap + cH, "counter on top: the same height, so the clamp still fits");

    // One pill standing alone -- no counter, or one card -- collapses onto the top edge. If
    // it did not, a lone title pill would sit a gap lower than it ever has.
    for (const bool below : {true, false})
      near(stackCaption(top, tH, 0.0, gap, below).titleY, top,
           "no counter, no gap: the title sits where the block starts");
    std::puts("stackCaption: title and counter, either order, and a lone title");
  }

  // --- a configured corner radius ------------------------------------------------------
  //
  // Negative means "a capsule", which is what these pills have always been, and the default
  // says so rather than hard-coding h / 2 at every draw site. The cap is the load-bearing
  // half: Hyprland's rounding is an inward cut, and a radius past the half-short-side does
  // not round more, it inverts the shape.
  {
    near(pillRadius(-1.0, 200.0, 30.0), 15.0, "negative is a capsule on the short side");
    near(pillRadius(-1.0, 30.0, 200.0), 15.0, "and it follows the shape, not just the height");
    near(pillRadius(0.0, 200.0, 30.0), 0.0, "zero is square");
    near(pillRadius(8.0, 200.0, 30.0), 8.0, "a radius in range passes through");
    near(pillRadius(99.0, 200.0, 30.0), 15.0, "past the cap it clamps, it does not invert");
    near(pillRadius(-1.0, 0.0, 0.0), 0.0, "a degenerate box gets nothing, not a negative");
    std::puts("pillRadius: capsule, square, in-range, and clamped at half the short side");
  }

  std::puts("render geometry ok");
  return 0;
}
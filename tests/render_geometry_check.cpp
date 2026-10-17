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
#include <cstdlib>
#include <cstdio>
#include <string>
#include <utility>

#include "../src/render/geometry.hpp"

using gloview::LRect;
using gloview::render::closeButtonRect;
using gloview::render::pillScale;
using gloview::render::Corner;
using gloview::render::dragVisualBox;
using gloview::render::insetWithin;
using gloview::render::killZoneGlow;
using gloview::render::pillRadius;
using gloview::render::captionPills;
using gloview::render::captionPillY;

namespace {

constexpr double kEps = 1e-9;

int g_checks = 0;

void want(bool got, bool expected, const char* what) {
  ++g_checks;
  if (got != expected) {
    std::fprintf(stderr, "  %s: got %d, want %d\n", what, got, expected);
    std::abort();
  }
}

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

// The caption pill must fit the card it labels. The run's cards shrink in rounding steps with
// distance and the label is rasterised once at full width, so a pill sized off the texture
// alone is the same width on every card -- wider than the thing it labels on a shrunken one,
// which is what a hover over a secondary card showed. Each case below turns red if its half
// of pillScale() is replaced by a constant.
static void pillFitsCard() {
  // A pill that already fits is untouched, or the foreground card's title would shrink for no
  // reason and every caption would come out smaller than its text needs to be.
  want(pillScale(692.0, 12.0, 305.0) == 1.0, true, "a pill that already fits is not shrunk");
  // A step-shrunk card brings its pill down with it, and it ENDS UP fitting -- which is the
  // point, since 1.0 sails past a card of every size on the run.
  const double k = pillScale(180.0, 12.0, 305.0);
  want(k < 1.0, true, "a pill wider than its card is scaled down");
  want(305.0 * k <= 180.0 - 12.0, true, "the scaled pill fits the card");
  // A card narrower than the inset still gets a drawable pill rather than a negative size,
  // which is what an unguarded max() hands to a rect.
  want(pillScale(4.0, 12.0, 305.0) > 0.0, true, "a card narrower than the inset still scales");
  // No pill is not a division by zero.
  want(pillScale(692.0, 12.0, 0.0) == 1.0, true, "no pill is not a division by zero");
}

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


  // --- where the caption's two pills land ----------------------------------------------
  //
  // Both knobs name a card EDGE. On one edge the counter is the outer pill either way --
  // above the card it leads, below the card it trails -- so the pair reads the same way
  // whichever way round the card is. That is the invariant; a counter between the card and
  // the title reads as part of the card's own furniture.
  {
    // The card gap and the title-to-counter gap are DIFFERENT knobs, and they are
    // deliberately different numbers here. Passing one value for both is what put the
    // caption flush against the card -- no gap at all whenever there was no counter to
    // measure against -- and no assertion written over a single gap would have seen it.
    const double tH = 30.0, cH = 24.0, gap = 8.0, cardGap = 10.0;
    const double cardY = 200.0, cardH = 300.0, screenH = 756.0, margin = 6.0;

    const auto low = captionPills(cardY, cardH, tH, cH, cardGap, gap, true, screenH, margin);
    near(low.titleY, cardY + cardH + cardGap,
         "below the card: the title sits the CARD gap off the edge");
    near(low.counterY - low.titleY, tH + gap, "and the counter trails it by the STACK gap");

    const auto high = captionPills(cardY, cardH, tH, cH, cardGap, gap, false, screenH, margin);
    near(high.titleY + tH, cardY - cardGap,
         "above the card: the title sits the CARD gap off the edge");
    near(high.titleY - high.counterY, cH + gap, "and the counter leads it, one gap further out");

    // The invariant, stated against the CARD rather than against the title: the counter is
    // further from the card on both edges. A counter between the card and the title reads as
    // part of the card's furniture rather than as a number about the run.
    assert(low.counterY - (cardY + cardH) > low.titleY - (cardY + cardH));
    assert(cardY - high.counterY > cardY - high.titleY);

    // Clamped as ONE block: the counter on the bottom edge is what would walk off, and
    // clamping the title alone let it go past the screen edge while the title sat on the
    // margin.
    const auto tight = captionPills(700.0, 100.0, tH, cH, cardGap, gap, true, screenH, margin);
    near(tight.counterY + cH, screenH - margin, "a caption too tall for the screen is capped");

    const auto tightTop = captionPills(40.0, 100.0, tH, cH, cardGap, gap, false, screenH, margin);
    near(tightTop.counterY, margin, "and floored, outer pill first");

    std::puts("captionPills: the counter is the outer pill on either edge, clamped as a block");
  }

  // --- one pill's own edge --------------------------------------------------------------
  //
  // The split case: title and counter on opposite edges, so there is no order between them
  // and each hangs off the card alone.
  {
    const double screenH = 756.0, margin = 6.0;
    near(captionPillY(300.0, 400.0, 30.0, 10.0, true, screenH, margin), 710.0,
         "below: the card's lower edge plus the gap");
    near(captionPillY(300.0, 400.0, 30.0, 10.0, false, screenH, margin), 260.0,
         "above: the card's upper edge minus the gap, less the pill");
    assert(captionPillY(740.0, 100.0, 30.0, 10.0, true, screenH, margin) + 30.0 <= screenH);
    assert(captionPillY(20.0, 100.0, 30.0, 10.0, false, screenH, margin) >= 0.0);
    std::puts("captionPillY: either edge, either clamp direction");
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

  // The strip band's placement. Every one of these was a private Overview method reading
  // cfg:: at the point of use; they are here because a band that lands on the wrong edge is
  // a wrong ANSWER, not a wrong drawing.
  {
    using gloview::render::Anchor;
    using gloview::render::isHorizontal;
    using gloview::render::parseAnchor;
    using gloview::render::stripBand;
    using gloview::render::stripOffset;
    using gloview::render::stripSlide;
    using gloview::render::stripThickness;

    assert(parseAnchor("bottom", "") == Anchor::Bottom);
    assert(parseAnchor("left", "") == Anchor::Left);
    assert(parseAnchor("right", "") == Anchor::Right);
    assert(parseAnchor("top", "") == Anchor::Top);
    // Anything unrecognised is Top, and an empty anchor falls back to the older knob
    // rather than to Top -- otherwise upgrading silently moves a bottom strip.
    assert(parseAnchor("nonsense", "bottom") == Anchor::Top);
    assert(parseAnchor("", "bottom") == Anchor::Bottom);
    assert(parseAnchor("", "") == Anchor::Top);
    assert(isHorizontal(Anchor::Top) && isHorizontal(Anchor::Bottom));
    assert(!isHorizontal(Anchor::Left) && !isHorizontal(Anchor::Right));
    std::puts("strip anchor: four edges, fallback on empty, Top on unknown");

    // The 100px floor, and the inverted range a small offset_max would have produced.
    near(stripThickness(1080.0, 120.0, 0.3), 120.0, "in range passes through");
    near(stripThickness(1080.0, 40.0, 0.3), 100.0, "below the floor it clamps up");
    // offset_max 0.05 on a 1080 cross axis puts hi at 54, under the floor: clamp(40, 54, 54)
    // is the valid range and 54 is the answer. Passing a floor above hi used to invert it.
    near(stripThickness(1080.0, 40.0, 0.05), 54.0, "a tiny offset_max does not invert");
    near(stripThickness(1080.0, 9999.0, 0.3), 324.0, "above hi it clamps down");
    std::puts("strip thickness: floor, ceiling, and no inverted range");

    near(stripOffset(1080.0, -50.0), 0.0, "negative offsets clamp to 0");
    near(stripOffset(1080.0, 20.0), 20.0, "in range passes through");
    // The ceiling is cross * 0.4, not cross * offset_max: this one bounds how far the band
    // can be pushed in from its edge and has always been its own number.
    near(stripOffset(1080.0, 1e9), 432.0, "the offset has its own ceiling");
    std::puts("strip offset: floored at 0, ceiled at 40% of the cross axis");

    // One band, four edges. A mis-placed band reads as a correct band in the wrong place,
    // which is why each edge is checked against all four of its numbers.
    const auto top = stripBand(Anchor::Top, 1366.0, 768.0, 60.0, 10.0);
    sameRect(top, LRect{0, 10, 1366, 60}, "top band");
    const auto bottom = stripBand(Anchor::Bottom, 1366.0, 768.0, 60.0, 10.0);
    sameRect(bottom, LRect{0, 698, 1366, 60}, "bottom band");
    const auto left = stripBand(Anchor::Left, 1366.0, 768.0, 60.0, 10.0);
    sameRect(left, LRect{10, 0, 60, 768}, "left band");
    const auto right = stripBand(Anchor::Right, 1366.0, 768.0, 60.0, 10.0);
    sameRect(right, LRect{1296, 0, 60, 768}, "right band");
    std::puts("strip band: all four edges land on that edge");

    // The reveal slide: at e == 1 the band is in place, at e == 0 it is off-screen, and
    // the direction is always OFF the anchored edge.
    for (const auto &[a, want] : {std::pair{Anchor::Top, gloview::LVec2{0.0, -30.0}},
                                 std::pair{Anchor::Bottom, gloview::LVec2{0.0, 30.0}},
                                 std::pair{Anchor::Left, gloview::LVec2{-30.0, 0.0}},
                                 std::pair{Anchor::Right, gloview::LVec2{30.0, 0.0}}}) {
      const auto half = stripSlide(a, 0.0, 60.0, 0.5);
      near(half.x, want.x, "slide x at e=0");
      near(half.y, want.y, "slide y at e=0");
      const auto done = stripSlide(a, 1.0, 60.0, 0.5);
      near(done.x, 0.0, "slide x at e=1");
      near(done.y, 0.0, "slide y at e=1");
    }
    near(stripSlide(Anchor::Top, 0.5, 60.0, 0.5).y, -15.0, "halfway is half the distance");
    // slide_frac is a fraction OF THE THICKNESS, so a band twice as thick slides twice as far.
    near(stripSlide(Anchor::Top, 0.0, 120.0, 0.5).y, -60.0, "slide scales with thickness");
    near(stripSlide(Anchor::Top, 0.0, 60.0, 0.0).y, 0.0, "a zero slide_frac never moves");
    std::puts("strip slide: off the anchored edge, scaled by thickness and slide_frac");
  }

  
  // One of the two boxes the strip draws and the hit-test must agree on to the pixel.
  {
    using gloview::render::fitInside;
    using gloview::render::winSlotRect;
    const auto card = LRect{100, 40, 167, 94};


    // A thumbnail is stored card-relative, so it follows the card's size.
    sameRect(winSlotRect(LRect{0.5, 0.0, 0.5, 0.25}, card),
             LRect{183.5, 40, 83.5, 23.5}, "a relative box scales onto the card");
    sameRect(winSlotRect(LRect{0, 0, 1, 1}, card), card, "rel of 1,1 is the card itself");
    // The 2px floor on both axes: a window on a tall thin strip otherwise gets an
    // invisible sliver it cannot be clicked on. Above the floor it scales normally.
    const auto floored = winSlotRect(LRect{0, 0, 0.001, 0.001}, card);
    near(floored.w, 2.0, "width floor");
    near(floored.h, 2.0, "height floor");
    near(floored.x, 100.0, "floor does not move the origin");
    near(winSlotRect(LRect{0, 0, 0.4, 0.4}, card).w, 66.8, "above the floor it scales");
    std::puts("winSlotRect: card-relative scaling with a 2px floor on both axes");
  }

  // fitInside: the largest box of a given aspect that fits, centred.
  {
    using gloview::render::fitInside;
    const auto outer = LRect{0, 0, 200, 100};
    sameRect(fitInside(outer, 2.0), outer, "a matching aspect gets the outer box");
    // Narrower than the outer (2.0): the width is the limit, so it narrows -- pillarboxed
    // on the sides, centred.
    sameRect(fitInside(outer, 1.0), LRect{50, 0, 100, 100}, "too narrow -> pillarbox");
    // Wider than the outer: the height is the limit, so it shortens -- letterboxed top and
    // bottom, centred.
    sameRect(fitInside(outer, 4.0), LRect{0, 25, 200, 50}, "too wide -> letterbox");
    // The tolerance: within 0.01 the outer box comes back untouched, so a preview is never
    // a pixel narrower than its ring.
    sameRect(fitInside(outer, 2.005), outer, "just inside the tolerance");
    sameRect(fitInside(outer, 1.98), LRect{1, 0, 198, 100}, "just outside it");
    // Degenerate inputs return the outer box rather than dividing by zero.
    sameRect(fitInside(LRect{0, 0, 0, 0}, 2.0), LRect{0, 0, 0, 0}, "a zero box");
    sameRect(fitInside(outer, 0.0), outer, "a zero aspect");
    sameRect(fitInside(outer, -2.0), outer, "a negative aspect");
    std::puts("fitInside: matched, pillarboxed, letterboxed, tolerance, degenerate");
  }

  // firstHit: which of a row of boxes is under the cursor.
  {
    using gloview::render::firstHit;
    const LRect row[] = {{0, 0, 100, 100}, {100, 0, 100, 100}, {200, 0, 100, 100}};

    want(firstHit(50, 50, row, 3) == 0, true, "a point in the first box");
    want(firstHit(150, 50, row, 3) == 1, true, "a point in the second");
    want(firstHit(250, 50, row, 3) == 2, true, "a point in the third");

    // A miss is -1, and it is asked for on every mouse move outside a tile, so it has to be
    // an answer rather than a clamp onto the nearest card.
    want(firstHit(50, 500, row, 3) == -1, true, "below the row");
    want(firstHit(-5, 50, row, 3) == -1, true, "left of the row");
    want(firstHit(400, 50, row, 3) == -1, true, "right of the row");
    want(firstHit(50, 50, row, 0) == -1, true, "an empty row");

    // Boundary: contains() is inclusive on both edges, so the seam between two boxes
    // belongs to the first one. Either answer is defensible there; what is NOT defensible
    // is a seam that belongs to neither.
    want(firstHit(100, 50, row, 3) == 0, true, "the seam belongs to the earlier box");

    // Overlapping boxes: first match in ORDER wins, which is the grid rule. The carousel's
    // rule is the opposite and lives in its own loop -- a single helper doing both is how a
    // click lands on the card behind the one you aimed at.
    const LRect overlap[] = {{0, 0, 200, 200}, {0, 0, 50, 50}};
    want(firstHit(10, 10, overlap, 2) == 0, true, "an overlap picks the earlier box");
    std::puts("firstHit: hits, misses, seams, an empty row, and overlap order");
  }

  pillFitsCard();
  std::puts("pillScale: fits, shrinks, degenerate cards, and no division by zero");
  std::puts("render geometry ok");
  return 0;
}

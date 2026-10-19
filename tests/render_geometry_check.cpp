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

using hyprview::LRect;
using hyprview::render::closeButtonRect;
using hyprview::render::cardScale;
using hyprview::render::captionRemains;
using hyprview::render::escapeFrom;
using hyprview::render::scaledAboutCentre;
using hyprview::render::pillScale;
using hyprview::render::Corner;
using hyprview::render::dragVisualBox;
using hyprview::render::insetWithin;
using hyprview::render::killZoneGlow;
using hyprview::render::pillRadius;
using hyprview::render::captionPills;
using hyprview::render::captionPillY;

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
// A caption is labelled at its OWN card's size. The run's cards shrink in steps with distance,
// so a card two or three slots out was carrying the CENTRED card's caption size: the same pill
// on a card half the width, and worst on the outermost cards of a five-card run.
static void captionFollowsItsCard() {
  near(cardScale(692.0, 692.0), 1.0, "the centred card labels at full size");
  near(cardScale(526.0, 692.0), 526.0 / 692.0, "a neighbour scales with its card");
  want(cardScale(300.0, 692.0) < cardScale(526.0, 692.0), true,
       "the outermost ring scales further than the near neighbour");
  near(cardScale(692.0, 0.0), 1.0, "no reference set means no scaling, not a division by zero");
  near(cardScale(0.0, 692.0), 1.0, "a zero-width card is not scaled");
  want(cardScale(40.0, 692.0) >= 0.45, true,
       "the scale is floored: a caption scaled to nothing is an absent caption");
  std::puts("cardScale: reference, neighbours, the outermost ring, no reference, and the floor");
}

// Which way a chrome element gets out from under a card that reached it. This rule went
// wrong twice in a day and both times it was found by eye: once the caption was lifted
// further and further above its own card until it reached the strip, and once the fix for
// that stopped the caption being pushed at all. Both are this one choice of axis, so the
// choice is the test.
static void chromeTakesTheAxisThatKeepsItWithItsCard() {
  const LRect card{400, 300, 300, 200};   // the covering card
  const LRect capAbove{360, 240, 380, 40}; // a caption above it, wider than the card

  // The regression this exists for: the caption is wholly above the card, the vertical escape
  // is up, and up is the one direction that takes it away from the card it labels.
  {
    // ox > oy on purpose: that is the input that REACHES the vertical branch at all, which is
    // the branch the caption-above case lives in. With ox < oy the horizontal branch returns
    // first and this test would pass whatever the vertical branch did -- which is how it
    // passed with the guard deleted.
    const auto e = escapeFrom(capAbove, card, 200, 40);
    want(e.dy == 0.0, true, "a caption above the card is never lifted");
    want(e.dx != 0.0, true, "it slides sideways instead -- it is still pushed");
    want(e.depth == 200.0, true, "with the depth of the axis it took");
  }
  // A caption BELOW the card: down is beside the card, so the vertical push stands.
  {
    const LRect capBelow{360, 520, 380, 40};
    const auto e = escapeFrom(capBelow, card, 200, 40);
    want(e.dy > 0.0, true, "a caption below the card is pushed down");
    want(e.depth == 40.0, true, "by the vertical overlap");
  }
  // Room on the horizontal axis alone: that axis was always chosen and must stay.
  {
    const LRect wide{380, 280, 340, 240};
    const auto e = escapeFrom(wide, card, 40, 200);
    want(e.dx != 0.0 && e.dy == 0.0, true, "the cheaper axis still wins");
    want(e.depth == 40.0, true, "by the horizontal overlap");
  }
  // Direction is away from the covering card on both axes, or the element moves deeper in.
  {
    want(escapeFrom(LRect{100, 200, 100, 40}, card, 20, 50).dx < 0.0, true,
         "left of the card means left");
    want(escapeFrom(LRect{600, 200, 100, 40}, card, 20, 50).dx > 0.0, true,
         "right of the card means right");
  }
  // A caption that STRADDLES the card's top edge is not wholly above it, so the push is the
  // ordinary one -- the cover is what it straddles, not a card below it.
  {
    const LRect straddle{360, 290, 380, 40};
    want(escapeFrom(straddle, card, 200, 50).dy != 0.0, true,
         "a caption overlapping the card's top edge is pushed normally");
  }
  std::puts("escapeFrom: a caption above its card slides, one below is pushed down, cheaper axis, direction");
}

// How much of a caption is left. Two reasons it leaves -- being shoved, and its card leaving
// -- and every version of this has been inline arithmetic in drawTile, adjusted by eye and by
// screenshot. It is the one decision in the caption that had no pure form, which is why its
// rate was the thing I could not measure.
static void aCaptionIsGoneWhenItsCardIs() {
  // Nothing wrong with it: it stays whole, whatever the exit fraction says while opening.
  near(captionRemains(0.0, 300.0, 27.0, 1.0, /*opening=*/true), 1.0,
       "a caption that is not being shoved is whole, even mid-open");
  // Shoved a quarter of its own width: gone. A tenth of it: nearly whole. The rate is the
  // point -- at its full width it was still shrinking when it arrived at the strip.
  near(captionRemains(75.0, 300.0, 27.0, 0.0, true), 0.0, "gone over a quarter of its width");
  want(captionRemains(10.0, 300.0, 27.0, 0.0, true) > 0.8, true,
       "barely shoved leaves it nearly whole");
  // Its card leaving: it leaves with the card, and never outlives it.
  near(captionRemains(0.0, 300.0, 27.0, 1.0, false), 0.0, "gone when the card is gone");
  want(captionRemains(0.0, 300.0, 27.0, 0.5, false) < 0.6, true, "half out with a half-gone card");
  // Either reason is enough, and together they do not fight: a caption on a card that is
  // leaving AND being shoved is the smaller of the two, never the larger.
  want(captionRemains(300.0, 300.0, 27.0, 0.1, false) <=
       captionRemains(0.0, 300.0, 27.0, 0.1, false), true,
       "the stronger reason wins, never the weaker");
  std::puts("captionRemains: whole at rest, gone over a quarter width, gone with its card");
}

// Both pills, one factor, one rule. The counter was left out once on the grounds that it
// counts the run rather than labelling a card, and the screen showed two different rules on
// one caption: the title gone, the number standing under it.
static void bothPillsShareOneRect() {
  const LRect r{100.0, 50.0, 300.0, 27.0};
  const LRect half = scaledAboutCentre(r, 0.5);
  near(half.w, 150.0, "halved width");
  near(half.h, 13.5, "halved height");
  near(half.cx(), r.cx(), "about its own centre in x");
  near(half.cy(), r.cy(), "about its own centre in y");
  near(scaledAboutCentre(r, 0.0).w, 0.0, "gone is gone, not negative");
  near(scaledAboutCentre(r, 1.0).w, r.w, "whole is whole");
  std::puts("scaledAboutCentre: both axes, about the centre, and the two ends");
}

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

// The strip's own decisions: the band's alpha, the label's box, the two glyphs and
// the wallpaper crop. They were arithmetic at the draw site, where a change of one
// number moved a shape and nothing said so.
static void theStripDecidesItsOwnShapes() {
  using hyprview::render::allGlyph;
  using hyprview::render::coverFitUV;
  using hyprview::render::plusGlyph;
  using hyprview::render::stripBandAlpha;
  using hyprview::render::stripLabelBox;

  // One source for the band's alpha: the knob, scaled by the band's clock. The
  // colour string carries no alpha of its own.
  near(stripBandAlpha(70.0, 1.0), 0.7, "the knob is the band's alpha");
  near(stripBandAlpha(70.0, 0.5), 0.35, "and the band's clock scales it");
  near(stripBandAlpha(150.0, 1.0), 1.0, "a knob over 100 is clamped");
  near(stripBandAlpha(-10.0, 1.0), 0.0, "a negative knob is no alpha");

  // The label lives in the band ABOVE the card, and is shrunk to fit the card
  // plus the extra the knob allows.
  const LRect card{100.0, 200.0, 300.0, 100.0};
  const auto fits = stripLabelBox(card, 200.0, 20.0, 26.0, 24.0);
  sameRect(fits, LRect{150.0, 177.0, 200.0, 20.0}, "a label that fits keeps its size");
  const auto wide = stripLabelBox(card, 400.0, 20.0, 26.0, 24.0);
  near(wide.w, 324.0, "a wide label is shrunk to the card plus the extra");
  near(wide.h, 20.0 * (324.0 / 400.0), "and it keeps its proportions");
  want(wide.cx(), card.cx(), "and it stays centred on the card");

  // The "+" is a cross in the middle of the card, and a card too small for it
  // gets nothing rather than a speck.
  const auto plus = plusGlyph(card);
  want(plus.drawable, true, "a card with room gets the cross");
  near(plus.horizontal.cy(), card.cy(), "the cross is centred");
  near(plus.vertical.cx(), card.cx(), "the cross is centred");
  near(plus.horizontal.w, 34.0, "the arm is a third of the short side");
  want(plusGlyph(LRect{0.0, 0.0, 1.0, 1.0}).drawable, false, "a speck is not a cross");

  // The "All" glyph is a 2x2 grid, symmetric and inside the card.
  const auto all = allGlyph(card);
  want(all.cells[0].x != all.cells[1].x, true, "two columns");
  want(all.cells[0].y != all.cells[2].y, true, "two rows");
  for (const auto &c : all.cells)
    want(card.contains(c.cx(), c.cy()), true, "every cell is inside the card");

  // The wallpaper is cover-fit: the crop is on the texture's own axis, and the
  // other axis is used whole.
  const auto uv = coverFitUV(300.0, 100.0, 1920.0, 1080.0);
  near(uv.u0, 0.0, "the wide axis is used whole");
  near(uv.u1, 1.0, "the wide axis is used whole");
  want(uv.v0 > 0.0 && uv.v1 < 1.0, true, "the tall axis is cropped");
  near(uv.v0, 1.0 - uv.v1, "and the crop is centred");
  const auto tallCard = coverFitUV(100.0, 300.0, 1920.0, 1080.0);
  near(tallCard.v0, 0.0, "a card taller than the texture uses the tall axis whole");
  near(tallCard.v1, 1.0, "a card taller than the texture uses the tall axis whole");
  want(tallCard.u0 > 0.0 && tallCard.u1 < 1.0, true, "and the wide axis is cropped");
  std::puts("the strip: band alpha, label box, two glyphs, and the wallpaper crop");
}

// The flight of a swap: four kinds, one answer each. These were branches inside the
// render loop, where a change of one number moved a card and nothing said so.
static void theFlightHasFourKinds() {
  using hyprview::anim::Axis;
  using hyprview::anim::Travel;
  using hyprview::render::swapFlight;
  using hyprview::render::SwapFlightInput;

  const LRect from{0.0, 0.0, 100.0, 100.0};
  const LRect to{200.0, 0.0, 100.0, 100.0};

  // A plain slide: the card travels from one box to the other.
  {
    SwapFlightInput in;
    in.travel = Travel::Slide;
    in.axis = Axis::Horizontal;
    in.from = from;
    in.to = to;
    in.p = 0.5;
    const auto f = swapFlight(in);
    sameRect(f.box, LRect{100.0, 0.0, 100.0, 100.0}, "halfway is halfway");
    near(f.alpha, 1.0, "a slide does not fade");
  }

  // The vertical swap is an ARC, not a slide on Y: the card leaves upwards by its own
  // height plus the lift, and comes down on the other card.
  {
    SwapFlightInput in;
    in.travel = Travel::Slide;
    in.axis = Axis::Vertical;
    in.from = from;
    in.to = to;
    in.lift = 12.0;
    in.t = 0.0;
    in.q1 = 0.0;
    const auto start = swapFlight(in);
    sameRect(start.box, from, "it starts where it was");

    in.t = 0.25;
    in.q1 = 1.0;
    const auto up = swapFlight(in);
    near(up.box.y, -(100.0 + 12.0), "it leaves upwards by its height and the lift");

    in.t = 0.5;
    in.q2 = 0.0;
    const auto apex = swapFlight(in);
    near(apex.box.y, -(100.0 + 12.0), "the apex is over the destination");
    near(apex.alpha, 0.0, "and the card is gone at the turn");

    in.t = 1.0;
    in.q2 = 1.0;
    const auto end = swapFlight(in);
    sameRect(end.box, to, "it lands on the other card");
    near(end.alpha, 1.0, "and it is back");
  }

  // A fade crosses over at the midpoint.
  {
    SwapFlightInput in;
    in.travel = Travel::Fade;
    in.from = from;
    in.to = to;
    in.t = 0.25;
    const auto first = swapFlight(in);
    sameRect(first.box, from, "the first half is the old card");
    near(first.alpha, 0.5, "fading out");
    in.t = 0.75;
    const auto second = swapFlight(in);
    sameRect(second.box, to, "the second half is the new card");
    near(second.alpha, 0.5, "fading in");
  }

  // A pop shrinks to 70% and grows back with the card curve.
  {
    SwapFlightInput in;
    in.travel = Travel::Pop;
    in.from = from;
    in.to = to;
    in.t = 0.15;
    const auto first = swapFlight(in);
    sameRect(first.box, from, "the first part is the old card");
    near(first.alpha, 0.5, "fading out");
    in.t = 0.3;
    in.popK = 0.7;
    const auto shrunk = swapFlight(in);
    near(shrunk.box.w, 70.0, "it shrinks to 70%");
    near(shrunk.box.cx(), to.cx(), "about its own centre");
    in.t = 1.0;
    in.popK = 1.0;
    const auto grown = swapFlight(in);
    sameRect(grown.box, to, "and it grows back onto the card");
    near(grown.alpha, 1.0, "and it is back");
  }
  std::puts("the flight: slide, vertical arc, fade, pop");
}

// The kill zone sits at the screen edge opposite the strip.
static void theKillZoneIsOppositeTheStrip() {
  using hyprview::render::Anchor;
  using hyprview::render::killZoneRect;
  const auto top = killZoneRect(Anchor::Bottom, 1920.0, 1080.0, 120.0);
  sameRect(top, LRect{0.0, 0.0, 1920.0, 120.0}, "a bottom strip leaves the zone at the top");
  const auto bottom = killZoneRect(Anchor::Top, 1920.0, 1080.0, 120.0);
  sameRect(bottom, LRect{0.0, 960.0, 1920.0, 120.0}, "a top strip leaves the zone at the bottom");
  std::puts("the kill zone: opposite the strip");
}

// How a window's surface maps into its tile: one scale, one translation. It was
// arithmetic in the leaf, where a wrong scale is a stretched window and nothing
// says so.
static void theWindowMapsIntoItsTile() {
  using hyprview::render::windowFit;
  using hyprview::render::WindowFitInput;

  // A fullscreen window on the monitor it fills: one to one, no offset.
  {
    WindowFitInput in;
    in.destW = 1920.0;
    in.destH = 1080.0;
    in.logicalW = 1920.0;
    in.logicalH = 1080.0;
    in.settled = true;
    const auto fit = windowFit(in);
    want(fit.valid, true, "a fullscreen window fits");
    near(fit.scale, 1.0, "at the monitor's own scale");
    near(fit.translate.x, 0.0, "and it is not moved");
    near(fit.translate.y, 0.0, "and it is not moved");
  }

  // The same window on a scaled monitor: the pixels are what matter, not the
  // logical size.
  {
    WindowFitInput in;
    in.destW = 1920.0;
    in.destH = 1080.0;
    in.logicalW = 960.0;
    in.logicalH = 540.0;
    in.monScale = 2.0;
    in.settled = true;
    const auto fit = windowFit(in);
    near(fit.scale, 1.0, "a scaled monitor still draws at its own pixels");
  }

  // A contained window keeps its shape: the smaller of the two ratios wins, and
  // the content is centred in the tile.
  {
    WindowFitInput in;
    in.destW = 1000.0;
    in.destH = 100.0;
    in.logicalW = 500.0;
    in.logicalH = 500.0;
    in.contain = true;
    in.settled = true;
    const auto fit = windowFit(in);
    near(fit.scale, 0.2, "contained by the tight side");
    near(fit.translate.x, 2250.0, "and centred in the tile");
    near(fit.translate.y, 0.0, "and centred in the tile");
  }

  // Not settled: the tile is covered a little over, so the backing never shows
  // as a seam.
  {
    WindowFitInput in;
    in.destW = 1920.0;
    in.destH = 1080.0;
    in.logicalW = 1920.0;
    in.logicalH = 1080.0;
    in.seamPad = 2.0;
    const auto fit = windowFit(in);
    want(fit.scale > 1.0, true, "an unsettled tile is over-covered");
  }

  // A tile with no area draws nothing.
  {
    WindowFitInput in;
    in.destW = 0.0;
    in.destH = 0.0;
    in.settled = true;
    want(windowFit(in).valid, false, "an empty tile has no fit");
  }
  std::puts("the window fit: one to one, contained, over-covered, and nothing at all");
}

// The run paints far -> near, so the focused card and its ring end up on top, and
// the cards the run does not show paint behind everything.
static void theRunPaintsFarBeforeNear() {
  using hyprview::render::carouselPaintOrder;
  using hyprview::render::carouselTone;
  using hyprview::render::CarouselCardRef;

  const std::vector<CarouselCardRef> cards{
      {0, 0, true},  // the focused card
      {1, 1, true},  // its neighbour
      {2, 2, true},  // the far one
      {3, 3, false}, // not on the run
  };
  const auto paint = carouselPaintOrder(cards);
  want(paint.size(), 4u, "every card is in the order");
  want(paint.front() == 3, true, "a card the run does not show paints first");
  want(paint.back() == 0, true, "the focused card paints last");
  // Far before near among the visible ones.
  want(paint[1] == 2, true, "the far card paints before the near one");
  want(paint[2] == 1, true, "the near card paints before the focused one");

  // The dim: clear at the centre, the near tone at one step, the far tone tapered
  // beyond it.
  near(carouselTone(0, 0.125, 0.25, 0.8), 0.0, "the focused card is clear");
  near(carouselTone(1, 0.125, 0.25, 0.8), 0.125, "the neighbour takes the near tone");
  near(carouselTone(2, 0.125, 0.25, 0.8), 0.25 * 0.8, "the far card takes the far tone");
  near(carouselTone(3, 0.125, 0.25, 0.8), 0.25 * 0.8 * 0.8, "and every step past it tapers");
  std::puts("the run: far before near, and the dim that goes with the depth");
}

// A click on the run lands on the front-most card: the cards overlap, so the first
// match is the wrong one.
static void aClickOnTheRunLandsOnTheFront() {
  using hyprview::render::carouselHit;
  using hyprview::render::CarouselHitCard;

  // The focused card and its neighbour overlap; the click belongs to the focused one.
  const std::vector<CarouselHitCard> cards{
      {0, 0, true, LRect{400.0, 200.0, 300.0, 200.0}},
      {1, 1, true, LRect{500.0, 200.0, 300.0, 200.0}},
  };
  want(carouselHit(cards, 550.0, 300.0) == 0, true, "the front card wins the overlap");
  want(carouselHit(cards, 750.0, 300.0) == 1, true, "the far card takes what is only its own");
  want(carouselHit(cards, 100.0, 100.0) == -1, true, "a click on nothing hits nothing");

  // A card the run does not show is not there to be clicked.
  const std::vector<CarouselHitCard> hidden{
      {0, 0, false, LRect{400.0, 200.0, 300.0, 200.0}},
      {1, 1, true, LRect{500.0, 200.0, 300.0, 200.0}},
  };
  want(carouselHit(hidden, 450.0, 300.0) == -1, true, "a hidden card is not a target");
  want(carouselHit(hidden, 550.0, 300.0) == 1, true, "and the shown card still is");
  std::puts("the click on the run: the front-most card, and only what is shown");
}

// The wallpaper covers the monitor, and a frame that is black everywhere is not
// a backdrop.
static void theWallpaperCoversTheMonitor() {
  using hyprview::render::coverFit;
  using hyprview::render::kSourceAnimatedStreak;
  using hyprview::render::looksBlack;

  // The same shape: it fills, exactly.
  const auto same = coverFit(1920.0, 1080.0, 1920.0, 1080.0);
  near(same.scale, 1.0, "no scaling for the same shape");
  near(same.x, 0.0, "and no offset");
  near(same.y, 0.0, "and no offset");

  // The monitor is wider than the image: the image is scaled to the width and
  // centred vertically, the overflow cut off.
  const auto wide = coverFit(1920.0, 1080.0, 1920.0, 1200.0);
  near(wide.scale, 1.0, "scaled to the width");
  near(wide.y, -60.0, "and centred, the overflow cut off");

  // The monitor is taller than the image: the other axis.
  const auto tall = coverFit(1080.0, 1920.0, 1920.0, 1080.0);
  near(tall.scale, 1920.0 / 1080.0, "scaled to the height");
  near(tall.x, (1080.0 - 1920.0 * (1920.0 / 1080.0)) / 2.0, "and centred, the overflow cut off");

  // Black is a frame that is nowhere lit. A dark frame of a video wallpaper is
  // not black: the threshold is low.
  want(looksBlack(0, 0, 0), true, "black is black");
  want(looksBlack(7, 7, 7), true, "almost black is black");
  want(looksBlack(8, 0, 0), false, "one lit channel is not black");
  want(looksBlack(200, 200, 200), false, "a lit frame is not black");

  // Animation is several changes in a row: one re-creation of a static wallpaper
  // is not animation.
  want(kSourceAnimatedStreak >= 2, true, "one change is not animation");
  std::puts("the wallpaper: cover-fit, the black check, and what counts as animation");
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
    using hyprview::render::Anchor;
    using hyprview::render::isHorizontal;
    using hyprview::render::parseAnchor;
    using hyprview::render::stripBand;
    using hyprview::render::stripOffset;
    using hyprview::render::stripSlide;
    using hyprview::render::stripThickness;

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
    for (const auto &[a, want] : {std::pair{Anchor::Top, hyprview::LVec2{0.0, -30.0}},
                                 std::pair{Anchor::Bottom, hyprview::LVec2{0.0, 30.0}},
                                 std::pair{Anchor::Left, hyprview::LVec2{-30.0, 0.0}},
                                 std::pair{Anchor::Right, hyprview::LVec2{30.0, 0.0}}}) {
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
    using hyprview::render::fitInside;
    using hyprview::render::winSlotRect;
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
    using hyprview::render::fitInside;
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
    using hyprview::render::firstHit;
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

  chromeTakesTheAxisThatKeepsItWithItsCard();
  aCaptionIsGoneWhenItsCardIs();
  bothPillsShareOneRect();
  pillFitsCard();
  captionFollowsItsCard();
  theStripDecidesItsOwnShapes();
  theFlightHasFourKinds();
  theKillZoneIsOppositeTheStrip();
  theWindowMapsIntoItsTile();
  theRunPaintsFarBeforeNear();
  aClickOnTheRunLandsOnTheFront();
  theWallpaperCoversTheMonitor();
  std::puts("pillScale: fits, shrinks, degenerate cards, and no division by zero");
  std::puts("render geometry ok");
  return 0;
}

// Where the caption's two pieces land.
//
// The rules worth pinning:
//   - the pill repeats the icon and the text it carries, inset in the card's bottom-left,
//     whatever the counter does;
//   - a title wider than the card shrinks; the bar does not;
//   - the counter is OUTSIDE the card: its own pill, its own edge, its own margin;
//   - the strip band thickens the margin of its own edge, and the counter keeps off it;
//   - a caption with nothing to say draws nothing at all.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/render/geometry.hpp"

using hyprview::LRect;
using hyprview::render::Anchor;
using hyprview::render::Caption;
using hyprview::render::CaptionInput;
using hyprview::render::CaptionLayout;
using hyprview::render::CaptionSizes;
using hyprview::render::CaptionStyle;
using hyprview::render::captionLayout;
using hyprview::render::cardScale;
using hyprview::render::pillRadius;
using hyprview::render::placeCaption;
using hyprview::render::stripPush;

namespace {

constexpr double kEps = 1e-9;

int g_checks = 0;

void near(double got, double want, const char *what) {
  ++g_checks;
  if (std::fabs(got - want) >= kEps) {
    std::fprintf(stderr, "  %s: got %.17g, want %.17g\n", what, got, want);
    std::abort();
  }
}

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::fprintf(stderr, "  %s: got %d, want %d\n", what, got, expected);
    std::abort();
  }
}

void sameRect(const LRect &got, const LRect &want, const char *what) {
  ++g_checks;
  if (std::fabs(got.x - want.x) >= kEps || std::fabs(got.y - want.y) >= kEps ||
      std::fabs(got.w - want.w) >= kEps || std::fabs(got.h - want.h) >= kEps) {
    std::fprintf(stderr, "  %s: got {%.17g %.17g %.17g %.17g}, want {%.17g %.17g %.17g %.17g}\n",
                 what, got.x, got.y, got.w, got.h, want.x, want.y, want.w, want.h);
    std::abort();
  }
}

// One card, one label, no counter: the common case, so the other cases state only what they change.
CaptionSizes sizes(bool label = true, bool counter = false, double textW = 200.0) {
  return CaptionSizes{textW, label ? 24.0 : 0.0, counter ? 90.0 : 0.0, counter ? 20.0 : 0.0,
                      label, counter};
}

CaptionStyle style(bool counterBelow = true) {
  return CaptionStyle{11.0, 5.0, 11.0, 5.0, 8.0, 6.0, 0.0, counterBelow, 6.0};
}

} // namespace

// THE RULE the pill exists for: it repeats the icon and the text it carries -- their size,
// their inset from the card -- instead of spanning the card as a band.
static void thePillRepeatsItsContent() {
  const LRect card{100.0, 200.0, 400.0, 300.0};
  const auto lay = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(), style());

  // 200 of text + 2 * 11 of padding, 24 of text height + 2 * 5, inset 8.
  sameRect(lay.plate, LRect{108.0, 458.0, 222.0, 34.0}, "the pill wraps its content");
  near(lay.plate.x, card.x + 8.0, "it sits the inset in from the card's left edge");
  near(lay.plate.y + lay.plate.h, card.y + card.h - 8.0, "and the inset up from its bottom");
  near(lay.contentX, lay.plate.x + 11.0, "the text starts at the pill's own padding");

  // The counter's edge is the counter's business: the pill does not move with it.
  const auto topCounter = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, true),
                                        style(/*counterBelow=*/false));
  sameRect(topCounter.plate, lay.plate, "a counter on the top edge leaves the pill alone");
  near(topCounter.counter.y + topCounter.counter.h, card.y - 8.0,
       "and the counter is the thing outside, above the card");
  std::puts("the pill: icon and text plus padding, inset in the card's bottom-left");
}

// The pill cannot run off its card, so a long title shrinks inside it -- the pill grows with
// the text until it meets the inset, then the text does the giving.
static void aLongTitleShrinksInThePill() {
  const LRect card{100.0, 200.0, 400.0, 300.0};

  const auto shortTitle = captionLayout(card, LRect{}, 1920.0, 1080.0,
                                        sizes(true, false, 120.0), style());
  near(shortTitle.kFit, 1.0, "a short title is drawn at full size");
  near(shortTitle.plate.w, 120.0 + 2.0 * 11.0, "and the pill is its width plus the padding");

  const auto longTitle = captionLayout(card, LRect{}, 1920.0, 1080.0,
                                       sizes(true, false, 900.0), style());
  want(longTitle.kFit < 1.0, true, "a long title is scaled down to fit");
  near(longTitle.plate.x + longTitle.plate.w, card.x + card.w - 8.0,
       "the pill stops at the card's inset, not past it");
  near(900.0 * longTitle.kFit, card.w - 2.0 * 8.0 - 2.0 * 11.0,
       "the text takes the card less the inset and the padding, no more");
  std::puts("the pill: a long title shrinks inside it, the pill stops at the inset");
}

// The icon leads the pill: it widens the pill by its own span, and it sets the pill's height
// when it is taller than the text.
static void theIconLeadsThePill() {
  const LRect card{100.0, 200.0, 400.0, 300.0};
  const auto plain = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, false, 200.0),
                                   style());
  near(plain.plate.w, 200.0 + 2.0 * 11.0, "no icon, the pill fits the text");

  auto icon = sizes(true, false, 200.0);
  icon.iconW = 24.0;
  icon.hasIcon = true;
  const auto withIcon = captionLayout(card, LRect{}, 1920.0, 1080.0, icon, style());
  near(withIcon.plate.w - plain.plate.w, 24.0 + 6.0, "the icon widens the pill by icon and gap");
  near(withIcon.kFit, 1.0, "a title that fits is not shrunk for the icon's sake");
  near(withIcon.contentX, plain.contentX, "the icon leads from the pill's padding");
  near(withIcon.plate.y, plain.plate.y, "and a short icon does not change the height");

  icon.iconW = 36.0;
  const auto tall = captionLayout(card, LRect{}, 1920.0, 1080.0, icon, style());
  near(tall.plate.h - plain.plate.h, 36.0 - 24.0,
       "a taller icon grows the pill by the difference");
  std::puts("the pill's icon: its own span wide, height when it is the tallest thing");
}

// The counter is a NUMBER about the run, and it stays outside the card, on its own edge: its
// own padding, its own gap from the card, its own screen margin.
static void theCounterIsTheOutsidePill() {
  const LRect card{100.0, 200.0, 400.0, 300.0};
  const auto lay = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, true), style());

  near(lay.counter.w, 112.0, "the counter pill is its text plus its own padding");
  near(lay.counter.h, 30.0, "and its height too");
  near(lay.counter.cx(), 960.0, "centred on the screen");
  near(lay.counter.y, card.y + card.h + 8.0, "the gap below the card is counter_gap");
  want(lay.counter.y >= card.y + card.h, true, "it is outside the card, not on it");
  want(lay.counter.y >= lay.plate.y + lay.plate.h, true,
       "and never over the bar: it hangs below the card");

  // The counter's edge is its own: the title plate does not follow it.
  const auto up = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, true),
                                style(/*counterBelow=*/false));
  near(up.counter.y + up.counter.h, card.y - 8.0, "on the top edge it hangs off the top");
  near(up.plate.y, lay.plate.y, "and the bar stays where it was");

  // A counter wider than the screen is centred rather than clamped to a negative inset.
  const auto huge = captionLayout(LRect{0.0, 0.0, 400.0, 300.0}, LRect{}, 100.0, 1080.0,
                                  CaptionSizes{10.0, 10.0, 90.0, 20.0, true, true},
                                  style());
  near(huge.counter.x, 50.0,
       "a counter wider than the screen keeps the screen's mid as its left edge, not a negative");
  std::puts("the counter: outside the card, its own padding, gap and edge");
}

// The strip band is part of the edge margin -- on its own edge only, which is why the bundled
// counter ended above the screen on the other edge in an earlier version.
static void theCounterKeepsOffTheBand() {
  const LRect band{0.0, 0.0, 1920.0, 100.0};
  const LRect card{100.0, 50.0, 400.0, 300.0};
  const auto up = captionLayout(card, band, 1920.0, 1080.0, sizes(true, true),
                                style(/*counterBelow=*/false));
  near(up.counter.y, 106.0, "a top band pushes the counter down by the band, plus its margin");

  const LRect bottomBand{0.0, 980.0, 1920.0, 100.0};
  const LRect low{100.0, 700.0, 400.0, 300.0};
  const auto down = captionLayout(low, bottomBand, 1920.0, 1080.0, sizes(true, true),
                                  style(/*counterBelow=*/true));
  near(down.counter.y + down.counter.h, 980.0 - 6.0,
       "a bottom band stops the counter its own margin short of itself");
  std::puts("the counter: the band's margin on the band's edge, and nowhere else");
}

// Nothing to say, nothing drawn: a zero-width plate is the signal, not a special case at the
// draw site. A card with no name at all is the counter's own bar.
static void nothingToLabel() {
  const LRect card{100.0, 200.0, 400.0, 300.0};

  const auto none = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(false, false), style());
  near(none.plate.w, 0.0, "no label and no counter means nothing to draw");
  near(none.counter.w, 0.0, "and no counter either");

  const auto counterOnly = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(false, true),
                                         style());
  want(counterOnly.plate.w > 0.0, true, "a counter with no title is the pill itself");
  near(counterOnly.plate.w, 90.0 + 2.0 * 11.0, "fitted to the counter text plus its padding");
  near(counterOnly.plate.h, 20.0 + 2.0 * 5.0, "as tall as the counter plus its padding");
  near(counterOnly.counter.w, 0.0, "and there is no second pill for it");
  near(counterOnly.plate.y + counterOnly.plate.h, card.y + card.h - 8.0,
       "inset in the card's bottom-left");
  std::puts("the bar: the counter alone, and nothing at all");
}

// The outside pill steps aside from a card that reaches it and is swallowed by one covering it whole.
static void theCounterStepsAsideAndLeaves() {
  const LRect home{600.0, 510.0, 112.0, 30.0};

  {
    // A card whose bottom edge crosses the counter's top: it steps DOWN and stays whole.
    const std::vector<LRect> covers{{520.0, 350.0, 300.0, 180.0}};
    const auto moved = placeCaption(CaptionInput{home, covers, true, 0.0, true});
    want(moved.box.y > home.y, true, "a counter under an overlapping card steps down");
    near(moved.scale, 1.0, "and is whole: it left the cover instead of being swallowed");
    want(moved.shove.y > 0.0, true, "and the shove it took is reported");
  }
  {
    // Held still (the run's centred card): no shove, whatever reaches it.
    const std::vector<LRect> covers{{520.0, 350.0, 300.0, 180.0}};
    const auto still = placeCaption(CaptionInput{home, covers, false, 0.0, true});
    sameRect(still.box, home, "a counter that may not slide stays where it was put");
  }
  {
    // Covered whole: gone. This is the state the old caption reached at its own width.
    const std::vector<LRect> covers{{500.0, 400.0, 400.0, 400.0}};
    const auto gone = placeCaption(CaptionInput{home, covers, true, 0.0, true});
    near(gone.scale, 0.0, "a counter a card has covered outright is gone");
  }
  {
    // Its card leaving takes it along, and never leaves it outliving the card.
    const std::vector<LRect> none{};
    near(placeCaption(CaptionInput{home, none, true, 0.5, false}).scale, 0.5,
         "half out with a half-gone card");
    near(placeCaption(CaptionInput{home, none, true, 1.0, false}).scale, 0.0,
         "gone when the card is gone");
  }
  std::puts("the counter: steps aside, holds still, swallowed, and leaves with its card");
}

// The run's cards shrink with distance, and the plate goes with its card.
static void thePlateIsSizedWithItsCard() {
  near(cardScale(692.0, 692.0), 1.0, "the centred card labels at full size");
  near(cardScale(526.0, 692.0), 526.0 / 692.0, "a neighbour scales with its card");
  near(cardScale(692.0, 0.0), 1.0, "no reference set means no scaling, not a division by zero");
  near(cardScale(0.0, 692.0), 1.0, "a zero-width card is not scaled");
  want(cardScale(40.0, 692.0) >= 0.45, true,
       "the scale is floored: a plate scaled to nothing is an absent plate");

  const LRect card{100.0, 200.0, 346.0, 300.0};
  CaptionStyle ref;
  ref.cardRefW = 692.0;
  ref.padX = 11.0;
  ref.padY = 5.0;
  const auto lay = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, false), ref);
  near(lay.kFit, 0.5, "a card half the centred card's width carries half-size text");
  near(lay.plate.w, 200.0 * 0.5 + 2.0 * 11.0 * 0.5, "and the pill scales with it");
  near(lay.plate.h, 34.0 * 0.5, "and half the height, so it stays in proportion");
  near(lay.plate.y + lay.plate.h, card.y + card.h - 8.0, "still inset from its bottom edge");
  std::puts("cardScale: reference, neighbours, no reference, and the floor");
}

// The strip leaves its band along the anchor axis, by the deepest penetration.
static void theStripLeavesItsBand() {
  const LRect bottom{0.0, 980.0, 1920.0, 100.0};
  const auto down = stripPush(bottom, Anchor::Bottom, std::vector<LRect>{{100.0, 900.0, 200.0, 300.0}});
  near(down.x, 0.0, "a bottom band is left downwards");
  near(down.y, 220.0, "by the penetration");

  const auto up = stripPush(LRect{0.0, 0.0, 1920.0, 100.0}, Anchor::Top,
                             std::vector<LRect>{{100.0, 50.0, 200.0, 300.0}});
  near(up.y, -50.0, "a top band is left upwards");

  const auto left = stripPush(LRect{0.0, 0.0, 100.0, 1080.0}, Anchor::Left,
                               std::vector<LRect>{{50.0, 0.0, 300.0, 100.0}});
  near(left.x, -50.0, "a left band is left to the left");

  const auto right = stripPush(LRect{1820.0, 0.0, 100.0, 1080.0}, Anchor::Right,
                                std::vector<LRect>{{1700.0, 0.0, 300.0, 100.0}});
  near(right.x, 180.0, "a right band is left to the right");

  // Nothing reaches the band: nothing moves.
  const auto clear = stripPush(bottom, Anchor::Bottom, std::vector<LRect>{{100.0, 100.0, 200.0, 300.0}});
  near(clear.x, 0.0, "a band nothing reaches stays put");
  near(clear.y, 0.0, "a band nothing reaches stays put");
  std::puts("the strip push: four edges, by the deepest penetration");
}

// A configured corner radius: negative is a capsule, and the cap is the load-bearing half.
static void theRadiusIsCapped() {
  near(pillRadius(-1.0, 200.0, 30.0), 15.0, "negative is a capsule on the short side");
  near(pillRadius(0.0, 200.0, 30.0), 0.0, "zero is square");
  near(pillRadius(8.0, 200.0, 30.0), 8.0, "a radius in range passes through");
  near(pillRadius(99.0, 200.0, 30.0), 15.0, "past the cap it clamps, it does not invert");
  near(pillRadius(-1.0, 0.0, 0.0), 0.0, "a degenerate box gets nothing, not a negative");
  std::puts("pillRadius: capsule, square, in-range, and clamped at half the short side");
}

int main() {
  thePillRepeatsItsContent();
  aLongTitleShrinksInThePill();
  theIconLeadsThePill();
  theCounterIsTheOutsidePill();
  theCounterKeepsOffTheBand();
  nothingToLabel();
  theCounterStepsAsideAndLeaves();
  thePlateIsSizedWithItsCard();
  theStripLeavesItsBand();
  theRadiusIsCapped();
  std::puts("caption geometry ok");
  return 0;
}

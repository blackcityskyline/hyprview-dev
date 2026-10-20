// Where a caption's pills land, and how far they leave.
//
// Six fixes in a row landed inline in drawTile -- shove fade, one factor for both
// pills, the band margin, the counter on the opposite edge, the screen clamp, the
// swallowed escape -- each a branch added at the draw site, where the only
// observer was the screen. This file pins those decisions as geometry: same
// header the plugin compiles, no GL, no Hyprland, no config.
//
// The rules worth pinning:
//   - the band is part of the edge margin, on its own edge only;
//   - the counter is the outer pill on the edge it shares with the title;
//   - a caption on a shrunk card is scaled with that card;
//   - a caption shoved out from under a card leaves along the cheaper axis, and a
//     swallowed one leaves through the nearest edge;
//   - the strip leaves its band along the anchor axis, by the deepest penetration.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/render/geometry.hpp"

using hyprview::LRect;
using hyprview::LVec2;
using hyprview::render::Anchor;
using hyprview::render::CaptionLayout;
using hyprview::render::CaptionSizes;
using hyprview::render::CaptionStyle;
using hyprview::render::captionLayout;
using hyprview::render::pushOut;
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

// One card, one label, one counter: the common case, so the other cases can state
// only what they change.
CaptionSizes sizes(bool label = true, bool counter = true) {
  return CaptionSizes{label ? 200.0 : 0.0, label ? 24.0 : 0.0,
                      counter ? 90.0 : 0.0, counter ? 20.0 : 0.0, label, counter};
}

CaptionStyle style(bool below = true, bool counterBelow = true) {
  return CaptionStyle{11.0, 5.0, 11.0, 5.0, 10.0, 8.0, 6.0, 6.0, 400.0, 34.0, below,
                      counterBelow};
}

} // namespace

// The band is part of the edge margin, and only on the edge it is drawn on.
static void theBandIsPartOfTheMargin() {
  // A card near the top edge: its caption above it is held off the edge by the
  // margin, and the band on that edge thickens the margin.
  const LRect high{100.0, 20.0, 400.0, 300.0};
  const auto top = captionLayout(high, LRect{0.0, 0.0, 1920.0, 60.0}, 1920.0, 540.0, sizes(),
                                  style(false, false));
  near(top.pill.y, 104.0, "a band at the top pushes a caption above the card down to its margin");
  const auto noBand = captionLayout(high, LRect{}, 1920.0, 540.0, sizes(), style(false, false));
  near(noBand.pill.y, 44.0, "without the band it sits on the plain margin");

  // A band at the bottom thickens the bottom margin: the caption under the card
  // is lifted off the band's edge by exactly the band's thickness.
  const LRect box{100.0, 200.0, 400.0, 300.0};
  const auto bottom = captionLayout(box, LRect{0.0, 480.0, 1920.0, 60.0}, 1920.0, 540.0,
                                     sizes(), style(true, true));
  const auto bare = captionLayout(box, LRect{}, 1920.0, 540.0, sizes(), style(true, true));
  near(bare.pill.y - bottom.pill.y, 60.0, "the band eats its own thickness off the margin");
  near(bottom.pill.y, 402.0, "and the caption is clamped to what is left");

  // A band on the far edge does not touch this one.
  const auto other = captionLayout(box, LRect{0.0, 0.0, 1920.0, 60.0}, 1920.0, 540.0, sizes(),
                                    style(true, true));
  near(other.pill.y, bare.pill.y, "a band on the other edge leaves this margin alone");
  std::puts("the band: its own edge only, and by its own thickness");
}

// One card, two pills: the counter is the outer one on the edge they share.
static void theCounterIsTheOuterPill() {
  const LRect box{100.0, 200.0, 400.0, 300.0};
  const auto lay = captionLayout(box, LRect{}, 1920.0, 1080.0, sizes(), style());

  sameRect(lay.pill, LRect{189.0, 510.0, 222.0, 34.0}, "the title pill");
  sameRect(lay.counter, LRect{904.0, 552.0, 112.0, 30.0}, "the counter pill");
  // The counter is further from the card than the title: a counter between the card
  // and the title reads as part of the card's own furniture.
  want(lay.counter.y - (box.y + box.h) > lay.pill.y - (box.y + box.h), true,
       "the counter is the outer pill");

  // On the opposite edge there is no order between them: each hangs off the card
  // alone, and each is clamped by its own margin.
  const auto split = captionLayout(box, LRect{}, 1920.0, 1080.0, sizes(), style(true, false));
  near(split.pill.y, 510.0, "the title stays under the card");
  near(split.counter.y, 160.0, "the counter goes over it");
  std::puts("the counter: outer on the shared edge, alone on the other one");
}

// A caption belongs to the card it labels, so it is scaled with that card.
static void aCaptionIsSizedWithItsCard() {
  const LRect big{100.0, 200.0, 400.0, 300.0};
  const LRect small{100.0, 200.0, 200.0, 300.0};

  const auto full = captionLayout(big, LRect{}, 1920.0, 1080.0, sizes(), style());
  near(full.pill.w, 222.0, "the label at the card's own size");

  // A neighbour on the run is smaller than the centred card, and its caption
  // shrinks with it: the same pill on a half-width card is a pill that does not fit.
  const auto shrunk = captionLayout(small, LRect{}, 1920.0, 1080.0, sizes(), style());
  near(shrunk.pill.w, 111.0, "the neighbour's caption is scaled with its card");
  near(shrunk.kFit, 0.5, "and the factor is the card's own");

  // A card wider than the screen still gets a caption that fits: the clamp centres
  // it rather than hanging it off the edge.
  const LRect wide{0.0, 0.0, 2500.0, 300.0};
  const auto over = captionLayout(wide, LRect{}, 1920.0, 1080.0,
                                  CaptionSizes{1900.0, 24.0, 90.0, 20.0, true, true}, style());
  near(over.pill.x, 960.0, "a pill wider than the screen is centred on it");
  std::puts("the caption: sized with its card, and clamped to the screen");
}

// The site's favicon rides inside the title's pill, so the pill grows by the icon and the
// gap between them; the text keeps its size, and the pill stays centred on the card.
static void theIconWidensTheTitlePill() {
  const LRect card{100.0, 100.0, 400.0, 300.0};
  const auto plain = captionLayout(card, LRect{}, 1920.0, 1080.0, sizes(true, false), style());
  auto withIcon = sizes(true, false);
  withIcon.iconW = 24.0;
  withIcon.hasIcon = true;
  const auto icon = captionLayout(card, LRect{}, 1920.0, 1080.0, withIcon, style());
  near(icon.pill.w - plain.pill.w, (24.0 + 6.0) * icon.kFit,
       "the pill grows by the icon and the gap, at the card's scale");
  near(icon.pill.x, plain.pill.x - (24.0 + 6.0) * icon.kFit / 2.0,
       "and stays centred on the card");
}

// A caption with no label shows the counter instead, and one with neither shows
// nothing at all -- a zero-width pill is the signal, not a special case.
static void aCaptionWithoutALabel() {
  const LRect box{100.0, 200.0, 400.0, 300.0};

  const auto counterOnly = captionLayout(box, LRect{}, 1920.0, 1080.0, sizes(false, true), style());
  near(counterOnly.pill.w, 112.0, "the counter takes the pill's place");
  near(counterOnly.counter.h, 0.0, "and there is no second pill");
  near(counterOnly.pill.y, 510.0, "the counter hangs off the card by the title gap");

  const auto none = captionLayout(box, LRect{}, 1920.0, 1080.0, sizes(false, false), style());
  near(none.pill.w, 0.0, "nothing to label means nothing to draw");
  std::puts("the caption: counter only, and nothing at all");
}

// Out from under a card: the cheaper axis, and the nearest edge when there is none.
static void aCaptionLeavesACardThatReachedIt() {
  const LRect card{400.0, 300.0, 300.0, 200.0};

  // Nothing covers it: no push.
  const LRect free{380.0, 480.0, 340.0, 40.0};
  const auto none = pushOut(free, std::vector<LRect>{});
  near(none.x, 0.0, "no cover, no push");
  near(none.y, 0.0, "no cover, no push");

  // Covered from below: the caption goes down, by the vertical overlap.
  const auto down = pushOut(free, std::vector<LRect>{card});
  near(down.x, 0.0, "it leaves vertically");
  near(down.y, 20.0, "by the vertical overlap");

  // The deepest cover wins, whatever order they arrive in: the first one here is
  // the shallower of the two.
  const LRect shallow{370.0, 450.0, 360.0, 100.0};
  const auto deepest = pushOut(free, std::vector<LRect>{shallow, card});
  near(deepest.y, 20.0, "the deeper cover decides the push");
  near(deepest.x, 0.0, "and it is the deeper cover's axis");

  // Swallowed: the cover is bigger than the caption on both axes, so there is no
  // cheaper way out and the nearest edge is the way.
  const LRect small{450.0, 330.0, 100.0, 60.0};
  const auto out = pushOut(small, std::vector<LRect>{card});
  near(out.x, 0.0, "a swallowed caption leaves through the nearest edge");
  near(out.y, -30.0, "and that edge is the cover's nearest one");
  std::puts("the push: cheaper axis, deepest cover, nearest edge when swallowed");
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

int main() {
  theBandIsPartOfTheMargin();
  theCounterIsTheOuterPill();
  aCaptionIsSizedWithItsCard();
  aCaptionWithoutALabel();
  theIconWidensTheTitlePill();
  aCaptionLeavesACardThatReachedIt();
  theStripLeavesItsBand();
  std::puts("caption geometry ok");
  return 0;
}

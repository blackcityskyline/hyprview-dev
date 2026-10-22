// Strip::cardAt -- the box a workspace card is drawn and hit-tested at.
//
// It moved here from render/geometry.hpp because the card's box is the STRIP's, not the
// builder's: buildStrip assigns it, eleven files ask for it, and the only two numbers that
// displace it are the band's slide and the group's scroll.
//
// model/strip.hpp needs no compositor, so this compiles with nothing but the standard
// library.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/model/strip.hpp"

using hyprview::LRect;
using hyprview::LVec2;
using hyprview::model::Strip;
using hyprview::model::StripItem;

namespace {

int g_checks = 0;

void sameRect(const LRect &got, const LRect &want, const char *what) {
  ++g_checks;
  if (std::fabs(got.x - want.x) >= 1e-9 || std::fabs(got.y - want.y) >= 1e-9 ||
      std::fabs(got.w - want.w) >= 1e-9 || std::fabs(got.h - want.h) >= 1e-9) {
    std::printf("FAIL %s: got {%g %g %g %g}, want {%g %g %g %g}\n", what, got.x, got.y,
                got.w, got.h, want.x, want.y, want.w, want.h);
    std::exit(1);
  }
}

// One card with a known laid-out box. buildStrip assigns `.card`; this is that field alone.
Strip withCards(const LRect &card) {
  Strip s;
  StripItem it;
  it.id     = 1;
  it.card   = card;
  it.active = true;
  s.add(std::move(it));
  return s;
}

} // namespace

int main() {
  const auto card = LRect{100, 40, 167, 94};
  const auto s    = withCards(card);

  // Slide and scroll add, and they are independent: the band slides vertically while the
  // card group scrolls horizontally, and neither may fold into the other.
  sameRect(s.cardAt(0, {0, 0}, {0, 0}), card,
           "no slide, no scroll is the laid-out box");
  sameRect(s.cardAt(0, {0, -30}, {0, 0}), LRect{100, 10, 167, 94}, "slide alone");
  sameRect(s.cardAt(0, {0, 0}, {-64, 0}), LRect{36, 40, 167, 94}, "scroll alone");
  sameRect(s.cardAt(0, {0, -30}, {-64, 0}), LRect{36, 10, 167, 94}, "both");

  // The size never changes. A card that resized with the scroll would make the scroll read
  // as a zoom.
  const auto moved = s.cardAt(0, {5, 5}, {-64, 12});
  ++g_checks;
  if (std::fabs(moved.w - 167.0) >= 1e-9 || std::fabs(moved.h - 94.0) >= 1e-9) {
    std::puts("FAIL slide or scroll changed the card's size");
    std::exit(1);
  }

  // Out of range is an EMPTY box, not the last card and not a throw. Hit-testing runs
  // against a strip that is moving, and "no card there" is an answer the caller asks for on
  // purpose -- a card that wrapped the index would send a drag to the wrong workspace.
  sameRect(s.cardAt(1, {0, 0}, {0, 0}), LRect{0, 0, 0, 0}, "one past the end");
  sameRect(s.cardAt(9999, {-500, -500}, {0, 0}), LRect{0, 0, 0, 0}, "far past the end");

  // An empty strip answers the same way, and does not read the card out of nothing.
  const Strip empty;
  sameRect(empty.cardAt(0, {0, -30}, {-64, 0}), LRect{0, 0, 0, 0}, "an empty strip");

  std::printf("strip cardAt: %d checks\n", g_checks);
  std::puts("strip ok");
  return 0;
}
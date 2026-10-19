// Where a card starts. model/origin.hpp as arithmetic.
//
// This is the decision that produced every visual bug of the tab work, and it had five
// answers in five files. It is here because it is the one thing a NEW subject kind gets
// wrong: the rules disagreed on purpose -- a window has a place on the desktop, a tab is a
// slot inside one and has none -- and the disagreement was a hidden boolean at each site.
// A new kind that got the boolean wrong flew in from a box it was never at, or grew out
// of the top left corner, or swept the strip band across the screen on the way.
//
// Every case below is one of those bugs, written so that re-introducing it turns this
// file red. The whole file is 40 lines of pure arithmetic, so it runs in the same second
// as the layout check and knows nothing about the compositor.

#include "model/origin.hpp"

#include <cstdio>

using namespace hyprview;
using hyprview::model::isPlaced;
using hyprview::model::noPlace;
using hyprview::model::outgoingBox;
using hyprview::model::priorSlot;

namespace {

int failures = 0;

void expect(const char *what, const LRect &got, const LRect &want) {
  if (got.w == want.w && got.h == want.h && got.x == want.x && got.y == want.y)
    return;
  std::printf("FAIL %s\n  got  {%g,%g %gx%g}\n  want {%g,%g %gx%g}\n", what, got.x,
              got.y, got.w, got.h, want.x, want.y, want.w, want.h);
  ++failures;
}

// The places that exist, spelled once so no case can quietly invent a third.
constexpr LRect kDesktop{0, 0, 1354, 756};   // a window's box on the desktop
constexpr LRect kSlot{400, 300, 320, 180};     // a card's box on the run
constexpr LRect kLeft{0, 200, 100, 180};      // the slot it is leaving
constexpr LRect kNext{760, 300, 320, 180};    // the slot it is taking
constexpr LRect kCell{40, 40, 600, 400};      // a grid cell: off the run
constexpr LRect kNothing{};                   // off-stage, or never placed

// 1. A card that WAS somewhere starts where it was, and the desktop it had before the
// overlay opened is not it. Dropping this rule -- returning ownPlace for a window -- is
// what makes a windowed card fly across the screen instead of one slot along the run.
void cardWithASlotStartsAtIt() {
  expect("window on the run glides from the slot it left, not the desktop",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/true, /*revealed=*/true, kLeft, kDesktop, kNext), kLeft);
  expect("tab on the run glides from the slot it left too",
         outgoingBox(/*hasOwnPlace=*/false, /*onRun=*/true, /*revealed=*/true, kLeft, noPlace(), kNext), kLeft);
}

// 2. A window JOINING the run has no slot, and the fly-in from its desktop box is the
// signature of the window carousel. Pinning every card to its target took it away, and the
// fix that took it away was this rule and not the tab rule it was meant to be.
void windowJoiningTheRunFliesInFromItsOwnPlace() {
  expect("window joining the run has no slot, so it comes from the desktop",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/true, /*revealed=*/true, noPlace(), kDesktop, kSlot), kDesktop);
}

// 3. A tab is a slot inside a window and has no place of its own, so the desktop box the
// build seeded it with must not survive. This is the bug that drew a 1354x756 card for a
// tab and sent it in from full screen, and the tab's answer has to be the same whatever
// happens to sit in ownPlace -- that is what "no place of its own" MEANS.
void tabNeverStartsFromTheDesktop() {
  expect("tab on the run starts at its slot, not at the seeded desktop box",
         outgoingBox(/*hasOwnPlace=*/false, /*onRun=*/true, /*revealed=*/true, noPlace(), kDesktop, kSlot), kSlot);
  expect("a tab's answer does not depend on ownPlace at all",
         outgoingBox(/*hasOwnPlace=*/false, /*onRun=*/true, /*revealed=*/true, noPlace(), kDesktop, kSlot),
         outgoingBox(/*hasOwnPlace=*/false, /*onRun=*/true, /*revealed=*/true, noPlace(), noPlace(), kSlot));
  expect("tab off the run is off-stage, not parked on the desktop",
         outgoingBox(/*hasOwnPlace=*/false, /*onRun=*/false, /*revealed=*/true, noPlace(), kDesktop, kNothing),
         kNothing);
}

// 4. Off the run nothing flies. The grid and the desktop lay cards out where they belong;
// the desktop box is a place the card has without the overlay, not a place to animate it
// from while the overlay is up.
void offTheRunCardsAppearWhereTheyBelong() {
  expect("window off the run appears in its cell",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/false, /*revealed=*/true, noPlace(), kDesktop, kCell), kCell);
  expect("a window off the run and off-stage is nowhere, not on the desktop",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/false, /*revealed=*/true, noPlace(), kDesktop, kNothing),
         kNothing);
}

// 5. A card that JOINS a scene already on screen did not come from the desktop, whatever
// box it has there. It was not in the picture a moment ago, so the desktop is a place it is
// about to have, not one it is leaving. This is the window that flew out at 1354x756 when
// all-workspaces was toggled and shrank into its slot while its sibling slid one slot along.
void aJoiningCardStartsAtItsSlot() {
  expect("a window joining an already-shown run does NOT fly from the desktop",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/true, /*revealed=*/false, noPlace(),
                     kDesktop, kSlot),
         kSlot);
  expect("the same window DOES fly from the desktop when the overlay reveals the scene",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/true, /*revealed=*/true, noPlace(),
                     kDesktop, kSlot),
         kDesktop);
  // A card that WAS on screen still glides from where it was, revealed or not.
  expect("reveal does not override a real prior slot",
         outgoingBox(/*hasOwnPlace=*/true, /*onRun=*/true, /*revealed=*/true, kLeft, kDesktop,
                     kNext),
         kLeft);
}

// 6. `natural` is evidence for a window and is not evidence for a tab -- that inversion is
// the whole boolean, and three sites got it backwards. A tab whose natural was seeded with
// the browser window's box must still read its prior slot from the run.
void naturalIsEvidenceForAWindowAndNotForATab() {
  expect("a window's prior slot is its natural, whatever the target says",
         model::priorSlot(/*hasOwnPlace=*/true, /*wasOnRun=*/true, kLeft, kNext), kLeft);
  expect("a tab's prior slot is its target, never its seeded natural",
         model::priorSlot(/*hasOwnPlace=*/false, /*wasOnRun=*/true, kDesktop, kNext), kNext);
  expect("a tab that was not on the run has no prior slot",
         model::priorSlot(/*hasOwnPlace=*/false, /*wasOnRun=*/false, kDesktop, kNext), kNothing);
  expect("a window keeps its natural even when it was off the run",
         model::priorSlot(/*hasOwnPlace=*/true, /*wasOnRun=*/false, kDesktop, kNext), kDesktop);
}

// 6. The property the cases above are instances of, over every input the callers can
// produce: a card NEVER starts at the origin while it has a real box to end at. That is
// the growth out of the top left corner -- the animation everybody can see on every step
// -- and it is what an empty natural interpolated from. Exhaustive, so a rule added later
// cannot reintroduce it through a case nobody thought to name.
void nothingGrowsOutOfTheCorner() {
  int checked = 0;
  // Two spellings of "has a box": every axis is a real input the callers can produce, so
  // this covers all 32 and not the 16 that read better in the count below.
  for (const bool hasOwn : {false, true})
    for (const bool onRun : {false, true})
      for (const bool revealed : {false, true})
        for (const LRect &prior : {kNothing, kLeft})
          for (const LRect &own : {kNothing, kDesktop})
            for (const LRect &target : {kNothing, kSlot}) {
              const LRect got = outgoingBox(hasOwn, onRun, revealed, prior, own, target);
            if (!isPlaced(got) && isPlaced(target)) {
              std::printf("FAIL grows out of the corner: hasOwn=%d run=%d prior=%g own=%g\n",
                          int(hasOwn), int(onRun), prior.w, own.w);
              ++failures;
            }
            ++checked;
          }
  if (checked != 64)
    std::printf("FAIL exhaustive sweep covered %d of 64\n", checked), ++failures;
}

} // namespace

int main() {
  cardWithASlotStartsAtIt();
  windowJoiningTheRunFliesInFromItsOwnPlace();
  tabNeverStartsFromTheDesktop();
  offTheRunCardsAppearWhereTheyBelong();
  aJoiningCardStartsAtItsSlot();
  naturalIsEvidenceForAWindowAndNotForATab();
  nothingGrowsOutOfTheCorner();

  if (failures) {
    std::printf("origin_check: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("origin_check: ok\n");
  return 0;
}
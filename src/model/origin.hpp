#pragma once

#include "../layout.hpp"

// Where a card starts when the layout moves it. One question, one answer.
//
// Every rebuild asked this and answered it in five places with five rules. buildTiles
// seeded a natural, startTileGlide overwrote it or deferred to the capture, stepAltTab
// pinned the tabs to the slot they occupied, pinRunEdges pinned the tabs that joined or
// left the run, and Placement::box caught whatever fell through to an empty natural. The
// rules disagreed on purpose and the disagreement was a hidden boolean: a WINDOW has a
// box on the desktop to fly from and a TAB is a slot inside one and has none. Adding a
// subject kind meant finding all five sites and getting that boolean right in each, and
// every visual bug of the past weeks was one of the five getting it wrong:
//
//   - buildTiles handed a tab the browser window's box, so a tab card was 1354x756 and
//     flew in from full screen.
//   - stepAltTab and pinRunEdges did not know a tab has no prior box, so a card joining
//     the run had nothing to glide from and grew out of the top left corner.
//   - Placement::box interpolated from an empty natural, which is the origin, so a tab
//     that reached it unsorted grew from the corner as well.
//
// The boolean is the whole mechanism, so it is the whole signature. hasOwnPlace is
// cardOf(t).hasWindow() at the call site: a window is a place on the desktop, a tab is
// not.
//
// The rules, in the order they are asked:
//
//   1. A card that WAS somewhere starts where it was. priorSlot wins over everything,
//      including on the desktop -- a card on the run glides from the slot it left, not
//      from the box it had before the overlay opened.
//   2. Off the run there is no flight: the grid and the desktop lay cards out in place,
//      and a window's desktop box is not a source it should fly from there.
//   3. A card joining the run with no slot glides in from its own place. This is the
//      classic window fly-in, and pinning every card to its target took it away.
//   4. Anything else -- a tab, which has exactly one place it has ever been -- starts at
//      its target. Including an empty one: a card off the run has a zeroed target and is
//      not drawn, and starting it from a desktop box it was never at is the bug this
//      function exists to make unrepresentable.
//
// No clock, no curve, no window handle: this is arithmetic over boxes, so it compiles
// and runs without hyprland/ and is reachable from a test.
namespace gloview::model {

// A subject with no place of its own outside the overlay. A tab is a slot inside a
// window; the seed every caller passes as a window's natural until a real one replaces it.
[[nodiscard]] constexpr LRect noPlace() { return LRect{}; }

[[nodiscard]] constexpr bool isPlaced(const LRect &b) { return b.w >= 1.0; }

// The box the card occupied a moment ago. Empty when it has never been on the run.
//
// This is where the boolean bites, and it is a separate function because three sites got it
// wrong by asking the wrong question of `natural`:
//
//   - A WINDOW's natural IS its prior slot. startTileGlide set it from the capture, and
//     layoutTiles has since moved only the target. So read it.
//   - A TAB's natural is whatever the build seeded, which is a box the tab was never at --
//     the browser window's box -- so it is NOT evidence. Its prior slot is the target it is
//     about to be replaced by, and only while it was on the run to have one. Reading natural
//     here is the bug that sent a tab card in at the size of the whole browser.
//
// `wasOnRun` is whether the card held a slot before this layout. Taken BEFORE any target is
// written, at every call site, because afterwards the question has no answer left.
[[nodiscard]] constexpr LRect priorSlot(bool hasOwnPlace, bool wasOnRun,
                                        const LRect &natural, const LRect &target) {
  if (hasOwnPlace)
    return natural;
  return wasOnRun ? target : noPlace();
}

// priorSlot: the box the card occupied a moment ago, empty if it has never been on the
// run. ownPlace: the place the subject has without the overlay, empty when it has none.
// target: where it is going.
[[nodiscard]] constexpr LRect outgoingBox(bool hasOwnPlace, bool onRun,
                                           const LRect &priorSlot,
                                           const LRect &ownPlace,
                                           const LRect &target) {
  if (isPlaced(priorSlot))
    return priorSlot;
  if (!onRun)
    return target;
  if (hasOwnPlace && isPlaced(ownPlace))
    return ownPlace;
  return target;
}

} // namespace gloview::model
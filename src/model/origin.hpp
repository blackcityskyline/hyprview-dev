#pragma once

// Where a card starts when the layout moves it. Five rebuild paths used to answer this
// separately and disagreed; the disagreement was one hidden boolean each -- does this subject
// have a place that exists without the overlay -- so the boolean is the whole signature.
// Rules, in order: a card that WAS somewhere starts there; off the run nothing flies; a card
// being revealed glides in from its own place; a card JOINING a shown scene starts at its
// slot; anything else starts at its target. Pure arithmetic over boxes, so ctest reaches it.

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
//   3. A card that is being REVEALED glides in from its own place. That is the classic
//      window fly-in -- the windows lifting off the desktop when the overlay opens -- and
//      pinning every card to its target took it away. Revealed means nothing was captured,
//      so every card is arriving at once and the desktop is the one place they all came
//      from.
//   4. A card that JOINS a scene already on screen did not come from the desktop, however
//      true it is that it has a box there: it was not in the picture a moment ago, so the
//      desktop is a place it is ABOUT to have, not one it is leaving. Toggling all-workspaces
//      put a second window into a run whose first window was already in its slot, and that
//      one flew out at 1354x756 and shrank into place while its sibling slid one slot along
//      (measured: a=6,6,1354,756 b=336,227,692,389, so the card was nearly the monitor on
//      its first frame). It starts at its slot.
//   5. Anything else -- a tab, which has exactly one place it has ever been -- starts at
//      its target. Including an empty one: a card off the run has a zeroed target and is
//      not drawn, and starting it from a desktop box it was never at is the bug this
//      function exists to make unrepresentable.
//
// No clock, no curve, no window handle: this is arithmetic over boxes, so it compiles
// and runs without hyprland/ and is reachable from a test.
namespace gloview::model {

// A subject with no place of its own outside the overlay: a tab, which is a slot inside a
// window. The seed a window's natural gets until a real one replaces it.
[[nodiscard]] constexpr LRect noPlace() { return LRect{}; }

[[nodiscard]] constexpr bool isPlaced(const LRect &b) { return b.w >= 1.0; }

// The box the card occupied a moment ago, empty if it has never been on the run. A window's
// natural IS its prior slot (startTileGlide set it from the capture, layoutTiles has since
// moved only the target). A tab's natural is whatever the build seeded -- the browser window's
// box -- so its prior slot is the target it is about to be replaced by, and only while it was
// on the run to have one. wasOnRun is read BEFORE any target is written.
[[nodiscard]] constexpr LRect priorSlot(bool hasOwnPlace, bool wasOnRun,
                                        const LRect &natural, const LRect &target) {
  if (hasOwnPlace)
    return natural;
  return wasOnRun ? target : noPlace();
}

// priorSlot: the box the card occupied a moment ago, empty if it has never been on the
// run. ownPlace: the place the subject has without the overlay, empty when it has none.
// target: where it is going.
// revealed: nothing was captured, so the card arrives with the rest of the scene and the
// desktop is where all of them came from. Passed in because it is a property of the capture --
// one fact about the frame, not one per card.
[[nodiscard]] constexpr LRect outgoingBox(bool hasOwnPlace, bool onRun, bool revealed,
                                           const LRect &priorSlot,
                                           const LRect &ownPlace,
                                           const LRect &target) {
  if (isPlaced(priorSlot))
    return priorSlot;
  if (!onRun)
    return target;
  if (revealed && hasOwnPlace && isPlaced(ownPlace))
    return ownPlace;
  return target;
}

} // namespace gloview::model
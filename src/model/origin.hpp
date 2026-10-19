#pragma once

// Where a card starts when the layout moves it. Five rebuild paths used to answer this
// separately and disagreed; the disagreement was one hidden boolean each -- does this subject
// have a place that exists without the overlay -- so the boolean is the whole signature.
// Rules, in order: a card that WAS somewhere starts there; off the run nothing flies; a card
// being revealed glides in from its own place; a card JOINING a shown scene starts at its
// slot; anything else starts at its target. Pure arithmetic over boxes, so ctest reaches it.

#include "../layout.hpp"

namespace gloview::model {

// A subject with no place of its own outside the overlay: a tab, which is a slot inside a
// window. The seed a window's natural gets until a real one replaces it.
[[nodiscard]] constexpr LRect noPlace() { return LRect{}; }

[[nodiscard]] constexpr bool isPlaced(const LRect &b) { return b.w >= 1.0; }

// The box the card occupied a moment ago, empty if it has never been on the run. A window's
// natural IS its prior slot (startTileGlide set it from the capture; layoutTiles has since
// moved only the target). A tab's natural is the seeded browser-window box, so its prior slot
// is the target it is about to be replaced by. wasOnRun is read BEFORE any target is written.
[[nodiscard]] constexpr LRect priorSlot(bool hasOwnPlace, bool wasOnRun,
                                        const LRect &natural, const LRect &target) {
  if (hasOwnPlace)
    return natural;
  return wasOnRun ? target : noPlace();
}

// priorSlot: the box the card occupied a moment ago, empty if it has never been on the
// run. ownPlace: the place the subject has without the overlay, empty when it has none.
// target: where it is going. revealed: nothing was captured, so the desktop is where the
// scene came from -- a property of the capture, passed in, not one per card.
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
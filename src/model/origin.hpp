#pragma once

// Where a card starts when the layout moves it: a card that WAS somewhere starts there, off
// the run nothing flies, a revealed card glides in from its own place, a joining card starts
// at its slot, anything else at its target. Pure arithmetic over boxes, so ctest reaches it.

#include "../layout.hpp"

namespace hyprview::model {

// A subject with no place of its own outside the overlay: a tab is a slot inside a window.
[[nodiscard]] constexpr LRect noPlace() { return LRect{}; }

[[nodiscard]] constexpr bool isPlaced(const LRect &b) { return b.w >= 1.0; }

// A window's natural IS its prior slot; a tab's natural is the seeded browser-window box, so
// its prior slot is the target it is about to be replaced by. wasOnRun is read BEFORE any
// target is written.
[[nodiscard]] constexpr LRect priorSlot(bool hasOwnPlace, bool wasOnRun,
                                        const LRect &natural, const LRect &target) {
  if (hasOwnPlace)
    return natural;
  return wasOnRun ? target : noPlace();
}

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

} // namespace hyprview::model

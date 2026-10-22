#pragma once

// Where a card starts when the layout moves it: a card that WAS somewhere starts there, off
// the run nothing flies, a joining card starts at its slot, anything else at its target.

#include "../layout.hpp"
#include "subject.hpp"

#include <cmath>
#include <utility>
#include <vector>

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

// Where every tile of a rebuilt deck starts (AGENTS.md 2.29): a card that WAS on
// screen starts where it was, a card that joins a shown scene starts at its slot.
struct TileStart {
  SubjectRef subj;
  LRect ownPlace = {};
  LRect target = {};
  bool hasWindow = false;
};

struct StartBox {
  LRect natural;
  double landed = 1.0;
};

// seamRunW: the width a card may cross before it is the ring's seam and not a step. A seam
// card starts where it lands -- sliding it across the run is the card flying over the row.
[[nodiscard]] inline std::vector<StartBox> startBoxes(
    const std::vector<std::pair<SubjectRef, LRect>> &oldBoxes,
    const std::vector<TileStart> &tiles, bool onRun, double seamRunW = 0.0) {
  std::vector<StartBox> out;
  out.reserve(tiles.size());
  for (const auto &t : tiles) {
    const LRect *drawn = nullptr;
    for (const auto &[oldSubj, oldBox] : oldBoxes)
      if (oldSubj == t.subj) {
        drawn = &oldBox;
        break;
      }
    LRect natural = outgoingBox(t.hasWindow, onRun, /*revealed=*/oldBoxes.empty(),
                                drawn ? *drawn : noPlace(), t.ownPlace, t.target);
    if (onRun && seamRunW > 0.0 && std::abs(natural.x - t.target.x) > seamRunW * 0.5)
      natural = t.target;
    out.push_back(StartBox{natural, drawn ? 1.0 : 0.0});
  }
  return out;
}

} // namespace hyprview::model

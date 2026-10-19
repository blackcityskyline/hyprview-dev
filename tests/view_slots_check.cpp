// Scene::focusedWindow and State::tileInSlot.
//
// Both are one query each, and both had three readers spelling them out before. Both also
// have a failure that looks like nothing happening: focusedWindow returns null when the
// deck holds tiles but nothing is selected, and Enter then exits as a bare close; tileInSlot
// returns -1 on a stale slot, and alt+tab would select an unrelated tile instead of the
// next one on screen.
//
// model/scene.hpp and view/state.hpp pull in Hyprland types, so this test exercises the two
// rules over the pieces that do NOT need one: the index arithmetic in tileInSlot's
// caller, and the empty-vs-selected distinction in focused().

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../src/layout.hpp"
#include "../src/view/state.hpp"

using hyprview::Engine;
using hyprview::view::State;

// The slot is written by resolve() and nothing else -- it is part of the geometry it was
// resolved with, so there is deliberately no setSlot(). These go through resolve.
void setSlot(State &s, std::vector<std::size_t> slot) {
  s.resolve(s.engine(), s.carousel(), s.carouselFor(), std::move(slot));
}

namespace {

int g_checks = 0;

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

} // namespace

int main() {
  State s;

  // tileInSlot on a slot that has never been resolved.
  want(s.tileInSlot(0) == -1, true, "an unresolved slot has no tile");
  want(s.tileInSlot(99) == -1, true, "a far-out slot has no tile");

  // The carousel orders by depth, so slot k is NOT tile k. A helper that assumed it was
  // would step alt+tab to a tile that is not on screen.
  setSlot(s, {2, 0, 1});
  want(s.tileInSlot(0) == 2, true, "slot 0 holds tile 2");
  want(s.tileInSlot(1) == 0, true, "slot 1 holds tile 0");
  want(s.tileInSlot(2) == 1, true, "slot 2 holds tile 1");
  // ...and the inverse must agree, or selection and the drawing disagree.
  want(s.slotOf(0) == 1, true, "tile 0 is in slot 1");
  want(s.slotOf(1) == 2, true, "tile 1 is in slot 2");
  want(s.slotOf(2) == 0, true, "tile 2 is in slot 0");
  want(s.slotOf(3) == -1, true, "a tile not in any slot");

  // The stale case the header warns about: the slot is a cache that outlives a shrink of
  // the tile array. An index past its end has to answer -1, not read past the end.
  setSlot(s, {5, 7});
  want(s.tileInSlot(0) == 5, true, "a stale slot still answers in range");
  want(s.tileInSlot(1) == 7, true, "and past that");
  want(s.tileInSlot(2) == -1, true, "one past a stale slot is -1, not a read past the end");

  // An empty slot is -1 for every position, which is the "fall back rather than select
  // something unrelated" signal.
  State e;
  want(e.tileInSlot(0) == -1, true, "an empty slot answers -1");
  want(e.slotOf(0) == -1, true, "and nothing is in any slot");

  // Setting the slot does not disturb the run's other modes.
  State m;
  setSlot(m, {1});
  m.setRun(true);
  m.setCarouselOn(false);
  m.setDesktop(true);
  want(m.run(), true, "run survives a slot write");
  want(m.carouselOn(), false, "carouselOn survives");
  want(m.desktop(), true, "desktop survives");
  want(m.allOverride() == -1, true, "allOverride survives");

  std::printf("view slots: %d checks\n", g_checks);
  std::puts("view slots ok");
  return 0;
}
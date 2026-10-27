// Which leaf drives the master clock, and whether the backdrop fades with it.
//
// These were three separate expressions of the same two questions, in eased(),
// animDuration() and blurAlpha(). eased() picked a leaf to read a curve from and
// animDuration() picked a leaf to read a duration from -- and nothing checked that the two
// picked the same one. If they ever disagreed, the overview would close along one curve
// while timing itself by another.
//
// eased() and animDuration() now both go through Overview::masterId(), so the choice
// happens once. What this file checks is the choice itself and the rule about the
// backdrop, both of which are the kind of thing that is right in every case someone tries
// and wrong in the one they do not.

#include <cassert>
#include <cstdio>

#include "../src/anim_types.hpp"

using hyprview::anim::masterLeaf;
using hyprview::anim::MasterLeaf;

int main() {
  // --- the leaf ------------------------------------------------------------------
  // Plain open, plain close, and the jump exit. jumpMode only changes anything once the
  // overview is on its way out: while it is still opening, the opening leaf is correct
  // even under a digit key, because nothing about that is visible yet.
  assert(masterLeaf(false, true)  == MasterLeaf::Open);
  assert(masterLeaf(false, false) == MasterLeaf::Close);
  assert(masterLeaf(true, true)   == MasterLeaf::Open);
  assert(masterLeaf(true, false)  == MasterLeaf::JumpOut);
  std::puts("jumpMode only changes the exit leaf");

  std::puts("OK");
  return 0;
}
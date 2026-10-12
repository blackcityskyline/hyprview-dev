// debug_trace is a mask, and a mask that selects nothing is indistinguishable from a
// trace that was never switched on -- the file is simply absent, and the natural reading
// of an absent file is "nothing happened", not "you asked for the wrong bit".
//
// That is not hypothetical. Debugging a drag release used to require reading four hundred
// SCROLL records a second to find the one DRAG line, so the gate became a mask. The
// regression this file exists to prevent is a family silently dropping out of it, or
// `1` -- the value every earlier doc called a boolean -- quietly meaning one family.

#include <cassert>
#include <cstdio>

#include "../src/debug/log.hpp"

using gloview::debug::trace::fam::kAll;
using gloview::debug::trace::fam::kAnim;
using gloview::debug::trace::fam::kDrag;
using gloview::debug::trace::fam::kLife;
using gloview::debug::trace::fam::kScroll;
using gloview::debug::trace::fam::kSwap;
using gloview::debug::trace::selects;

namespace {
constexpr int kBits[] = {kAnim, kDrag, kLife, kScroll, kSwap};
} // namespace

int main() {
  // --- off is off, for every family -------------------------------------------------
  {
    for (const int bit : kBits)
      assert(!selects(0, bit));
    std::puts("gate 0 selects nothing");
  }

  // --- 1 predates the mask and must keep meaning all of it --------------------------
  //
  // Every example in the README and every run from before the mask used 1 as a boolean.
  // If it stopped meaning "everything", every recorded trace would silently lose four
  // families and no test would notice, because the records would still look plausible.
  {
    for (const int bit : kBits)
      assert(selects(1, bit));
    assert(selects(1, kAll));
    std::puts("gate 1 still selects every family");
  }

  // --- one bit selects exactly its own family ---------------------------------------
  {
    assert(selects(2, kDrag));
    assert(!selects(2, kAnim));
    assert(!selects(2, kLife));
    std::puts("a single bit selects one family and no other");
  }

  // --- combinations are unions, never subtractions -----------------------------------
  {
    // 6 = DRAG|LIFE, the "I am chasing a release" mask.
    assert(selects(6, kDrag) && selects(6, kLife));
    assert(!selects(6, kAnim) && !selects(6, kScroll) && !selects(6, kSwap));

    // Everything except SCROLL: 1|2|4|16 = 23.
    assert(selects(23, kAnim) && selects(23, kDrag) && selects(23, kLife) && selects(23, kSwap));
    assert(!selects(23, kScroll));
    std::puts("masks combine as unions");
  }

  // --- the bits are distinct and cover the documented set -----------------------------
  {
    const int all = kAnim | kDrag | kLife | kScroll | kSwap;
    assert(all == 31);
    assert(kAnim == 1 && kDrag == 2 && kLife == 4 && kScroll == 8 && kSwap == 16);
    // Every bit in the set is claimed by exactly one family, so nothing is unreachable.
    for (int i = 0; i < 5; ++i) {
      const int bit = 1 << i;
      int owners    = 0;
      for (const int f : kBits)
        if (f == bit)
          ++owners;
      assert(owners == 1);
    }
    std::puts("the five bits are distinct, documented values, and none is unreachable");
  }

  // --- an unrecognised family is never suppressed -------------------------------------
  //
  // kAll is what record() falls back to for a tag it does not recognise. A record nobody
  // can name is exactly the one worth seeing, so it passes through whatever the mask is.
  {
    for (const int gate : {1, 2, 6, 16, 31})
      assert(selects(gate, kAll));
    std::puts("an unrecognised record family survives every mask");
  }

  std::puts("OK");
  return 0;
}
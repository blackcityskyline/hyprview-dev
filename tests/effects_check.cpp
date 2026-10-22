// The effects are the model's invariants, and they were untested because the file that holds
// them also held a Hyprland type: the list of workspaces this overview made persistent. That
// list is session state, not an effect, and it now lives with its owner -- so the effects are
// a header a test can include without a compositor.
//
// Two rules are worth a red test, because both were a bug once: a pulse is REPLACED and not
// stacked (pulses accrue per frame, so a second ring on the same subject reads as a double
// ring), and a swap flight stands down one surface only (a window in the all-workspaces view
// is drawn twice at once, so the tile and the strip card are separate flights).

#include <cassert>
#include <cstdio>

#include "../src/model/effects.hpp"

using hyprview::model::Effects;
using hyprview::model::SubjectRef;
using hyprview::model::SwapFX;

int main() {
  const SubjectRef a{7, 1};
  const SubjectRef b{9, 1};

  // --- a pulse is replaced, not stacked -----------------------------------------------
  {
    Effects fx;
    fx.kickPulse(a);
    fx.eachPulse([](const auto &p) { assert(p.p == 0.0); });
    // The pulse of another subject is none of this one's business.
    fx.kickPulse(b);
    assert(fx.pulses().size() == 2);

    fx.kickPulse(a);
    assert(fx.pulses().size() == 2);
    std::size_t forA = 0;
    fx.eachPulse([&](const auto &p) {
      if (p.subj == a)
        ++forA;
    });
    assert(forA == 1);
    std::puts("a re-kicked pulse replaces the old one instead of stacking a second ring");
  }

  // --- a flight stands down one surface only ------------------------------------------
  {
    Effects fx;
    fx.addSwap(SwapFX{.subj = a, .from = {}, .toStrip = true});
    assert(fx.swapActive(a, true));
    assert(!fx.swapActive(a, false));
    assert(!fx.swapActive(b, true));
    std::puts("a swap flight belongs to the surface it was launched from");
  }

  // --- the origin of a flight is pinned by the caller that knows it --------------------
  {
    Effects fx;
    assert(!fx.retargetLastSwap(hyprview::LRect{1, 2, 3, 4}));
    fx.addSwap(SwapFX{.subj = a, .from = {}});
    assert(fx.retargetLastSwap(hyprview::LRect{5, 6, 7, 8}));
    assert(fx.swapfx().back().from.x == 5 && fx.swapfx().back().from.w == 7);
    std::puts("the last flight takes the origin its caller pins");
  }

  std::puts("the effects hold their invariants");
  return 0;
}

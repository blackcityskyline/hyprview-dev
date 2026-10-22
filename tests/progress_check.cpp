// The run's open/close position, and the rule that close inverts it.
//
// One double, seven readers, four writers -- and the four writers said nothing about what
// they meant. Two wrote 1.0 for "a swap has landed", three wrote 0.0 for "the exit
// finished", and one wrote 1e-6, which exists only so the first open frame is not zero.
// Nothing checked the range.
//
// This is a plain value type and the checks are small, so they live here rather than
// wherever the transitions happen to be called from.

#include <array>
#include <cstdio>
#include <cstdlib>

#include "../src/clock/progress.hpp"

using hyprview::clock::Progress;

namespace {

int g_checks = 0;

void near(double got, double want, const char *what) {
  ++g_checks;
  if (got < want - 1e-9 || got > want + 1e-9) {
    std::printf("FAIL %s: got %.17g, want %.17g\n", what, got, want);
    std::exit(1);
  }
}

void isTrue(bool got, const char *what) {
  ++g_checks;
  if (!got) {
    std::printf("FAIL %s\n", what);
    std::exit(1);
  }
}

} // namespace

int main() {
  // A fresh run is closed.
  Progress p;
  near(p.raw(), 0.0, "a default run is closed");

  // The master drives it up while opening, and the value is the master's raw progress.
  for (const double t : std::array{0.0, 0.25, 0.5, 1.0}) {
    p.fromMaster(t, /*opening=*/true);
    near(p.raw(), t, "while opening the value is the master's raw");
  }

  // close() rides the master DOWN: the master counts 0->1 and the run counts 1->0. This is
  // the inversion -- dropping it put every card on its natural box on the first close
  // frame, because currentBox lerps natural->target as e RISES.
  p.fromMaster(1.0, /*opening=*/false);
  near(p.raw(), 0.0, "the master at 1 means closed");
  p.fromMaster(0.0, /*opening=*/false);
  near(p.raw(), 1.0, "and the master at 0 means fully open");
  p.fromMaster(0.75, /*opening=*/false);
  near(p.raw(), 0.25, "so the close is the complement, not the same number");

  // exit() is the question every exit fraction asks, and there is no second formula.
  p.fromMaster(0.5, /*opening=*/true);
  near(p.exit(/*opening=*/true), 0.0, "while opening there is no exit progress");
  // A close is measured from the other end, so 0.75 must not come out as 0.75 here: the
  // value is 1 - master, and the exit is 1 - value, which puts it back at the master's own
  // 0.75. Using 0.5 would hide which of the two complements is in play.
  p.fromMaster(0.75, /*opening=*/false);
  near(p.raw(), 0.25, "while closing the value is the complement of the master");
  near(p.exit(/*opening=*/false), 0.75, "and exit() is the complement of the value");
  p.fromMaster(0.25, /*opening=*/false);
  near(p.exit(/*opening=*/false), 0.25, "which is where the master was");

  // The named end states, which are what replaced bare assignments.
  p.settled();
  near(p.raw(), 1.0, "settled is fully open");
  p.closed();
  near(p.raw(), 0.0, "closed is shut");

  // The first frame of an open is NOT zero. Zero is the closed state, and the entry
  // animation is evaluated against this -- a card would start one frame behind, which is
  // what 1e-6 is there to avoid.
  p.firstFrame();
  isTrue(p.raw() > 0.0, "the first frame is not exactly zero");
  isTrue(p.raw() < 1e-4, "the first frame is a hair above zero");
  near(p.exit(/*opening=*/false), 1.0 - p.raw(), "and exit() stays consistent with it");

  // No clamp, deliberately. A curve with overshoot legitimately hands back a value outside
  // [0,1] and the box math wants the overshoot: clamping here would flatten every
  // overshooting curve into a plateau at the top. This is the behaviour the bare
  // assignment had and changing it is a behaviour change, not a refactor -- so it is
  // pinned here instead.
  p.fromMaster(1.25, /*opening=*/true);
  near(p.raw(), 1.25, "an overshooting master is stored as given, not clamped");
  p.fromMaster(-0.25, /*opening=*/true);
  near(p.raw(), -0.25, "and so is a negative one");
  p.fromMaster(1.5, /*opening=*/false);
  near(p.raw(), -0.5, "closing complements an overshooting master the same way");

  std::printf("progress: %d checks\n", g_checks);
  std::puts("progress ok");
  return 0;
}
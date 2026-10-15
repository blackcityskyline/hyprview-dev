// view::State's mode rules.
//
// These were one-line expressions on the coordinator, spelled out at six call sites, and
// the part that actually decides anything -- "an override for THIS run outranks the config
// key, and -1 means no override" -- was not written down anywhere. A sixth site getting
// the precedence backwards would show every tile on the wrong workspace.

#include <cstdio>
#include <cstdlib>

#include "../src/view/state.hpp"

using gloview::view::State;

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
  // No override: the config decides, both ways.
  State s;
  want(s.allOverride() == -1, true, "a fresh run has no override");
  want(s.allWorkspaces(false), false, "no override, config off");
  want(s.allWorkspaces(true), true, "no override, config on");

  // An override wins over the config, in both directions. This is the whole rule.
  s.setAllOverride(1);
  want(s.allOverride() == 1, true, "the override is readable");
  want(s.allWorkspaces(false), true, "override on beats config off");
  want(s.allWorkspaces(true), true, "override on, config on");

  s.setAllOverride(0);
  want(s.allWorkspaces(true), false, "override OFF beats config ON");
  want(s.allWorkspaces(false), false, "override off, config off");

  // -1 is the ONLY value that means "no override". Any other zero-ish value is an answer.
  s.setAllOverride(-1);
  want(s.allWorkspaces(true), true, "back to no override, config on");
  want(s.allWorkspaces(false), false, "back to no override, config off");

  // The test is `< 0`, not `== -1`: any negative value means "no override" and the config
  // decides. -7 is not a thing the plugin sets, but the rule as written says what it says,
  // and getting this backwards would silently ignore the key.
  s.setAllOverride(-7);
  want(s.allWorkspaces(false), false, "any negative reads as no override, config off");
  want(s.allWorkspaces(true), true, "any negative reads as no override, config on");
  // And zero is NOT no override -- that is the case a `== 0` test would get wrong, and it
  // is the one that actually means "the user turned this off for this run".
  s.setAllOverride(0);
  want(s.allWorkspaces(true), false, "zero is an override, not an absence");

  // The mode flags are independent: touching one must not move another.
  State t;
  t.setAllOverride(1);
  t.setDesktop(true);
  t.setRun(false);
  t.setCarouselOn(false);
  want(t.allWorkspaces(false), true, "allWorkspaces survives the other flags");
  want(t.desktop(), true, "desktop");
  want(t.run(), false, "run");
  want(t.carouselOn(), false, "carouselOn");
  want(t.allOverride() == 1, true, "the override itself");

  // setDesktopMode flips one flag and nothing else -- which is what close() and
  // toggleDesktop() both depend on.
  State u;
  u.setDesktop(true);
  u.setDesktop(false);
  want(u.desktop(), false, "desktop off");
  want(u.allOverride() == -1, true, "allOverride untouched");
  want(u.run(), false, "run untouched");
  want(u.carouselOn(), true, "carouselOn untouched");

  std::printf("view state: %d checks\n", g_checks);
  std::puts("view state ok");
  return 0;
}
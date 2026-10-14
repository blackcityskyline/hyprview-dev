// The above-overview namespace list: a surface on this list must stay fully visible while
// the overlay is up.
//
// The parse lives in session::isAboveLayer, which takes the list as an argument rather than
// reading cfg:: -- so it is callable from here, with no compositor and no config. The comma
// splitting and the `*` prefix rule are the whole of it, and both have bitten: a list
// written "bar, foo " must not produce an entry with a trailing space, and `waybar*` has to
// match `waybar/something` without matching `notwaybar`.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/session/layer_names.hpp"

namespace {

int g_checks = 0;

void check(bool got, bool want, const char *what) {
  ++g_checks;
  if (got != want) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, want);
    std::fflush(stdout);
    std::abort();
  }
}

} // namespace

int main() {
  using gloview::session::isAboveLayer;

  // The implicit entry: every install carries it and the config cannot remove it.
  check(isAboveLayer("aboveoverview", ""), true, "aboveoverview with an empty list");
  check(isAboveLayer("aboveoverview-bar", ""), true, "aboveoverview as a substring");
  check(isAboveLayer("gloview-overlay", ""), false, "an unrelated namespace");

  // Exact entries, with the separators the config actually gets written with.
  check(isAboveLayer("waybar", "waybar"), true, "a lone entry");
  check(isAboveLayer("waybar", "waybar,quickshell"), true, "first of several");
  check(isAboveLayer("quickshell", "waybar,quickshell"), true, "last of several");
  check(isAboveLayer("waybar", "waybar, quickshell"), true, "space after the comma");
  check(isAboveLayer("waybar", " waybar "), true, "leading and trailing space");
  check(isAboveLayer("waybar", "waybar,,quickshell"), true, "an empty entry is skipped");
  check(isAboveLayer("", "waybar"), false, "an empty namespace matches nothing");

  // Prefix entries. `waybar*` is for a surface whose namespace is a path-like name.
  check(isAboveLayer("waybar/something", "waybar*"), true, "a prefix entry matches");
  check(isAboveLayer("waybar", "waybar*"), true, "a prefix entry matches the bare name");
  check(isAboveLayer("notwaybar", "waybar*"), false, "a prefix is anchored at the start");
  check(isAboveLayer("mywaybar", "waybar*"), false, "and not at the end either");
  check(isAboveLayer("way", "waybar*"), false, "a shorter name is not a match");

  // Mixed list, both forms, plus the implicit entry.
  const char *mixed = "waybar*, quickshell,gls42";
  check(isAboveLayer("waybar/x", mixed), true, "prefix in a mixed list");
  check(isAboveLayer("quickshell", mixed), true, "exact in a mixed list");
  check(isAboveLayer("gls42", mixed), true, "last exact in a mixed list");
  check(isAboveLayer("foot", mixed), false, "an absent name in a mixed list");
  // A config entry that CONTAINS the implicit name is redundant, not a way to exclude it:
  // the substring test runs first, so no list can turn it off.
  check(isAboveLayer("aboveoverview", mixed), true,
        "the implicit entry cannot be switched off by listing it");

  // A `*` alone means an empty prefix, and ns.compare(0, 0, "") is 0 for every name -- so a
  // bare star matches everything, the empty name included. Recorded because it is what the
  // rule says, not because it is useful: a user who writes above_namespaces = "*" gets every
  // layer kept visible, which is the opposite of what the name suggests.
  check(isAboveLayer("anything", "*"), true, "a bare star matches every name");
  check(isAboveLayer("", "*"), true, "including the empty one -- the prefix is empty");

  std::printf("isAboveLayer: %d checks\n", g_checks);
  std::puts("layer namespaces ok");
  return 0;
}
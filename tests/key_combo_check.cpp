// Key-combo parsing.
//
// The rule that matters is in the last line of comboMatches: EXTRA held modifiers fail
// the match. Without it a bare "tab" swallows shift+tab, and the user's own toggle bind
// stops firing — which reads as "my shortcut does nothing sometimes" and is not a config
// error at all. Every check below that involves a held modifier is really checking that
// one line, from a different direction.
//
// These are evdev keycodes, not Hyprland's: the plugin compares against the raw event.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/input/key_combo.hpp"

using hyprview::input::keyIndex;
using hyprview::input::keyMatches;
using hyprview::input::kModAlt;
using hyprview::input::kModCtrl;
using hyprview::input::kModMeta;
using hyprview::input::kModShift;

namespace {

int g_checks = 0;

void yes(int code, uint32_t mods, const char *combo, const char *what) {
  ++g_checks;
  if (!keyMatches(code, mods, combo)) {
    std::printf("FAIL %s: '%s' did not match with mods=%u\n", what, combo, mods);
    std::exit(1);
  }
}

void no(int code, uint32_t mods, const char *combo, const char *what) {
  ++g_checks;
  if (keyMatches(code, mods, combo)) {
    std::printf("FAIL %s: '%s' matched with mods=%u\n", what, combo, mods);
    std::exit(1);
  }
}

} // namespace

int main() {
  // evdev: tab=15, q=16, escape=1, enter=28, a=30, F1=59
  constexpr int kEsc = 1, kQ = 16, kTab = 15, kE = 18, kT = 20, kEnter = 28, kA = 30, kF1 = 59;

  // A bare token with nothing held.
  yes(kTab, 0, "tab", "a bare token");
  yes(kQ, 0, "q", "a letter");
  yes(kA, 0, "a", "another letter");
  yes(kEsc, 0, "escape", "a name longer than one letter");
  yes(kEnter, 0, "return", "return and enter are the same key");
  yes(kF1, 0, "f1", "a function key");
  no(kTab, 0, "q", "the wrong key");

  // THE RULE. A bare "tab" must not swallow shift+tab.
  no(kTab, kModShift, "tab", "a bare token does not take extra modifiers");
  no(kTab, kModCtrl, "tab", "ctrl is extra too");
  no(kTab, kModAlt, "tab", "alt is extra");
  no(kTab, kModMeta, "tab", "meta is extra");
  yes(kTab, kModShift, "shift+tab", "and shift+tab is its own binding");
  yes(kTab, kModCtrl, "ctrl+tab", "ctrl+tab is its own binding");
  yes(kTab, kModAlt, "alt+tab", "alt+tab is its own binding");
  yes(kTab, kModMeta, "super+tab", "and super+tab");

  // A combo must not fire on the bare key either.
  no(kTab, 0, "shift+tab", "shift+tab needs shift held");
  no(kQ, 0, "ctrl+q", "ctrl+q needs ctrl held");

  // Modifiers come FIRST. comboMatches peels '+' segments off the front until one is not a
  // modifier name, so "q+ctrl" asks for a modifier called "q", gets 0, and fails. This is
  // pre-existing and it is the spelling Hyprland's own config uses, so it stays -- but it
  // is a rule, not an accident, and until now nothing said so.
  yes(kQ, kModCtrl, "ctrl+q", "mod first");
  no(kQ, kModCtrl, "q+ctrl", "key first -- modifiers are only read from the front");
  yes(kQ, kModCtrl, "CTRL+Q", "uppercase");
  yes(kQ, kModCtrl, "control+q", "control is an alias for ctrl");
  yes(kTab, kModMeta, "meta+tab", "meta is an alias for super");
  yes(kTab, kModMeta, "win+tab", "win is an alias for super");
  yes(kTab, kModMeta, "super+tab", "and so is super");
  yes(kTab, kModShift, "SHIFT+TAB", "uppercase modifiers");

  // Two modifiers on one token: both must be held, and neither counts as extra.
  yes(kQ, kModCtrl | kModShift, "ctrl+shift+q", "two modifiers");
  no(kQ, kModCtrl, "ctrl+shift+q", "with only one of the two");
  no(kQ, kModShift, "ctrl+shift+q", "with only the other");

  // A modifier bit with no name in the table makes the whole token unmatchable, rather
  // than matching on the key alone -- "hyper+q" must not fire on plain q.
  no(kQ, 0, "hyper+q", "an unknown modifier fails the token");
  no(kQ, kModMeta, "hyper+q", "even with a real modifier held");

  // A trailing or leading plus leaves an empty name, which is not a key.
  no(kQ, kModCtrl, "ctrl+", "a trailing plus");
  no(kQ, 0, "+", "a bare plus");
  yes(kQ, 0, "q", "and the key still matches on its own");

  // Comma-separated alternatives: any one of them matches, in any position.
  yes(kTab, 0, "super+q,tab", "the second alternative");
  yes(kQ, kModMeta, "super+q,tab", "the first alternative");
  no(kEsc, 0, "super+q,tab", "neither alternative");
  yes(kEsc, 0, "a,escape", "an alternative after a comma and a space");
  yes(kA, 0, " a ", "surrounding whitespace is not a token");
  // "super+q,tab" must not let plain q through by falling off the end of the list.
  no(kQ, 0, "super+q", "an unheld modifier on the only alternative");

  // keyIndex answers WHICH position, which a multi-position bind needs.
  ++g_checks;
  if (keyIndex(kQ, kModMeta, "super+q,super+e,super+t") != 0) {
    std::puts("FAIL keyIndex: the first of three");
    std::exit(1);
  }
  ++g_checks;
  if (keyIndex(kE, kModMeta, "super+q,super+e,super+t") != 1) {
    std::puts("FAIL keyIndex: the middle of three");
    std::exit(1);
  }
  ++g_checks;
  if (keyIndex(kT, kModMeta, "super+q,super+e,super+t") != 2) {
    std::puts("FAIL keyIndex: the last of three");
    std::exit(1);
  }
  ++g_checks;
  if (keyIndex(kEsc, 0, "super+q") != -1) {
    std::puts("FAIL keyIndex: no match is -1");
    std::exit(1);
  }
  // An empty list is -1, not 0 -- 0 is a real position and would be the first entry of
  // nothing.
  ++g_checks;
  if (keyIndex(kQ, 0, "") != -1) {
    std::puts("FAIL keyIndex: an empty combo is -1");
    std::exit(1);
  }
  ++g_checks;
  if (keyIndex(kQ, 0, " , ") != -1) {
    std::puts("FAIL keyIndex: a combo of only separators is -1");
    std::exit(1);
  }
  // And a match at position 0 must not be reported as "no match".
  ++g_checks;
  if (keyIndex(kQ, kModCtrl, "ctrl+q,ctrl+e") != 0) {
    std::puts("FAIL keyIndex: zero is a position, not a miss");
    std::exit(1);
  }

  std::printf("key combos: %d checks\n", g_checks);
  std::puts("key combos ok");
  return 0;
}
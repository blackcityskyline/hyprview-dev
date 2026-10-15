// Which Hyprland window is which browser window, decided by the only thing both sides have.
//
// Every case here is a case where matching would be a guess, and the question being asked is
// not "does it match" but "does it match WITHOUT guessing". Two inboxes, two terminals, two
// browser windows whose active tabs are the same page -- in all of them the honest answer is
// "no", because the wrong match files one browser's tabs under another's window and nothing
// about the result looks wrong on screen.

#include <cstdio>
#include <string>
#include <vector>

#include "../src/model/tab_match.hpp"

using gloview::model::BrowserLinks;
using gloview::model::HostWindow;
using gloview::model::matchBrowserWindows;

namespace {

int failures = 0;

void want(bool ok, bool expected, const char *what) {
  if (ok == expected)
    return;
  ++failures;
  std::printf("FAIL: %s (expected %s)\n", what, expected ? "true" : "false");
}

// A map literal without typing the type twice at every use.
std::unordered_map<std::uint64_t, std::string> titles(std::initializer_list<
    std::pair<const std::uint64_t, std::string>> l) {
    return std::unordered_map<std::uint64_t, std::string>(l);
}

} // namespace

int main() {
  const std::vector<HostWindow> hosts = {
      {100, "Inbox — mail"},
      {101, "kitty"},
      {102, "Dashboard"},
  };

  // --- the ordinary case -------------------------------------------------------------------
  {
    const auto m = matchBrowserWindows(hosts, titles({{7, "Inbox — mail"}, {8, "kitty"}}));
    want(m.size() == 2, true, "two browser windows found their hosts");
    want(m.at(7) == 0, true, "the mail window matched the mail window");
    want(m.at(8) == 1, true, "the terminal matched the terminal");
    want(m.count(9) == 0, true, "a browser window whose title matches nothing is unmatched");
    std::puts("ordinary: exact titles match, one to one");
  }

  // --- two windows, one title ----------------------------------------------------------------
  // The failure the whole rule exists for. Two inboxes are not rare, and picking one is a
  // coin flip that looks like a match.
  {
    const std::vector<HostWindow> two = {{100, "Inbox"}, {101, "Inbox"}};
    const auto m = matchBrowserWindows(two, titles({{7, "Inbox"}}));
    want(m.empty(), true, "an ambiguous host title matches nothing");
    std::puts("ambiguity: two hosts with one title match neither");
  }

  // --- two browser windows, one title --------------------------------------------------------
  {
    const auto m = matchBrowserWindows(hosts, titles({{7, "kitty"}, {8, "kitty"}}));
    want(m.empty(), true, "two browser windows claiming one host match neither");
    std::puts("conflict: one host is not claimed twice");
  }

  // --- the first one wins nothing: a contested host is skipped for BOTH, not given to one ---
  {
    // Three browser windows, one host, and a second host that is unambiguous. The contested
    // pair must produce nothing, and the clean one must still be matched -- a conflict is
    // local, not a reason to give up on the rest.
    const auto m = matchBrowserWindows(hosts, titles({{7, "Dashboard"}, {8, "Dashboard"},
                                                      {9, "kitty"}}));
    want(m.size() == 1, true, "only the unambiguous one matched");
    want(m.at(9) == 1, true, "and it is the right one");
    std::puts("contest: local to the contested pair");
  }

  // --- an empty title agrees to nothing ---------------------------------------------------------
  {
    const std::vector<HostWindow> blank = {{100, ""}, {101, "kitty"}};
    const auto m = matchBrowserWindows(blank, titles({{7, ""}, {8, "kitty"}}));
    want(m.size() == 1, true, "only the window with a real title matched");
    want(m.count(7) == 0, true, "a blank title is unknown, not agreement");
    want(m.at(8) == 1, true, "the real one still matched");
    std::puts("blank: unknown is not agreement");
  }

  // --- nothing at all ---------------------------------------------------------------------------
  {
    want(matchBrowserWindows(hosts, {}).empty(), true, "nothing reported, nothing matched");
    want(matchBrowserWindows({}, titles({{7, "kitty"}})).empty(), true,
         "no hosts, no matches");
  }

  // --- the match, kept ----------------------------------------------------------------------------
  // Its own type because it has a lifetime: built when a run opens, dropped when it ends.
  // Holding it for the life of the plugin would let a title that matched during one run keep
  // deciding which browser a window belongs to during the next, and titles are what the match
  // is made of.
  {
    BrowserLinks links;
    links.rebuild(hosts, titles({{7, "Inbox — mail"}, {8, "kitty"}}));
    want(links.browserOf(100) == 7, true, "host to browser");
    want(links.browserOf(101) == 8, true, "and again");
    want(links.browserOf(102) == 0, true, "an unmatched host answers 0, not a stale match");
    want(links.hostOf(7) == 100, true, "browser to host, the other direction");
    want(links.hostOf(8) == 101, true, "and again");
    want(links.hostOf(9) == 0, true, "an unmatched browser answers 0");
    // A rebuild REPLACES. This is the point of holding it at all: the next run must not
    // inherit the last one's answers.
    links.rebuild(hosts, titles({{9, "kitty"}}));
    want(links.browserOf(100) == 0, true, "the previous run's match is gone");
    want(links.browserOf(101) == 9, true, "and the new one took its place");
    want(links.browserOf(102) == 0, true, "nothing carried over");
    links.clear();
    want(links.empty(), true, "and clearing empties it");
    want(links.browserOf(101) == 0, true, "with no answer left behind");
    std::puts("links: both directions, and a rebuild replaces");
  }

  if (failures) {
    std::printf("tab_match: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("tab_match: every guess refused");
  return 0;
}
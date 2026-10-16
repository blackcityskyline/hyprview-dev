// Which compositor window is which browser window, decided by process.
//
// A title cannot do this job and pretending otherwise is what this file used to do. A browser
// appends its own name to a window's title -- "(page) — Zen Browser" -- so the string a
// compositor holds and the string an extension reports differ by construction, always, on every
// browser that does it. Measured, not assumed: the compositor held
// "(31) ... - YouTube — Zen Browser" while the browser would report "(31) ... - YouTube".
//
// A pid is the same number on both sides. It is not a heuristic and it is not a prefix: two
// windows of the same browser share it, which is exactly the grouping wanted, and a window of
// another browser never does.

#include <cstdio>
#include <cstdint>
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

using Pids = std::unordered_map<std::uint64_t, std::uint64_t>;

Pids pids(std::initializer_list<std::pair<const std::uint64_t, std::uint64_t>> l) {
    return Pids(l);
}

// Four compositor windows: two of one browser, one of another, one that is not a browser.
const std::vector<HostWindow> hosts = {
    {100, 1111}, // firefox, window 1
    {101, 1111}, // firefox, window 2
    {102, 2222}, // chromium
    {103, 0},    // a terminal
};

} // namespace

int main() {
  // --- the ordinary case ---------------------------------------------------------------------
  {
    const auto m = matchBrowserWindows(hosts, pids({{7, 1111}, {8, 2222}}));
    // Browser 7 has two windows on the run and nothing says which one it is, so it matches
    // NEITHER. This is the case the old title rule got backwards: two windows of one browser
    // differ in title and agree in process. Browser 8 is untouched by this and still matches,
    // which is the point -- one browser's ambiguity is not another browser's problem.
    want(m.count(7) == 0, true, "a browser with two windows on the run matches neither");
    want(m.count(8) == 1, true, "while a browser with one window still matches it");
    want(m.size() == 1, true, "and the ambiguous one contributed nothing at all");
    std::puts("ambiguity: two windows of one browser, no way to tell");
  }

  // --- one window per browser ------------------------------------------------------------------
  {
    const auto m = matchBrowserWindows(hosts, pids({{8, 2222}}));
    want(m.size() == 1, true, "a browser with exactly one window matches it");
    want(m.at(8) == 2, true, "and it is the right one");
    std::puts("ordinary: one window, one process, one match");
  }

  // --- a process nobody claims ---------------------------------------------------------------
  {
    const auto m = matchBrowserWindows(hosts, pids({{9, 3333}}));
    want(m.empty(), true, "a browser with no window on this run matches nothing");
    std::puts("absence: a browser that is not on the run");
  }

  // --- a window with no process is never a candidate --------------------------------------------
  {
    // The terminal has pid 0. Even if a feed claimed process 0, it must not pick it up: 0 means
    // "this is not a browser window", and letting it match would attach tabs to whatever
    // happens to have no process.
    const auto m = matchBrowserWindows(hosts, pids({{9, 0}}));
    want(m.empty(), true, "process 0 matches no window, because no window claims to be one");
    std::puts("zero: an unknown process is not a window");
  }

  // --- the match, kept ----------------------------------------------------------------------------
  // Its own type because it has a lifetime: built when a run opens, dropped when it ends.
  {
    BrowserLinks links;
    links.rebuild(hosts, pids({{8, 2222}}));
    want(links.browserOf(102) == 8, true, "host to browser");
    want(links.browserOf(100) == 0, true, "an unmatched host answers 0, not a stale match");
    want(links.hostOf(8) == 102, true, "browser to host, the other direction");
    want(links.hostOf(7) == 0, true, "an unmatched browser answers 0");
    // A rebuild REPLACES: the next run must not inherit the last one's answers.
    links.rebuild(hosts, pids({{8, 1111}}));
    want(links.browserOf(100) == 0, true, "two windows of one browser still match neither");
    links.rebuild(hosts, pids({{7, 2222}}));
    want(links.browserOf(102) == 7, true, "and the new answer replaced the old");
    links.clear();
    want(links.empty(), true, "clearing empties it");
    want(links.browserOf(102) == 0, true, "with no answer left behind");
    std::puts("links: both directions, and a rebuild replaces");
  }

  if (failures) {
    std::printf("tab_match: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("tab_match: matched by process, never guessed");
  return 0;
}
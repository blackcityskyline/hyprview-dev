// What the browser side said, and what the plugin is allowed to conclude from it.
//
// The rules here are all about REFUSING to conclude something. Every one of them exists
// because the failure they prevent looks like working code: a tab filed under the wrong
// window, a generation that wraps onto one already seen, a half-read file that reads as "this
// browser has fewer tabs than it did". A parse that guesses is worse than a parse that
// returns nothing, because nothing is visibly nothing and a guess is not.

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "../src/model/tab_feed.hpp"

using hyprview::model::TabInfo;
using hyprview::model::TabSnapshot;
using hyprview::model::parseTabs;

namespace {

int failures = 0;

void want(bool ok, bool expected, const char *what) {
  if (ok == expected)
    return;
  ++failures;
  std::printf("FAIL: %s (expected %s)\n", what, expected ? "true" : "false");
}

// A snapshot that parsed, or an empty one so the test can continue and report more than the
// first failure. Returned BY VALUE: binding a reference to the optional's contents would
// dangle, because the optional is a temporary at every call site.
TabSnapshot must(const std::optional<TabSnapshot> &s, const char *what) {
  if (!s) {
    ++failures;
    std::printf("FAIL: %s -- did not parse\n", what);
    return {};
  }
  return *s;
}

void refuses(std::string_view text, const char *what) {
  want(!parseTabs(text).has_value(), true, what);
}

} // namespace

int main() {
  // --- the ordinary case ---------------------------------------------------------------------
  {
    const auto &s = must(parseTabs("g 7\n"
                                   "t 12 41 1 GitHub\n"
                                   "t 12 42 0 read the docs\n"
                                   "t 13 5 0 \n"),
                         "a well-formed snapshot");
    want(s.generation == 7, true, "the generation is the connection's identity");
    want(s.windows.size() == 2, true, "two windows reported");
    want(s.windows.at(12).known, true, "a window with tabs is known");
    want(s.windows.at(12).tabs.size() == 2, true, "both tabs of window 12");
    // Order is the browser's order and is load-bearing: the strip shows tabs left to right.
    want(s.windows.at(12).tabs[0].first == 41, true, "first tab id");
    want(s.windows.at(12).tabs[1].first == 42, true, "second tab id, in the order given");
    want(s.windows.at(12).tabs[0].second.title == "GitHub", true, "the title");
    want(s.windows.at(12).tabs[0].second.active, true, "the active flag");
    want(s.windows.at(12).tabs[1].second.active, false, "and an inactive one");
    // Spaces are the title, not separators. A caption showing "read the docs" as three words
    // is not what a title is.
    want(s.windows.at(12).tabs[1].second.title == "read the docs", true,
         "a title keeps its spaces");
    // An empty title is allowed and means exactly what it says.
    want(s.windows.at(13).tabs[0].second.title.empty(), true, "an empty title is a title");
    std::puts("ordinary: parsed, grouped, ordered, spaces kept");
  }

  // --- absence is not emptiness ----------------------------------------------------------------
  // The distinction the whole fallback rule rests on: a window nobody has spoken about is
  // NOT a window with no tabs. Collapsing the two makes the overview look like it lost every
  // window until the extension connects.
  {
    const auto &s = must(parseTabs("g 1\nt 12 1 0 x\n"), "one window");
    want(s.windows.count(99) == 0, true, "an unreported window is absent from the map");
    want(s.windows.at(12).known, true, "a reported window is known");
    // And the reported-but-tabless case is expressible: known, with nothing in it.
    const auto &none = must(parseTabs("g 1\n"), "a snapshot with no tabs at all");
    want(none.windows.empty(), true, "no windows heard from");
    want(none.generation == 1, true, "but the connection is still identified");
    std::puts("absence: unheard-of is not the same as empty");
  }

  // --- the generation is what makes ids reusable ------------------------------------------------
  {
    const auto &a = must(parseTabs("g 1\nt 12 41 0 one\n"), "generation 1");
    const auto &b = must(parseTabs("g 2\nt 12 41 0 two\n"), "generation 2");
    want(a.generation != b.generation, true,
         "a reconnect produces a different generation, so tab 41 cannot be confused");
    want(a.windows.at(12).tabs[0].second.title == "one", true, "first connection's tab 41");
    want(b.windows.at(12).tabs[0].second.title == "two", true, "second connection's tab 41");
    std::puts("generation: ids are only meaningful under the connection that issued them");
  }

  // --- refusals ---------------------------------------------------------------------------------
  // All of these mean "nothing has been reported". They must be indistinguishable, because a
  // half-read snapshot is worse than none: it looks like a browser with fewer tabs.
  {
    refuses("", "an empty file");
    refuses("\n\n", "a file of blank lines");
    refuses("t 12 41 0 x\n", "a tab with no generation");
    refuses("g\n", "a generation with no number");
    refuses("g 1\nt 12 41 2 x\n", "an active flag that is neither 0 nor 1");
    refuses("g 1\nt 12 41 x\n", "a tab with no active flag");
    refuses("g 1\nt 12 41\n", "a tab line that stops after the id");
    refuses("g 1\nt 12x 41 0 x\n", "junk glued onto a number");
    refuses("g 1\nt  41 0 x\n", "an empty window id");
    refuses("g 1\nt -1 41 0 x\n", "a signed id");
    refuses("g 1\ng 2\n", "two generation headers");
    refuses("g 1\nx 1 2 3\n", "a line with an unknown kind");
    refuses("g 1\nt 1 2 3\n", "an active flag of 3");
    // Overflow: a generation that wrapped would land on a number already seen, which would
    // make two different connections look like one.
    refuses("g 99999999999999999999999\n", "a generation that overflows");
    refuses("g 1\nt 99999999999999999999999 1 0 x\n", "a tab id that overflows");
    std::puts("refusals: eleven ways to say nothing, none of them guessed");
  }

  // --- a bounded generation keeps the epoch finite -------------------------------------------------
  {
    want(must(parseTabs("g 4294967295\n"), "max generation").generation == 4294967295u, true,
         "the largest generation the epoch can hold is accepted");
    refuses("g 4294967296\n", "one past it is refused");
    std::puts("generation: bounded by the epoch it becomes");
  }

  // --- windows and tabs may repeat, and order is the writer's -------------------------------------
  {
    const auto &s = must(parseTabs("g 3\nt 5 9 0 a\nt 5 9 0 b\nt 5 3 1 c\n"), "repeats");
    want(s.windows.at(5).tabs.size() == 3, true, "repeats are kept, not deduplicated");
    want(s.windows.at(5).tabs[0].first == 9, true, "in the order reported");
    want(s.windows.at(5).tabs[2].first == 3, true, "which is the order shown");
    std::puts("order: the writer's order is the plugin's order");
  }

  // --- the process a browser window belongs to --------------------------------------------------------
  // The one thing a compositor window and a browser window can be matched on exactly. A title
  // cannot: the browser appends its own name to it, so the string a compositor holds and the
  // string an extension reports differ by construction.
  {
    const auto &s = must(parseTabs("g 4\np 1234 12\np 1234 13\np 5678 14\n"
                                   "t 12 41 0 a\nt 13 1 0 b\nt 14 7 0 c\n"),
                         "a feed with processes");
    want(s.pids.size() == 3, true, "one process per window");
    want(s.pids.at(12) == 1234, true, "window 12 belongs to process 1234");
    // Two windows of the SAME browser share a process, which is the grouping wanted: a browser
    // with two windows is one program, and its tabs belong together.
    want(s.pids.at(13) == 1234, true, "and so does window 13");
    want(s.pids.at(14) == 5678, true, "while window 14 belongs to another browser");
    want(s.pids.count(99) == 0, true, "an unmentioned window has no process");

    // A feed with tabs but no processes is not useless -- it is what an older writer produced --
    // but it matches nothing, and saying so is the point.
    const auto &bare = must(parseTabs("g 5\nt 12 41 0 a\n"), "a feed with no processes");
    want(bare.pids.empty(), true, "no process lines means no processes");
    want(bare.windows.size() == 1, true, "and the tabs are still there");

    refuses("p abc 12\n", "a non-numeric process id");
    refuses("p 1234\n", "a process line with no window");
    std::puts("pids: per window, and shared within a browser");
  }

  if (failures) {
    std::printf("tab_feed: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("tab_feed: all rules hold");
  return 0;
}
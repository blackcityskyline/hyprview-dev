// The launcher's pure half: fzf-style ranking, the row list, selection and the action menu.
//
// The search is the feature -- a query that matches the wrong rows, or ranks a suffix above a
// prefix, is the bug a screenshot cannot show. Everything here runs headless.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/model/launcher.hpp"

using hyprview::model::fuzzyScore;
using hyprview::model::Launcher;
using hyprview::model::LauncherItem;
using hyprview::model::LauncherKind;
using hyprview::model::LauncherMode;
using hyprview::model::LauncherVerb;
using hyprview::model::rank;
using hyprview::model::verbLabel;
using hyprview::model::verbsFor;

namespace {

int g_checks = 0;

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

void wantInt(int got, int expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

void wantStr(const std::string &got, const std::string &expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got [%s], want [%s]\n", what, got.c_str(), expected.c_str());
    std::exit(1);
  }
}

LauncherItem row(std::string title, std::string detail = {}) {
  LauncherItem i;
  i.title = std::move(title);
  i.detail = std::move(detail);
  return i;
}

std::string titles(const std::vector<LauncherItem> &items, const std::string &q) {
  std::string out;
  for (const auto &h : rank(items, q)) {
    if (!out.empty())
      out += ",";
    out += items[h.item].title;
  }
  return out;
}

} // namespace

int main() {
  // Subsequence, not substring: the fzf contract.
  want(fuzzyScore("", "anything") == 0, true, "an empty query matches everything");
  want(fuzzyScore("fir", "Firefox") >= 0, true, "a prefix matches");
  want(fuzzyScore("ffx", "Firefox") >= 0, true, "a subsequence matches");
  want(fuzzyScore("fxr", "Firefox") == -1, true, "out-of-order letters do not match");
  want(fuzzyScore("firefoxes", "Firefox") == -1, true, "a query longer than the hay is no match");
  want(fuzzyScore("FIRE", "firefox") >= 0, true, "matching is case-insensitive");

  // Ranking rules that make a search feel right, not just correct.
  want(fuzzyScore("fire", "Firefox") > fuzzyScore("fire", "Bonfire"), true,
       "a prefix beats the same letters mid-word");
  want(fuzzyScore("tt", "Total Term") > fuzzyScore("tt", "attempt"), true,
       "word starts beat a run inside a word");
  want(fuzzyScore("abc", "abc") > fuzzyScore("abc", "axbxc"), true,
       "a tight run beats a scattered one");

  // The row order is the caller's order on a tie: recency is not the ranker's business.
  const std::vector<LauncherItem> items = {
      row("Firefox", "firefox"),
      row("Files", "org.gnome.Nautilus"),
      row("Terminal", "kitty"),
      row("Fire", "discord"),
  };
  wantStr(titles(items, ""), "Firefox,Files,Terminal,Fire", "an empty query keeps the list");
  wantStr(titles(items, "fir"), "Firefox,Fire", "only matching rows survive");
  wantStr(titles(items, "zzz"), "", "no match is an empty list");
  // "Terminal" and "Firefox" both contain the letters; the detail field is searched too.
  wantStr(titles(items, "kitty"), "Terminal", "the detail field is searched");

  Launcher l;
  want(l.active(), false, "a fresh launcher is closed");
  l.setItems(items);
  l.open(LauncherMode::Push);
  want(l.active(), true, "open");
  want(l.mode() == LauncherMode::Push, true, "the mode it was opened with");
  wantInt(static_cast<int>(l.hits().size()), 4, "an empty query shows every row");
  wantInt(l.selection(), 0, "the first row starts selected");

  l.appendChar('k');
  wantInt(static_cast<int>(l.hits().size()), 1, "typing narrows the list");
  l.appendChar('i');
  l.appendChar('t');
  wantInt(static_cast<int>(l.hits().size()), 1, "the query keeps narrowing");
  l.backspace();
  wantInt(static_cast<int>(l.hits().size()), 1, "backspace widens it again");
  want(l.query() == "ki", true, "the query is what was typed, minus the last character");

  // Selection: clamped on an explicit set, wrapping on a step.
  l.setQuery("");
  l.setSelection(99);
  wantInt(l.selection(), 3, "an explicit selection clamps to the last row");
  l.step(1);
  wantInt(l.selection(), 0, "a step wraps past the end");
  l.step(-1);
  wantInt(l.selection(), 3, "and wraps back");

  // A new query re-ranks and reselects: keeping index 3 over a different list is a lie.
  l.setSelection(3);
  l.setQuery("fire");
  wantInt(l.selection(), 0, "a new query reselects the top row");
  want(l.selectedItem() && l.selectedItem()->title == "Firefox", true,
       "the selected row is the top hit");

  // Action menu: per-kind verbs, and it dies with the row it belonged to.
  l.setQuery("");
  l.setSelection(0); // Firefox, a window
  want(l.menuOpen(), false, "the menu starts closed");
  l.toggleMenu();
  want(l.menuOpen(), true, "ctrl+b opens it");
  const auto winActs = l.menuActions();
  wantInt(static_cast<int>(winActs.size()), 4, "a window offers four verbs");
  want(winActs[0] == LauncherVerb::CloseWindow, true, "close window first");
  l.stepAny(1);
  wantInt(l.menuSelection(), 1, "the menu steps");
  wantInt(l.selection(), 0, "and the row list stays put while the menu owns the arrows");
  want(l.menuVerb() == LauncherVerb::KillWindow, true, "and names the verb under the cursor");
  l.step(1);
  want(l.menuOpen(), false, "stepping the row list closes the menu");

  LauncherItem tab;
  tab.kind = LauncherKind::Tab;
  tab.title = "Docs";
  tab.detail = "firefox";
  tab.tab = 42;
  l.setItems({tab});
  l.toggleMenu();
  const auto tabActs = l.menuActions();
  wantInt(static_cast<int>(tabActs.size()), 6, "a tab offers six verbs");
  want(tabActs[0] == LauncherVerb::CloseTab, true, "close tab first");
  want(tabActs[1] == LauncherVerb::UnloadTab, true, "unload tab second");
  want(tabActs[2] == LauncherVerb::CloseWindow, true, "the host window can be closed too");
  want(tabActs[3] == LauncherVerb::KillWindow, true, "and killed");

  // An action row has no menu: it IS the action.
  LauncherItem act;
  act.kind = LauncherKind::Action;
  act.title = "all workspaces";
  act.verb = LauncherVerb::ViewAll;
  l.setItems({act});
  l.toggleMenu();
  want(l.menuOpen(), false, "an action row opens no menu");

  // An empty list is not a selected row and not a menu.
  l.close();
  l.setItems({});
  l.toggleMenu();
  want(l.selectedItem() == nullptr, true, "nothing is selected in an empty list");
  want(l.hits().empty(), true, "and nothing is ranked");

  // Every verb has a label, and no two labels collide: the menu is built from them.
  for (std::size_t i = 0; i < static_cast<std::size_t>(LauncherVerb::Count); ++i) {
    const char *label = verbLabel(static_cast<LauncherVerb>(i));
    ++g_checks;
    if (!label || !*label) {
      std::printf("FAIL verb %zu has no label\n", i);
      std::exit(1);
    }
    for (std::size_t j = i + 1; j < static_cast<std::size_t>(LauncherVerb::Count); ++j)
      want(std::string(label) != verbLabel(static_cast<LauncherVerb>(j)), true,
           "verb labels are distinct");
  }
  want(verbsFor(LauncherKind::Action).empty(), true, "an action row has no verbs");
  want(verbsFor(LauncherKind::Window).size() == 4, true, "window verbs");
  want(verbsFor(LauncherKind::Tab).size() == 6, true, "tab verbs");

  std::printf("launcher: %d checks\n", g_checks);
  std::puts("launcher ok");
  return 0;
}

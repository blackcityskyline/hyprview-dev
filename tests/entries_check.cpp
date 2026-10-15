// How many entries one window contributes, in each of the three modes.
//
// The rule under test is the FALLBACK. In tabs mode a window the extension has never
// reported on must still appear -- as a window -- because "not heard from yet" and "has no
// tabs" look identical from here, and dropping every window until the extension connects is
// the failure that reads as "the overview lost my windows".
//
// A window that HAS been reported with zero tabs contributes nothing. That is the mode
// doing its job, and the two cases must not collapse into each other.
//
// model/entries.hpp carries no Hyprland, so this compiles with nothing but the standard
// library.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/model/entries.hpp"

using gloview::model::Entry;
using gloview::model::TabSet;
using gloview::model::TileMode;
using gloview::model::entriesFor;
using gloview::model::parseTileMode;

namespace {

int g_checks = 0;

const void *const kWin = reinterpret_cast<const void *>(0x1000);
const void *const kOther = reinterpret_cast<const void *>(0x2000);

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

TabSet tabs(std::vector<int> ids) {
  return TabSet{true, std::move(ids)};
}

// All entries are TABS of this window, and there are exactly the ids given, in order.
bool allTabs(const std::vector<Entry> &e, const std::vector<int> &ids) {
  if (e.size() != ids.size())
    return false;
  for (std::size_t i = 0; i < e.size(); ++i)
    if (e[i].win != kWin || e[i].tabId != ids[i])
      return false;
  return true;
}

} // namespace

int main() {
  // --- windows mode: unchanged, whatever the extension says -----------------------------
  {
    const auto none = entriesFor(kWin, TileMode::Windows, TabSet{false, {}});
    want(none.size() == 1 && none[0].tabId == -1, true, "windows, never heard from");

    const auto some = entriesFor(kWin, TileMode::Windows, tabs({7, 8, 9}));
    want(some.size() == 1 && some[0].tabId == -1, true, "windows, three tabs reported");

    const auto zero = entriesFor(kWin, TileMode::Windows, tabs({}));
    want(zero.size() == 1, true, "windows, reported none");
    std::puts("windows mode: one entry always, whatever the extension says");
  }

  // --- tabs mode -------------------------------------------------------------------------
  {
    // The fallback: not heard from yet, so shown as a window rather than dropped.
    const auto unknown = entriesFor(kWin, TileMode::Tabs, TabSet{false, {}});
    want(unknown.size() == 1 && unknown[0].tabId == -1, true,
         "tabs, never heard from, falls back to the window");
    want(unknown[0].win == kWin, true, "and it is this window");

    // Heard from: the window itself is NOT shown, only its tabs.
    want(allTabs(entriesFor(kWin, TileMode::Tabs, tabs({7, 8, 9})), {7, 8, 9}),
         true, "tabs, three reported");

    // Heard from with none: nothing. This is the case that must not fall back.
    want(entriesFor(kWin, TileMode::Tabs, tabs({})).empty(), true,
         "tabs, reported none, contributes nothing");

    // Order is the extension's, and it is the extension's because that is the order the user
    // sees in their own browser bar.
    want(allTabs(entriesFor(kWin, TileMode::Tabs, tabs({30, 10, 20})), {30, 10, 20}),
         true, "tabs keep the extension's order");

    // The other window is unaffected by this one's data.
    const auto other = entriesFor(kOther, TileMode::Tabs, tabs({}));
    want(other.empty(), true, "a second window with no tabs also contributes nothing");
    std::puts("tabs mode: falls back when unheard from, tab-only when heard from");
  }

  // --- both -------------------------------------------------------------------------------
  {
    const auto b = entriesFor(kWin, TileMode::Both, tabs({7, 8}));
    want(b.size() == 3, true, "both: the window and its two tabs");
    want(b[0].tabId == -1, true, "and the window comes first");
    want(b[1].tabId == 7 && b[2].tabId == 8, true, "then the tabs in order");

    const auto bUnknown = entriesFor(kWin, TileMode::Both, TabSet{false, {}});
    want(bUnknown.size() == 1, true, "both, unheard from, is just the window");
    want(bUnknown[0].tabId == -1, true, "as a window");

    const auto bZero = entriesFor(kWin, TileMode::Both, tabs({}));
    want(bZero.size() == 1 && bZero[0].tabId == -1, true,
         "both, reported none, is still the window");
    std::puts("both mode: the window, then its tabs");
  }

  // --- the parse: a typo must not change what the overview is ------------------------------
  {
    want(parseTileMode("windows") == TileMode::Windows, true, "windows");
    want(parseTileMode("tabs") == TileMode::Tabs, true, "tabs");
    want(parseTileMode("both") == TileMode::Both, true, "both");
    want(parseTileMode("") == TileMode::Windows, true, "empty is windows");
    want(parseTileMode("Windows") == TileMode::Windows, true, "case differs");
    // The load-bearing one: a misspelling must not land on tabs, which would silently
    // change what every window contributes.
    want(parseTileMode("tab") == TileMode::Windows, true, "a typo is windows, not tabs");
    want(parseTileMode("tabss") == TileMode::Windows, true, "and another");
    want(parseTileMode("t") == TileMode::Windows, true, "and a prefix");
    std::puts("parse: unknown names are windows, never tabs");
  }

  // --- tab id 0 is a tab, not a missing one ---------------------------------------------------
  {
    // ids[0] == 0 is a real tab id. If 0 were treated as "absent" the first tab of every
    // window would silently become the window itself.
    const auto e = entriesFor(kWin, TileMode::Tabs, tabs({0}));
    want(e.size() == 1, true, "tab id 0 produces one entry");
    want(e[0].tabId == 0, true, "and it is the tab, not the window");
    std::puts("tab id 0 is a tab, not a missing id");
  }

  std::printf("entries: %d checks\n", g_checks);
  std::puts("entries ok");
  return 0;
}
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
using gloview::model::Domain;
using gloview::model::entriesFor;
using gloview::model::domainName;
using gloview::model::parseDomain;

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

std::vector<std::int64_t> tabs(std::vector<int> ids) {
  return {ids.begin(), ids.end()};
}

// The three facts entriesFor() takes, which is what a caller holding a window can actually
// answer without rebuilding a container it does not own. isBrowser defaults to true: every
// case below that does not say otherwise is about a browser window.
std::vector<Entry> entries(const void *win, Domain mode, bool known,
                           const std::vector<std::int64_t> &ids, bool isBrowser = true) {
  return entriesFor(win, mode, known, ids, isBrowser);
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
    const auto none = entries(kWin, Domain::Windows, false, {});
    want(none.size() == 1 && none[0].tabId == -1, true, "windows, never heard from");

    const auto some = entries(kWin, Domain::Windows, true, tabs({7, 8, 9}));
    want(some.size() == 1 && some[0].tabId == -1, true, "windows, three tabs reported");

    const auto zero = entries(kWin, Domain::Windows, true, tabs({}));
    want(zero.size() == 1, true, "windows, reported none");
    std::puts("windows mode: one entry always, whatever the extension says");
  }

  // --- tabs mode -------------------------------------------------------------------------
  {
    // The fallback: not heard from yet, so shown as a window rather than dropped.
    const auto unknown = entries(kWin, Domain::Tabs, false, {});
    want(unknown.size() == 1 && unknown[0].tabId == -1, true,
         "tabs, never heard from, falls back to the window");
    want(unknown[0].win == kWin, true, "and it is this window");

    // Heard from: the window itself is NOT shown, only its tabs.
    want(allTabs(entries(kWin, Domain::Tabs, true, tabs({7, 8, 9})), {7, 8, 9}),
         true, "tabs, three reported");

    // Heard from with none: nothing. This is the case that must not fall back.
    want(entries(kWin, Domain::Tabs, true, tabs({})).empty(), true,
         "tabs, reported none, contributes nothing");

    // Order is the extension's, and it is the extension's because that is the order the user
    // sees in their own browser bar.
    want(allTabs(entries(kWin, Domain::Tabs, true, tabs({30, 10, 20})), {30, 10, 20}),
         true, "tabs keep the extension's order");

    // The other window is unaffected by this one's data.
    const auto other = entries(kOther, Domain::Tabs, true, tabs({}));
    want(other.empty(), true, "a second window with no tabs also contributes nothing");
    std::puts("tabs mode: falls back when unheard from, tab-only when heard from");
  }

  // --- a window that is not a browser ---------------------------------------------------------
  //
  // "Never heard from" and "not a browser" are both an empty tab list. Reading them as one
  // thing put the terminal, the editor and everything else on the desktop into the browser's
  // view -- which is a view whose whole subject is one program's tabs, so it is not a view of
  // that program. The fallback above is deliberately kept for the BROWSER case: an extension
  // that has not connected yet must not empty the screen.
  {
    want(entries(kOther, Domain::Tabs, false, {}, false).empty(), true,
         "tabs, a non-browser unheard from, contributes nothing");
    want(entries(kOther, Domain::Tabs, true, tabs({}), false).empty(), true,
         "tabs, a non-browser heard from, contributes nothing");
    // Windows mode still shows everything -- it is the default view and it does not care what
    // the extension says.
    want(entries(kOther, Domain::Windows, false, {}, false).size() == 1, true,
         "windows mode, a non-browser, is still one entry");
    std::puts("a non-browser is not in the browser's view");
  }

  // --- both -------------------------------------------------------------------------------
  {
    const auto b = entries(kWin, Domain::Both, true, tabs({7, 8}));
    want(b.size() == 3, true, "both: the window and its two tabs");
    want(b[0].tabId == -1, true, "and the window comes first");
    want(b[1].tabId == 7 && b[2].tabId == 8, true, "then the tabs in order");

    const auto bUnknown = entries(kWin, Domain::Both, false, {});
    want(bUnknown.size() == 1, true, "both, unheard from, is just the window");
    want(bUnknown[0].tabId == -1, true, "as a window");

    const auto bZero = entries(kWin, Domain::Both, true, tabs({}));
    want(bZero.size() == 1 && bZero[0].tabId == -1, true,
         "both, reported none, is still the window");
    std::puts("both mode: the window, then its tabs");
  }

  // --- the parse: a typo must not change what the overview is ------------------------------
  {
    want(parseDomain("windows") == Domain::Windows, true, "windows");
    want(parseDomain("tabs") == Domain::Tabs, true, "tabs");
    want(parseDomain("both") == Domain::Both, true, "both");
    want(parseDomain("") == Domain::Windows, true, "empty is windows");
    want(parseDomain("Windows") == Domain::Windows, true, "case differs");
    // The load-bearing one: a misspelling must not land on tabs, which would silently
    // change what every window contributes.
    want(parseDomain("tab") == Domain::Windows, true, "a typo is windows, not tabs");
    want(parseDomain("tabss") == Domain::Windows, true, "and another");
    want(parseDomain("t") == Domain::Windows, true, "and a prefix");
    std::puts("parse: unknown names are windows, never tabs");
  }

  // --- the name and the parse are one vocabulary, or the trace lies --------------------------
  // domainName() is what gets written into ANIM| and SWAP|, and parseDomain() is what reads
  // it back out of a config. If the two ever drift -- "tab" written, "tabs" read -- then a
  // mode written into a trace would not survive a round trip through the only place it can
  // be restored, and the record would name a mode that cannot be selected.
  {
    for (const auto d : {Domain::Windows, Domain::Tabs, Domain::Both})
      want(parseDomain(domainName(d)) == d, true,
           "a mode must survive being written and read back");
    // And the three names must stay distinct, which a copy-paste into domainName() would
    // not be caught by if only one direction were checked.
    want(domainName(Domain::Windows) != domainName(Domain::Tabs) &&
             domainName(Domain::Tabs) != domainName(Domain::Both) &&
             domainName(Domain::Windows) != domainName(Domain::Both),
         true, "the three modes have three different names");
    std::puts("names round-trip through the parser");
  }

  // --- tab id 0 is a tab, not a missing one ---------------------------------------------------
  {
    // ids[0] == 0 is a real tab id. If 0 were treated as "absent" the first tab of every
    // window would silently become the window itself.
    const auto e = entries(kWin, Domain::Tabs, true, tabs({0}));
    want(e.size() == 1, true, "tab id 0 produces one entry");
    want(e[0].tabId == 0, true, "and it is the tab, not the window");
    std::puts("tab id 0 is a tab, not a missing id");
  }

  std::printf("entries: %d checks\n", g_checks);
  std::puts("entries ok");
  return 0;
}
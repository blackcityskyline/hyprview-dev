// The tabs source, against the contract, with no compositor anywhere.
//
// This is the test that makes `virtual` on IContentSource mean something: Windows is the
// first implementation and Tabs is the second, and the point is that the SAME caller works
// with both. The fakes in content_source_check already proved the interface is enough to
// drive; this proves the second real implementation is built only out of that interface --
// no Hyprland type, no window, no workspace, nothing the boundary was drawn to keep out.

#include <cstdio>
#include <string>
#include <vector>

#include "../src/model/content_source.hpp"
#include "../src/model/tab_source.hpp"

using hyprview::model::GroupRef;
using hyprview::model::IContentSource;
using hyprview::model::SubjectRef;
using hyprview::model::TabSnapshot;
using hyprview::model::TabsContentSource;
using hyprview::model::parseTabs;

namespace {

int failures = 0;

void want(bool ok, bool expected, const char *what) {
  if (ok == expected)
    return;
  ++failures;
  std::printf("FAIL: %s (expected %s)\n", what, expected ? "true" : "false");
}

TabSnapshot snap(const char *text) { return *parseTabs(text); }

// Everything a caller may ask, asked only through the interface.
void drive(IContentSource &src, const char *what) {
  const auto all = src.enumerate();
  want(!all.empty(), true, what);
  (void)all;
}

} // namespace

int main() {
  // --- a source that has been told nothing -----------------------------------------------------
  {
    TabsContentSource s;
    want(s.enumerate().empty(), true, "nothing reported, nothing enumerated");
    want(s.isTab(SubjectRef{1, 1}), false, "a stale epoch is not a tab of this connection");
    want(s.label(SubjectRef{1, 1}).empty(), true, "and it labels nothing");
    want(s.groupOf(SubjectRef{1, 1}) == GroupRef{}, true, "and groups into the default");
    std::puts("empty: an unconnected source is empty, not broken");
  }

  // --- one subject per tab, under the connection that issued it ---------------------------------
  {
    TabsContentSource s;
    want(s.adopt(snap("g 7\nt 12 41 1 GitHub\nt 12 42 0 docs\nt 13 5 0 mail\n")), true,
         "a well-formed snapshot is accepted");
    const auto all = s.enumerate();
    want(all.size() == 3, true, "one subject per tab");
    for (const auto &r : all) {
      want(s.isTab(r), true, "every subject of the tabs source is a tab");
      want(r.epoch == 7, true, "and carries the connection as its epoch");
    }
    drive(s, "the interface drives the tabs source");
    std::puts("subjects: one per tab, epoch is the connection");
  }

  // --- labels and groups -------------------------------------------------------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 1 GitHub\nt 12 42 0 read the docs\nt 13 5 0 mail\n"));
    want(s.label(SubjectRef{41, 7}) == "GitHub", true, "the title of a tab");
    want(s.label(SubjectRef{42, 7}) == "read the docs", true, "spaces kept");
    want(s.label(SubjectRef{999, 7}).empty(), true, "a tab nobody reported labels nothing");
    // The group is the BROWSER WINDOW. A tab's group must never be a Hyprland workspace: the
    // compositor does not know tabs, and a workspace id here would be a claim nobody can check.
    want(s.groupOf(SubjectRef{41, 7}) == (GroupRef{12, 7}), true, "grouped by browser window");
    want(s.groupOf(SubjectRef{5, 7}) == (GroupRef{13, 7}), true, "and by the right one");
    want(s.groupOf(SubjectRef{999, 7}) == GroupRef{}, true, "an unknown tab has no group");
    std::puts("labels and groups: tabs group by browser window, never by workspace");
  }

  // --- the epoch is load-bearing ------------------------------------------------------------------
  // Two connections, same tab id. This is the ABA hazard the epoch exists for, and it is the
  // one thing that must not be "obviously fine".
  {
    TabsContentSource s;
    s.adopt(snap("g 1\nt 12 41 0 first\n"));
    want(s.label(SubjectRef{41, 1}) == "first", true, "the first connection's tab 41");
    s.adopt(snap("g 2\nt 12 41 0 second\n"));
    want(s.label(SubjectRef{41, 2}) == "second", true, "the second connection's tab 41");
    // The OLD ref must not resolve into the NEW connection. If it did, a tile built before a
    // reconnect would silently show a different page's title.
    want(s.label(SubjectRef{41, 1}).empty(), true, "the old ref labels nothing after a reconnect");
    want(s.isTab(SubjectRef{41, 1}), false, "and is no longer a tab");
    want(s.groupOf(SubjectRef{41, 1}) == GroupRef{}, true, "and has no group");
    std::puts("epoch: a ref from a previous connection resolves to nothing");
  }

  // --- a rejected snapshot changes nothing ---------------------------------------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 1 GitHub\n"));
    // Generation 0 is window-domain by definition; adopting it would let a tab claim to be a
    // window, which is the exact confusion the two-domain encoding exists to prevent.
    want(s.adopt(snap("g 0\nt 12 41 1 Other\n")), false, "generation zero is refused");
    want(s.label(SubjectRef{41, 7}) == "GitHub", true, "and the previous snapshot survives");
    // And a tab id that repeats within one connection is the writer's data, kept as given:
    // deduplicating it here would invent a rule the browser does not have.
    want(s.adopt(snap("g 8\nt 12 41 0 a\nt 12 41 0 b\n")), true, "a repeated tab id is accepted");
    want(s.label(SubjectRef{41, 8}) == "b", true, "last report wins, as the file says");
    std::puts("adopt: refused snapshots leave the previous one alone");
  }

  // --- clear() is a real forget, not a cosmetic one -------------------------------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 1 GitHub\n"));
    s.clear();
    want(s.enumerate().empty(), true, "clear() enumerates nothing");
    want(s.label(SubjectRef{41, 7}).empty(), true, "and keeps no titles");
    want(s.generation() == 0, true, "and forgets the connection");
    std::puts("clear: titles do not outlive the run");
  }

  // --- the verbs are remote, and the source is not the one who writes ------------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 1 GitHub\n"));
    const SubjectRef tab{41, 7};
    // False is the interface's own word for "it did not happen". Anything else here -- a true,
    // or a crash -- would make a caller animate towards a tab that never moved.
    want(s.activate(tab), false, "activating a tab goes through the mediator, not the source");
    want(s.close(tab), false, "closing a tab is remote too");
    want(s.moveTo(tab, GroupRef{12, 7}), false, "moving a tab is remote as well");
    std::puts("verbs: remote, and the source stays pure");
  }

  // --- answering by browser window, which is what a tile builder asks ---------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 0 mail\nt 12 42 0 docs\nt 13 5 0 chat\n"));
    want(s.knownFor(12), true, "a reported window is known");
    want(s.knownFor(13), true, "and so is the other one");
    want(s.knownFor(14), false, "a window nobody mentioned is not");
    const auto ids = s.tabIdsFor(12, /*showDiscarded=*/true);
    want(ids.size() == 2, true, "two tabs in window 12");
    want(ids[0] == 41 && ids[1] == 42, true, "in the order reported");
    want(s.tabIdsFor(13, true).size() == 1, true, "one in window 13");
    want(s.tabIdsFor(14, true).empty(), true, "and none for an unheard-of window");
    // The distinction the fallback rule is made of: never heard from is not the same as heard
    // from and empty. A check that only counted ids would not tell them apart, and the wrong
    // one drops every window until the extension connects.
    TabsContentSource empty;
    empty.adopt(snap("g 3\n"));
    want(empty.knownFor(12), false, "a connection that has not reported a window yet");
    want(empty.tabIdsFor(12, true).empty(), true, "no tabs for it either");
    std::puts("by window: known and ids, and unheard-from stays distinct from empty");
  }

  // --- unloaded tabs: reported always, listed only when asked for ------------------------------
  {
    TabsContentSource s;
    s.adopt(snap("g 7\nt 12 41 1 loaded\nt 12 42 2 unloaded\nt 12 43 3 active-unloaded\n"));
    const auto hidden = s.tabIdsFor(12, /*showDiscarded=*/false);
    want(hidden.size() == 1 && hidden[0] == 41, true,
         "the default listing is the loaded tabs only");
    const auto shown = s.tabIdsFor(12, /*showDiscarded=*/true);
    want(shown.size() == 3, true, "the toggle adds the unloaded ones");
    want(shown[1] == 42 && shown[2] == 43, true, "in the order reported");
    std::puts("unloaded: hidden by default, listed on request");
  }

  if (failures) {
    std::printf("tab_source: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("tab_source: all rules hold");
  return 0;
}
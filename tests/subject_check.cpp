// SubjectRef, GroupRef, and the Domain axis.
//
// The thing worth checking here is the EPOCH, because the bug it prevents is invisible by
// construction: a bare integer id compares equal to whatever inherited the number, and the
// code compiles, the map finds an entry, and the overview shows the wrong window.
//
// The second thing is Domain: a tab id of 0 must be a tab, and a window subject must never
// be confused with one. -1 exists for that and only for that, and a default-constructed
// SubjectRef must land on the window, not on a tab.
//
// model/subject.hpp has no compositor in it, which is what lets any of this be checked here.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "../src/model/subject.hpp"

using hyprview::model::Domain;
using hyprview::model::GroupRef;
using hyprview::model::SubjectRef;
using hyprview::model::showsTabs;
using hyprview::model::showsWindows;

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
  // --- identity -------------------------------------------------------------------------
  {
    SubjectRef a{41, 1};
    SubjectRef b{41, 1};
    want(a == b, true, "same id and epoch are the same subject");

    // THE rule. Tab 41 closed and a different page became tab 41: same number, different
    // subject, and nothing but the epoch tells them apart.
    SubjectRef reused{41, 2};
    want(a == reused, false, "a reused id under a new epoch is a different subject");
    want(a.id == reused.id, true, "and the numbers really are the same");

    // The reverse, which a `>=` comparison would get wrong: an older epoch is also not the
    // same subject. The stream can arrive out of order after a reconnect.
    want(reused == a, false, "and it does not match in the other direction either");

    // Epoch zero is a real epoch, not a wildcard. Default-constructed is the live compositor
    // window, which is the only subject with no source of truth behind it.
    const SubjectRef live{};
    want(live.id == 0 && live.epoch == 0, true, "default is window 0");
    want(live == SubjectRef{0, 0}, true, "and it equals an explicit window 0");
    want(live == SubjectRef{0, 1}, false, "epoch 0 is not a wildcard");
    std::puts("subject: id plus epoch, both directions, epoch 0 is not a wildcard");
  }

  // --- tab 0 is a tab ------------------------------------------------------------------
  {
    // If 0 meant "no tab", the first tab of every window would silently become the window.
    const SubjectRef tab0{0, 1};
    const SubjectRef the_live_window{};
    want(tab0.id == 0, true, "tab id 0 exists");
    want(tab0 != the_live_window, true, "and is not the default window");
    want(tab0 != SubjectRef{0, 0}, true, "nor a window-0 under epoch 0");
    std::puts("subject: id 0 is a subject, not a sentinel");
  }

  // --- a map keyed by SubjectRef ------------------------------------------------------------
  {
    // The use the type exists for. A map keyed by the raw id alone would hand the previous
    // tenant's value to the new one, which is the ABA the epoch is here to prevent.
    std::unordered_map<SubjectRef, int> live_subjects;
    live_subjects[SubjectRef{41, 1}] = 100;

    // Tab 41 is closed.
    live_subjects.erase(SubjectRef{41, 1});
    want(live_subjects.empty(), true, "closed and removed");

    // A different page becomes tab 41, under the same epoch -- the extension keeps its
    // connection, so nothing bumped.
    live_subjects[SubjectRef{41, 1}] = 200;
    want(live_subjects.size() == 1, true, "reopened under the same epoch");
    want(live_subjects[SubjectRef{41, 1}] == 200, true, "with its own value");

    // Now the extension reconnects. Every id it still holds is from the old connection and
    // every id it issues after is new, so the epoch moves and the old ones stop matching.
    const std::uint32_t next_epoch = 2;
    live_subjects[SubjectRef{41, next_epoch}] = 300;
    want(live_subjects.size() == 2, true, "the new epoch is a different subject");
    want(live_subjects[SubjectRef{41, 1}] == 200, true, "the old one is untouched");
    want(live_subjects[SubjectRef{41, next_epoch}] == 300, true, "the new one is separate");
    // And a lookup with the stale ref finds the stale value, not the new one.
    want(live_subjects.find(SubjectRef{41, 1}) != live_subjects.end(), true,
         "a stale ref still finds its own entry, not the live one");
    std::puts("subject: a map keyed by it does not leak across a reuse or a reconnect");
  }

  // --- GroupRef is the same shape for the same reason ------------------------------------
  {
    const GroupRef ws1{3, 0};
    const GroupRef ws2{3, 1};
    want(ws1 == GroupRef{3, 0}, true, "groups compare the same way");
    want(ws1 != ws2, true, "a reused workspace number under a new epoch differs");
    std::puts("group: same shape, same reason");
  }

  // --- the Domain axis ---------------------------------------------------------------------
  {
    want(showsWindows(Domain::Windows) && !showsTabs(Domain::Windows), true, "windows");
    want(!showsWindows(Domain::Tabs) && showsTabs(Domain::Tabs), true, "tabs");
    want(showsWindows(Domain::Both) && showsTabs(Domain::Both), true, "both");

    // Both is the only mode where a window subject and a tab subject coexist, and they must
    // stay distinguishable there. Window 0 and tab 0 in the same deck are different things.
    const SubjectRef win{};
    const SubjectRef tab{0, 1};
    want(win != tab, true, "window 0 and tab 0 are different subjects under both");
    std::puts("domain: windows, tabs, both, and window 0 != tab 0");
  }

  std::printf("subject: %d checks\n", g_checks);
  std::puts("subject ok");
  return 0;
}
// The staleness rule of the subject cache, with no GPU and no texture to fabricate.
//
// The thing being tested is not a picture. It is: a tab id belongs to the connection that
// issued it, so a picture produced under an old connection must never be handed out under a
// new one -- and the browser is reusing those ids at this very moment. Getting that wrong
// shows one page under another page's tab, which is not a crash, not a warning, and not
// something a user could report with any confidence.

#include <cstdio>
#include <string>

#include "../src/pixel/subject_cache.hpp"

using hyprview::model::SubjectRef;
using hyprview::pixel::SubjectCache;

namespace {

int failures = 0;

void want(bool ok, bool expected, const char *what) {
  if (ok == expected)
    return;
  ++failures;
  std::printf("FAIL: %s (expected %s)\n", what, expected ? "true" : "false");
}

// A stand-in for a texture. It needs to be falsy when absent and hold something identifiable
// when present -- that is all the cache ever asks of it.
struct FakeTex {
  std::string name;
  explicit operator bool() const { return !name.empty(); }
};

} // namespace

int main() {
  SubjectCache<FakeTex> c;

  // --- nothing cached is nothing found --------------------------------------------------------
  {
    want(c.size() == 0, true, "a fresh cache is empty");
    want(c.find(SubjectRef{41, 7}) == nullptr, true, "and finds nothing");
    want(c.has(SubjectRef{41, 7}), false, "and does not claim to have it");
    std::puts("empty: absent is not a null texture");
  }

  // --- the ordinary case ------------------------------------------------------------------------
  {
    c.put(SubjectRef{41, 7}, FakeTex{"tab41"});
    want(c.size() == 1, true, "one picture stored");
    want(c.has(SubjectRef{41, 7}), true, "found under its own key");
    want(c.find(SubjectRef{41, 7}) != nullptr &&
             c.find(SubjectRef{41, 7})->name == "tab41", true,
         "and it is the right one");
    want(c.has(SubjectRef{42, 7}), false, "another tab has none");
    want(c.find(SubjectRef{41, 8}) == nullptr, true, "the same id under another generation has none");
    std::puts("ordinary: keyed by id AND generation");
  }

  // --- reconnecting drops everything, not just the reused ids ------------------------------------
  // The point. Tab 41 is reused across connections; a picture of the old 41 must not answer
  // for the new one. Clearing on a generation change is what makes that true, and clearing
  // is safe precisely because the new connection re-reports everything.
  {
    c.put(SubjectRef{42, 7}, FakeTex{"tab42"}); // two pictures now
    want(c.size() == 2, true, "two pictures");
    c.put(SubjectRef{41, 8}, FakeTex{"new41"});  // the connection moved
    want(c.size() == 1, true, "and one put under a new generation dropped the rest");
    want(c.has(SubjectRef{41, 8}), true, "the new one is there");
    want(c.has(SubjectRef{41, 7}), false, "the old one is gone");
    want(c.has(SubjectRef{42, 7}), false, "including one whose id was never reused");
    want(c.generation() == 8, true, "and the cache says which connection it belongs to");
    std::puts("generation: a move drops the lot, because ids are being reused");
  }

  // --- a picture that failed to load is not a picture ----------------------------------------------
  // "We looked and there was nothing" and "we have not looked" have the same correct answer,
  // but they are different facts. Storing the failure would make has() report a tab it never
  // found art for, and a later retry would have no reason to happen.
  {
    c.put(SubjectRef{50, 8}, FakeTex{""});
    want(c.has(SubjectRef{50, 8}), false, "an empty texture is not stored");
    want(c.size() == 1, true, "and does not grow the cache");
    std::puts("null: a failed load leaves no trace to confuse a retry");
  }

  // --- replacing under one generation ---------------------------------------------------------------
  {
    c.put(SubjectRef{41, 8}, FakeTex{"newer41"});
    want(c.size() == 1, true, "a second put under the same generation replaces, not appends");
    want(c.find(SubjectRef{41, 8})->name == "newer41", true, "and the newer picture wins");
    std::puts("same generation: last put wins");
  }

  // --- clear -------------------------------------------------------------------------------------------
  {
    c.clear();
    want(c.size() == 0, true, "cleared");
    want(c.has(SubjectRef{41, 8}), false, "and nothing is found afterwards");
    want(c.generation() == 0, true, "and it no longer claims a connection");
    std::puts("clear: a picture does not outlive its run");
  }

  // --- slot(), which is how a cached entry is UPDATED -------------------------------------------
  // Added for the label cache, and at first with no check of its own: a slot() that did not
  // clear on a generation move passed the whole suite, because every test above used put().
  // A new entry point with no test is the failure this file exists to prevent, so it has one now.
  {
    SubjectCache<FakeTex> c;
    auto *first = c.slot(SubjectRef{41, 7});
    want(first != nullptr, true, "slot() hands out an entry");
    // slot() CREATES, unlike put() which refuses anything falsy. That is the difference
    // between the two entry points: put takes a value you already hold and skips a null one,
    // slot hands out a place to fill in. An empty entry occupies the key from here on, which
    // is what a label cache wants -- the key is the label's identity, not its contents.
    want(c.has(SubjectRef{41, 7}), true, "and the key it created is now in the cache");
    first->name = "label-one";
    want(c.find(SubjectRef{41, 7})->name == "label-one", true, "and reads back");

    // The connection moves. The entry made under the old one must go, even though nothing was
    // ever put() -- otherwise a label written for last week's tab answers for this one's.
    auto *second = c.slot(SubjectRef{41, 8});
    want(second != nullptr, true, "slot() under the new generation");
    want(second->name.empty(), true, "comes back empty: the old entry is gone");
    want(c.size() == 1, true, "and so is everything cached under the old one");
    std::puts("slot: updates in place, and still drops the lot on a generation move");
  }

  if (failures) {
    std::printf("subject_cache: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("subject_cache: staleness holds");
  return 0;
}
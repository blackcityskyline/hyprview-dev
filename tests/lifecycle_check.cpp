// The lifecycle invariants that were spread across six booleans and five writers.
//
// The two that decide whether the overlay paints at all:
//
//   1. m_active and m_pendingOpen are one axis, not two facts. Nothing said so, so nothing
//      stopped a writer from clearing both while the overlay was mid-open.
//   2. m_opening is NOT on that axis. replayReflow and switchToWorkspace set it on an
//      overlay that is already on screen, to replay the entry animation on changed tiles.
//      That is why nine sites spell `m_active && m_opening` instead of asking one thing.
//
// And the two that were guard chains rather than facts:
//
//   3. Deactivating mid-frame skips the final painted frame and shows one transparent
//      frame. The only caller guards it with `!m_opening && t >= 1.0` -- correct today,
//      because the guard chain is right, not because the type knows.
//   4. The park budget exists so a late overview beats a stuck one, and its whole size is
//      two frames.

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/model/lifecycle.hpp"

using hyprview::model::Lifecycle;
using Own = hyprview::model::Lifecycle::Own;

int main() {
  // --- a fresh lifecycle owns nothing ------------------------------------------------
  {
    Lifecycle l;
    assert(l.closed() && !l.active() && !l.parked());
    assert(!l.ownsScreen());
    assert(!l.opening() && !l.deactivating() && !l.jumpClosing());
    assert(l.parkTicks() == 0);
    std::puts("a fresh lifecycle is closed and inert");
  }

  // --- open parks; park does not count as visible ------------------------------------
  {
    Lifecycle l;
    l.requestOpen();
    // Invariant 3 from the header: the first parked frame already takes the screen away
    // from the windows, but nothing is painted yet.
    assert(l.parked() && !l.active());
    assert(l.ownsScreen());
    assert(l.closed() == false);
    // ...and a parked overlay is not opening, because the entry animation has not begun.
    assert(!l.opening());
    assert(!l.openingNow());
    std::puts("a parked overlay owns the screen without being visible or opening");
  }

  // --- the park budget: two frames, then it opens anyway ------------------------------
  {
    Lifecycle l;
    l.requestOpen();
    // A ready backdrop promotes on the first frame -- no waiting when there is nothing to
    // wait for, which is the case for every compositor whose wallpaper already exists.
    assert(l.parkTick(true));
    // parkTick only REPORTS that the budget is spent. Promotion is a separate transition,
    // which is why the two cannot drift apart into "a frame was counted but the overlay
    // never appeared".
    assert(l.parked());
    assert(l.promote());
    std::puts("park budget: ready on the first frame");
  }
  {
    Lifecycle l;
    l.requestOpen();
    assert(!l.parkTick(false)); // frame 1 of 2 must not promote
    assert(l.parkTicks() == 1);
    assert(l.parkTick(false)); // frame 2 of 2 must promote
    assert(l.parkTicks() == 2);
    // Budget spent, third frame: promotes even though the backdrop is still not ready.
    // A late overview beats a stuck one.
    assert(l.parkTick(false, 2));
    std::puts("park budget: two frames, then it opens anyway");
  }

  // --- promote is a transition, not a flag write ---------------------------------------
  {
    Lifecycle l;
    l.requestOpen();
    assert(l.promote());
    assert(l.active() && !l.parked());
    assert(l.opening());      // promotion IS the start of the entry animation
    assert(l.openingNow());   // ...and on screen, so the conjunction holds here
    // Promoting again is not a transition. Under the old booleans this was just a second
    // `m_active = true` and nobody could tell it apart from the first.
    assert(!l.promote());
    // Nor from Closed.
    Lifecycle c;
    assert(!c.promote());
    std::puts("promote only means anything while parked");
  }

  // --- invariant 2: the entry animation is orthogonal to ownership ---------------------
  // This is the whole reason the two are separate. A reflow replays the entry animation on
  // an overlay that never re-entered Parked.
  {
    Lifecycle l;
    l.requestOpen();
    assert(l.promote());
    l.endEntry();
    assert(l.active() && !l.opening() && !l.openingNow()); // Open

    // replayReflow(): tiles changed, replay the entry.
    l.replayEntry();
    assert(l.active() && l.opening() && l.openingNow());
    // Still on screen -- it never went back through Parked, and never should.
    assert(!l.parked());
    std::puts("a reflow replays entry without re-parking");
  }

  // --- the parked-with-entry-playing combination is reachable, and is not a bug ---------
  // A strip command passes openedOrParked(), not active(), so a workspace switch while
  // parked sets the entry flag on an overlay nobody can see yet. Promote then overwrites
  // it with the same value. Pinned because it is the state most likely to be called a bug
  // by someone reading the booleans for the first time.
  {
    Lifecycle l;
    l.requestOpen();
    l.replayEntry();
    assert(l.parked() && l.opening() && !l.openingNow());
    assert(l.promote());
    assert(l.active() && l.opening());
    std::puts("parked + entry-playing is reachable and survives promote");
  }

  // --- invariant 3: deactivation waits for the final frame -----------------------------
  {
    Lifecycle l;
    // Nothing on screen: there is no frame to hand back.
    assert(!l.requestDeactivate() && "deactivating a closed overlay must be refused");
    assert(!l.deactivating());

    l.requestOpen();
    l.promote();
    assert(l.requestDeactivate());
    assert(l.deactivating());
    assert(l.active()); // still visible -- the overlay is up until the final frame lands
    assert(l.consumeDeactivate());
    assert(!l.deactivating());
    assert(!l.consumeDeactivate()); // only once
    std::puts("deactivate is refused when closed and consumed exactly once");
  }

  // --- a close that arrives before the first visible frame -----------------------------
  {
    Lifecycle l;
    l.requestOpen();
    l.parkTick(true);
    assert(l.abortPark());
    assert(l.closed() && !l.ownsScreen());
    assert(l.parkTicks() == 0);
    // ...and from the screen there is nothing to abort.
    assert(!l.abortPark());
    std::puts("abortPark drops a park, and does nothing once on screen");
  }

  // --- the ws-jump close ------------------------------------------------------------------
  {
    Lifecycle l;
    l.requestOpen();
    l.promote();
    assert(!l.jumpClosing());
    l.setJumpClosing();
    assert(l.jumpClosing());
    assert(l.consumeJumpClosing());
    assert(!l.consumeJumpClosing());
    // Independent of the deactivate request: both can be pending, they are different
    // closes. animBusy() counts them together.
    l.setJumpClosing();
    assert(l.requestDeactivate());
    assert(l.jumpClosing() && l.deactivating());
    std::puts("jump-close and deactivate are independent");
  }

  // --- hardClose is total ----------------------------------------------------------------
  {
    Lifecycle l;
    l.requestOpen();
    l.parkTick(false);
    l.promote();
    l.replayEntry();
    assert(l.requestDeactivate());
    l.setJumpClosing();
    l.hardClose();
    assert(l.closed() && !l.ownsScreen() && !l.opening() && !l.deactivating() &&
           !l.jumpClosing() && l.parkTicks() == 0);
    std::puts("hardClose returns to a virgin lifecycle");
  }

  // --- describe is a data line, ANIM| grammar ---------------------------------------------
  {
    Lifecycle l;
    assert(l.describe() == "own=closed|opening=0");
    l.requestOpen();
    assert(l.describe() == "own=parked|opening=0|parkTicks=0");
    assert(!l.parkTick(false)); // spends frame 1 of 2
    assert(l.describe() == "own=parked|opening=0|parkTicks=1");
    l.promote();
    assert(l.describe() == "own=screen|opening=1");
    l.endEntry();
    l.requestDeactivate();
    l.setJumpClosing();
    assert(l.describe() ==
           "own=screen|opening=0|deactivating=1|jumpClosing=1");

    // Grammar: pipe-separated key=value, non-empty key, one '=', no whitespace -- the
    // property a parser depends on, asserted here so a field cannot be added as prose.
    for (const std::string &line : {l.describe(), Lifecycle{}.describe()}) {
      size_t start = 0;
      for (;;) {
        const size_t bar = line.find('|', start);
        const std::string field = line.substr(
            start, bar == std::string::npos ? std::string::npos : bar - start);
        const size_t eq = field.find('=');
        assert(eq != std::string::npos && ("no '=': " + field).c_str());
        assert(eq > 0 && field.find('=') == eq);
        assert(field.find(' ') == std::string::npos && field.find('\t') == std::string::npos);
        if (bar == std::string::npos)
          break;
        start = bar + 1;
      }
    }
    std::printf("describe: %s\n", l.describe().c_str());
    std::puts("describe is one pipe-separated key=value line");
  }

  std::puts("OK");
  return 0;
}
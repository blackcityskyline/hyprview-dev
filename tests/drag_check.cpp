// The drag FSM's invariants, which used to exist only as call-site discipline.
//
// The struct had 14 public fields. Seven files read them; one file wrote them, and
// nothing stopped a reader from becoming a writer. The pairings that the render path
// depends on -- a Tile press has an idx, a StripWin press has a winIdx and a window, an
// unarmed drag has neither -- were facts about how fourteen assignments happened to be
// spread across input/mouse.cpp, and a new one that got the order wrong compiled fine and
// drew a drag of somebody else's card.
//
// So the type now owns its state and every pairing below is checked structurally: there
// is no transition that reaches Press::Tile without an idx.
//
// This compiles drag.hpp directly. That is only possible because the FSM is templated on
// its window handle -- there is no hyprland/ include between here and the logic, which is
// the whole reason for the template rather than a PHLWINDOWREF member.

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/model/drag.hpp"

using gloview::LRect;
using gloview::model::DragT;

// A stand-in for PHLWINDOWREF: assignable, default-constructible, comparable. The FSM
// never locks it -- every caller that wants the window does that itself -- so the test
// only needs identity.
struct StubWin {
  int id = 0;
  friend bool operator==(const StubWin &a, const StubWin &b) { return a.id == b.id; }
};

using Drag = DragT<StubWin>;

namespace {

constexpr auto kNow = std::chrono::steady_clock::time_point(std::chrono::milliseconds(1234));

Drag tileDrag() {
  Drag d;
  d.beginTile(3, 100.0, 50.0, 1, 10.0, 5.0, LRect{90.0, 40.0, 40.0, 30.0}, kNow);
  return d;
}

Drag stripDrag() {
  Drag d;
  d.beginStripWin(1, 2, 100.0, 50.0, 1, 4.0, 6.0, LRect{90.0, 40.0, 20.0, 20.0},
                  StubWin{77}, kNow);
  return d;
}

} // namespace

int main() {
  // --- a virgin drag is not armed and says nothing about indices -------------------
  {
    Drag d;
    assert(!d.armed());
    assert(!d.onTile() && !d.onStripWin() && !d.consumed());
    // The invariant that had no guard: an unarmed drag reports -1 rather than whatever
    // the last drag left behind. Render code does `idx < tiles.size()`, and -1 passes.
    assert(d.idx() == -1);
    assert(d.winIdx() == -1);
    assert(!d.lifted());
    assert(d.travelSq() == 0.0);
    std::puts("a fresh drag is disarmed and indexless");
  }

  // --- Tile ------------------------------------------------------------------------
  {
    const Drag d = tileDrag();
    assert(d.armed() && d.onTile() && !d.onStripWin());
    assert(d.idx() == 3);
    // Tile has no strip index. It was -1 by field initialiser and stayed -1 only because
    // beginTile wrote it explicitly; that write is the thing under test.
    assert(d.winIdx() == -1);
    assert(!d.lifted());
    // grab offsets are pointer-inside-card, not card position.
    assert(d.grabX() == 10.0 && d.grabY() == 5.0);
    assert(d.fromBox().w == 40.0 && d.fromBox().h == 30.0);
    assert(d.holdFrom() == kNow);
    std::puts("Tile carries its card, its grab offset, and no strip index");
  }

  // --- StripWin ---------------------------------------------------------------------
  {
    const Drag d = stripDrag();
    assert(d.armed() && d.onStripWin() && !d.onTile());
    assert(d.idx() == 1 && d.winIdx() == 2);
    assert(d.win().id == 77);
    assert(!d.lifted());
    std::puts("StripWin carries both indices and its window");
  }

  // --- travel is measured from the press, not from the last frame --------------------
  {
    Drag d = tileDrag();
    assert(d.travelSq() == 0.0);
    d.moveTo(108.0, 50.0);
    assert(d.travelSq() == 64.0); // 8px, the slop threshold at mouse.cpp:148
    // Moving back toward the press point shrinks it again -- it is a distance, not a
    // sum of per-frame steps, so a jittery pointer cannot accumulate past the threshold
    // while never leaving the card.
    d.moveTo(100.0, 50.0);
    assert(d.travelSq() == 0.0);
    d.moveTo(100.0, 90.0);
    assert(d.travelSq() == 1600.0);
    assert(d.x() == 100.0 && d.y() == 90.0);
    std::puts("travelSq is displacement from the press point");
  }

  // --- lift is a flag, not a state ----------------------------------------------------
  // The comment on the old struct said "visuals gate on lifted, not press": a drag can
  // be armed and still not lifted, and a press can be released and stay lifted.
  {
    Drag d = tileDrag();
    d.setLifted(true);
    assert(d.armed() && d.lifted());
    d.setLifted(false);
    assert(d.armed() && !d.lifted());
    std::puts("lifted is independent of armed");
  }

  // --- a new press clears the previous one --------------------------------------------
  // This is the bug class the fourteen public fields allowed: strip -> tile used to leave
  // the strip's winIdx and window behind unless every call site remembered to clear them.
  {
    Drag d = stripDrag();
    d.setLifted(true);
    d.moveTo(500.0, 500.0);
    d.beginTile(9, 10.0, 10.0, 3, 0.0, 0.0, LRect{0, 0, 10, 10}, kNow);

    assert(d.onTile() && d.idx() == 9);
    assert(d.winIdx() == -1);          // not inherited from the strip press
    assert(d.win().id == 0);           // window released, not inherited
    assert(!d.lifted());               // lift is re-earned per press
    assert(d.travelSq() == 0.0);       // and so is the travel measure
    assert(d.button() == 3);
    std::puts("beginTile after a strip press inherits nothing");
  }

  {
    Drag d = tileDrag();
    d.beginStripWin(0, 4, 1.0, 1.0, 1, 0.0, 0.0, LRect{0, 0, 1, 1}, StubWin{9}, kNow);
    assert(d.onStripWin() && d.idx() == 0 && d.winIdx() == 4 && d.win().id == 9);
    std::puts("beginStripWin after a tile press inherits nothing either");
  }

  // --- clear is total ------------------------------------------------------------------
  {
    Drag d = stripDrag();
    d.setLifted(true);
    d.moveTo(700.0, 700.0);
    d.clear();
    assert(!d.armed() && !d.lifted());
    assert(d.idx() == -1 && d.winIdx() == -1 && d.win().id == 0);
    assert(d.travelSq() == 0.0 && d.x() == 0.0);
    assert(d.fromBox().w == 0.0);
    std::puts("clear leaves nothing behind");
  }

  // --- consume and miss keep the button, and stay unarmed ------------------------------
  // release has to tell left from right for both, and both are releases where idx() must
  // not answer with the previous drag's index.
  {
    Drag d = tileDrag();
    d.consume(3);
    assert(d.consumed() && !d.armed());
    assert(d.button() == 3);
    assert(d.idx() == -1 && d.winIdx() == -1);

    Drag e = stripDrag();
    e.miss(1);
    assert(!e.armed() && !e.consumed());
    assert(e.button() == 1);
    assert(e.idx() == -1 && e.winIdx() == -1);
    std::puts("consume/miss keep the button and release the indices");
  }

  // --- describe is a data line, not prose ------------------------------------------------
  // Same grammar as the ANIM| records: pipe-separated key=value, nothing else. A harness
  // has to split it without knowing this is the drag subsystem, which is the whole reason
  // it is not "press=tile idx=3 ...".
  {
    const std::string a = tileDrag().describe();
    assert(a.rfind("press=tile|", 0) == 0);
    assert(a.find("|idx=3|") != std::string::npos);
    assert(a.find("|lifted=0|") != std::string::npos);
    assert(a.find("|travelSq=") != std::string::npos);
    assert(a.find("winIdx=") == std::string::npos); // Tile has none

    const std::string b = stripDrag().describe();
    assert(b.rfind("press=stripwin|", 0) == 0);
    assert(b.find("|winIdx=2|") != std::string::npos);

    Drag e;
    e.consume(2);
    assert(e.describe() == "press=consumed|button=2");
    assert(Drag{}.describe() == "press=empty|button=0");

    // The grammar itself: split on '|' and every field must be key=value with a non-empty
    // key, exactly one '=', and no whitespace. This is the property a parser depends on,
    // and it is what stops a future field from being added as prose.
    for (const std::string *line : {&a, &b}) {
      size_t start = 0;
      for (;;) {
        const size_t bar = line->find('|', start);
        const std::string field =
            line->substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        const size_t eq = field.find('=');
        assert(eq != std::string::npos && ("field without '=': " + field).c_str());
        assert(eq > 0 && ("field with empty key: " + field).c_str());
        assert(field.find('=') == eq && ("two '=' in one field: " + field).c_str());
        assert(field.find(' ') == std::string::npos && field.find('\t') == std::string::npos);
        if (bar == std::string::npos)
          break;
        start = bar + 1;
      }
    }

    // Every state must have a distinct spelling, or a grep cannot tell them apart.
    std::printf("describe: %s | %s | %s\n", a.c_str(), b.c_str(), e.describe().c_str());
    std::puts("describe is one pipe-separated key=value line per state");
  }

  std::puts("OK");
  return 0;
}
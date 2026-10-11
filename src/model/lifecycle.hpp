#pragma once

// Who owns the screen, and whether the entry animation is running.
//
// Six booleans used to carry this: m_active, m_opening, m_pendingOpen, m_pendingTicks,
// m_pendingDeactivate, m_pendingJumpClose. Five files wrote them and nine read them, and
// two facts that decide whether the overlay paints at all were spread across the writes:
//
//   - m_active and m_pendingOpen are the same axis. open() sets one and clears the other,
//     primeBackdrop() flips them, close() clears both. Nothing said so, so nothing stopped
//     a future writer from setting both false while the overlay was mid-open.
//   - m_opening is NOT that axis. Three reflow sites (replayReflow, switchToWorkspace x2)
//     set it on an overlay that is already on screen, to replay the entry animation on
//     tiles that changed. Reading it as "is opening" is why the calls are written
//     `m_active && m_opening` at nine sites instead of asking one thing.
//
// So the ownership axis is a 3-value state and the entry animation is a flag beside it,
// which is what the code actually does. Neither is Hyprland-bound, so this header is
// reachable from a test -- which is the only reason the two invariants below are checked
// at all rather than remembered.

#include <string>

namespace gloview::model {

class Lifecycle {
public:
  // The screen axis. Parked is the state that is neither visible nor closed: the overlay
  // is coming up but the backdrop composite is not ready yet, and paint() draws nothing.
  //
  // Parked counts as owned for focus (AGENTS.md rule 10): the windows must lose focus on
  // the first parked frame, not when the overlay finally appears. It does NOT count as
  // active for painting. Those are different questions, which is why they are two methods
  // and not one flag.
  enum class Own : int { Closed, Parked, Screen };

  // --- transitions -------------------------------------------------------------------

  // open(): take the screen, but park until the backdrop exists. Parking rather than going
  // straight to Screen is what puts a backdrop-less frame on screen -- it used to be done
  // in open() by hiding layers, which is why they are hidden on promote instead.
  void requestOpen() {
    m_own          = Own::Parked;
    m_opening      = false;
    m_deactivating = false;
    m_jumpClosing  = false;
    m_parkTicks    = 0;
  }

  // One parked frame. Returns true when the overlay may finally become visible.
  //
  // budget is how many parked frames to spend waiting. Two is the whole budget: after that
  // it opens anyway, because a late overview beats a stuck one, and the frame it gives up
  // is the single black frame this arrangement exists to avoid.
  bool parkTick(bool backdropReady, int budget = 2) {
    ++m_parkTicks;
    return backdropReady || m_parkTicks >= budget;
  }

  // primeBackdrop(): a composite exists. Only legal from Parked -- promoting a closed or
  // already-visible overlay is not a transition, and doing it anyway is the bug this
  // return value now makes impossible.
  bool promote() {
    if (m_own != Own::Parked)
      return false;
    m_own     = Own::Screen;
    m_opening = true;
    return true;
  }

  // A close that arrived before the first visible frame: nothing to animate away, so drop
  // the park instead of running the close animation over an overlay nobody saw.
  bool abortPark() {
    if (m_own != Own::Parked)
      return false;
    m_own       = Own::Closed;
    m_parkTicks = 0;
    return true;
  }

  // The entry animation is running. Reachable two ways: from promote() above, and from the
  // reflow sites on an overlay that is already on screen. Both go through here, which is
  // why the flag is not derivable from the ownership axis.
  void replayEntry() { m_opening = true; }
  void endEntry() { m_opening = false; }

  // Ask for the overlay to go away. The wait is the point: deactivating mid-frame skips
  // the final painted frame and the screen shows one transparent frame (AGENTS.md rule 9).
  //
  // Refuses when nothing is on screen. There is nothing to hand back in that case, and
  // the one caller already guards it by `!m_opening && t >= 1.0` -- so the refusal changes
  // no behaviour and turns "the guard chain happens to be right" into a fact of the type.
  bool requestDeactivate() {
    if (m_own == Own::Closed)
      return false;
    m_deactivating = true;
    return true;
  }

  // finishPendingDeactivate(): the final frame is painted, hand the screen back.
  bool consumeDeactivate() {
    if (!m_deactivating)
      return false;
    m_deactivating = false;
    return true;
  }

  // A digit-jump ws-slide, which is a close that has to finish before it may hand over.
  void setJumpClosing() { m_jumpClosing = true; }
  bool consumeJumpClosing() {
    if (!m_jumpClosing)
      return false;
    m_jumpClosing = false;
    return true;
  }

  // hardClose(): everything back to the start, whatever state it was in.
  void hardClose() { *this = Lifecycle{}; }

  // --- queries -----------------------------------------------------------------------
  [[nodiscard]] Own own() const { return m_own; }
  [[nodiscard]] bool closed() const { return m_own == Own::Closed; }
  [[nodiscard]] bool parked() const { return m_own == Own::Parked; }
  // Visible: the compositor should be drawing the overlay. A parked overlay is NOT.
  [[nodiscard]] bool active() const { return m_own == Own::Screen; }
  // Owns input: parked counts, because the windows must already have lost focus.
  [[nodiscard]] bool ownsScreen() const { return m_own != Own::Closed; }
  [[nodiscard]] bool opening() const { return m_opening; }
  [[nodiscard]] bool deactivating() const { return m_deactivating; }
  [[nodiscard]] bool jumpClosing() const { return m_jumpClosing; }
  [[nodiscard]] int parkTicks() const { return m_parkTicks; }

  // The `active && opening` pair, which nine sites spelled out. They are not redundant:
  // the reflow paths have opening without a fresh open, so the conjunction is a real
  // question and asking it through one name keeps the answer consistent.
  [[nodiscard]] bool openingNow() const { return m_own == Own::Screen && m_opening; }

  // One pipe-separated key=value line, same grammar as ANIM| and DRAG|. `parkTicks` is
  // only meaningful while parked, and `deactivating` only while on screen -- so both are
  // emitted conditionally rather than as zeros that read like facts.
  [[nodiscard]] std::string describe() const {
    std::string s = "own=";
    s += m_own == Own::Closed ? "closed" : m_own == Own::Parked ? "parked" : "screen";
    s += std::string("|opening=") + (m_opening ? "1" : "0");
    if (m_own == Own::Parked)
      s += "|parkTicks=" + std::to_string(m_parkTicks);
    if (m_deactivating)
      s += "|deactivating=1";
    if (m_jumpClosing)
      s += "|jumpClosing=1";
    return s;
  }

private:
  Own m_own = Own::Closed;
  bool m_opening      = false;
  bool m_deactivating = false;
  bool m_jumpClosing  = false;
  int  m_parkTicks    = 0;
};

} // namespace gloview::model
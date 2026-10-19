#pragma once

// Who owns the screen, and whether the entry animation is running. Ownership is a 3-value
// state, the entry animation a flag beside it -- m_opening is NOT on the ownership axis,
// which is why sites ask `openingNow()` instead of combining two flags. Neither is
// Hyprland-bound, so a test can reach the header, which is the only reason the invariants
// are checked rather than remembered.

#include <string>

namespace gloview::model {

class Lifecycle {
public:
  // The screen axis. Parked is neither visible nor closed: the overlay is coming up but the
  // backdrop composite is not ready, and paint() draws nothing. Parked counts as owned for
  // focus (AGENTS.md rule 10) because the windows must lose focus on the first parked frame,
  // and does NOT count as active for painting -- two questions, two methods, not one flag.
  enum class Own : int { Closed, Parked, Screen };

  // --- transitions -------------------------------------------------------------------

  // open(): take the screen, but park until the backdrop exists -- going straight to Screen
  // puts a backdrop-less frame on screen. Layers are hidden on promote, not here.
  void requestOpen() {
    m_own          = Own::Parked;
    m_opening      = false;
    m_deactivating = false;
    m_jumpClosing  = false;
    m_parkTicks    = 0;
  }

  // One parked frame. Returns true when the overlay may finally become visible. Two is the
  // whole budget: after that it opens anyway, because a late overview beats a stuck one, and
  // the frame it gives up is the single black one this exists to avoid.
  bool parkTick(bool backdropReady, int budget = 2) {
    ++m_parkTicks;
    return backdropReady || m_parkTicks >= budget;
  }

  // primeBackdrop(): a composite exists. Only legal from Parked -- promoting a closed or
  // already-visible overlay is not a transition.
  bool promote() {
    if (m_own != Own::Parked)
      return false;
    m_own     = Own::Screen;
    m_opening = true;
    return true;
  }

  // A close arrived before the first visible frame: nothing to animate away.
  bool abortPark() {
    if (m_own != Own::Parked)
      return false;
    m_own       = Own::Closed;
    m_parkTicks = 0;
    return true;
  }

  // Reachable two ways -- promote(), and the reflow sites on an overlay already on screen --
  // which is why the flag is not derivable from the ownership axis.
  void replayEntry() { m_opening = true; }
  void endEntry() { m_opening = false; }

  // Ask for the overlay to go away. The wait is the point: deactivating mid-frame skips the
  // final painted frame and the screen shows one transparent frame (AGENTS.md rule 9).
  // Refuses when nothing is on screen, so the caller's guard is a fact of the type.
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

  // A digit-jump ws-slide: a close that must finish before it may hand over.
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

  // The `active && opening` pair nine sites spelled out. Not redundant: the reflow paths have
  // opening without a fresh open, so the conjunction is a real question.
  [[nodiscard]] bool openingNow() const { return m_own == Own::Screen && m_opening; }

  // One pipe-separated key=value line, same grammar as ANIM| and DRAG|. parkTicks is only
  // meaningful while parked and deactivating only while on screen, so both are emitted
  // conditionally rather than as zeros that read like facts.
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
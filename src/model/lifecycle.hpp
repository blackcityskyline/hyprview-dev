#pragma once

// Ownership is a 3-value state; m_opening is a flag BESIDE it, not on the axis -- sites ask
// openingNow(), not the pair. No Hyprland type here, so a test can reach the invariants.

#include <string>

namespace hyprview::model {

class Lifecycle {
public:
  // Parked: the overlay is coming up but the backdrop composite is not ready, and paint()
  // draws nothing. Parked still owns focus (AGENTS.md 2.14); active() answers visibility only.
  enum class Own : int { Closed, Parked, Screen };

  // Park until the backdrop exists: going straight to Screen puts a backdrop-less frame up.
  void requestOpen() {
    m_own          = Own::Parked;
    m_opening      = false;
    m_deactivating = false;
    m_jumpClosing  = false;
    m_parkTicks    = 0;
  }

  // Two parked frames is the whole budget: a late overview beats a stuck one.
  bool parkTick(bool backdropReady, int budget = 2) {
    ++m_parkTicks;
    return backdropReady || m_parkTicks >= budget;
  }

  bool promote() {
    if (m_own != Own::Parked)
      return false;
    m_own     = Own::Screen;
    m_opening = true;
    return true;
  }

  bool abortPark() {
    if (m_own != Own::Parked)
      return false;
    m_own       = Own::Closed;
    m_parkTicks = 0;
    return true;
  }

  // Not derivable from the ownership axis: the reflow sites set opening on an overlay that
  // is already on screen.
  void replayEntry() { m_opening = true; }
  void endEntry() { m_opening = false; }

  // The wait is the point: deactivating mid-frame shows one transparent frame (AGENTS.md 2.13).
  bool requestDeactivate() {
    if (m_own == Own::Closed)
      return false;
    m_deactivating = true;
    return true;
  }

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

  void hardClose() { *this = Lifecycle{}; }

  [[nodiscard]] Own own() const { return m_own; }
  [[nodiscard]] bool closed() const { return m_own == Own::Closed; }
  [[nodiscard]] bool parked() const { return m_own == Own::Parked; }
  [[nodiscard]] bool active() const { return m_own == Own::Screen; }
  // Parked counts: the windows must already have lost focus (AGENTS.md 2.14).
  [[nodiscard]] bool ownsScreen() const { return m_own != Own::Closed; }
  [[nodiscard]] bool opening() const { return m_opening; }
  [[nodiscard]] bool deactivating() const { return m_deactivating; }
  [[nodiscard]] bool jumpClosing() const { return m_jumpClosing; }
  [[nodiscard]] int parkTicks() const { return m_parkTicks; }

  // The `active && opening` pair nine sites spelled out.
  [[nodiscard]] bool openingNow() const { return m_own == Own::Screen && m_opening; }

  // Key=value, pipe-separated, same grammar as ANIM|.
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

} // namespace hyprview::model

#pragma once

#include "subject.hpp"

#include "../anim_types.hpp"
#include "../clock/clocks.hpp"
#include "../layout.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace hyprview::model {

struct Ghost {
  SubjectRef subj;
  LRect box;
};

// `p` accrues per FRAME, not wall-clock
struct WinPulse {
  SubjectRef subj;
  double p = 0.0;
  std::chrono::steady_clock::time_point last;
};

struct SwapFX {
  SubjectRef subj;
  LRect from;
  // One live flight per surface: a window in the all-workspaces view is drawn twice at once.
  bool        toStrip = false;
  anim::Move  style{};
  double ms = 320;
  std::string curve = "easeinout";
  anim::Tween clock;
  // Pairs ANIM|begin with ANIM|end; 0 = tracing off, or the flight had nowhere to land.
  int         traceSeq = 0;
  std::chrono::steady_clock::time_point traceStart{};
};

class Effects {
  public:
    [[nodiscard]] const std::vector<Ghost> &ghosts() const { return m_ghosts; }
    [[nodiscard]] const std::vector<WinPulse> &pulses() const {
      return m_pulses;
    }
    [[nodiscard]] const std::vector<SwapFX> &swapfx() const { return m_swapfx; }

    // Each surface stands down only for its own flight.
    [[nodiscard]] bool swapActive(SubjectRef s, bool strip) const {
      for (const auto &l : m_swapfx)
        if (l.subj == s && l.toStrip == strip)
          return true;
      return false;
    }

    // Replaces any pulse it already has: pulses accrue per FRAME, so re-kicking one in flight
    // restarts it rather than stacking a second ring.
    void kickPulse(SubjectRef s) {
      dropPulse([s](const WinPulse &p) { return p.subj == s; });
      addPulse(model::WinPulse{s, 0.0, std::chrono::steady_clock::now()});
    }
    void addGhost(const Ghost &g) { m_ghosts.push_back(g); }
    void addPulse(const WinPulse &p) { m_pulses.push_back(p); }
    void addSwap(SwapFX &&fx) { m_swapfx.push_back(std::move(fx)); }
    void clearGhosts() { m_ghosts.clear(); }

    // beginSwapFX() may reuse a slot rather than append, so the caller that knows the real
    // origin pins it afterwards.
    bool retargetLastSwap(const LRect &from) {
      if (m_swapfx.empty())
        return false;
      m_swapfx.back().from = from;
      return true;
    }

    template <class F> void eachPulse(F &&f) {
      for (auto &p : m_pulses)
        f(p);
    }
    template <class F> void eachSwap(F &&f) {
      for (auto &fx : m_swapfx)
        f(fx);
    }

    template <class P> void dropPulse(P pred) {
      std::erase_if(m_pulses, pred);
    }
    template <class P> void dropSwap(P pred) {
      std::erase_if(m_swapfx, pred);
    }
  private:
    std::vector<Ghost> m_ghosts;
    std::vector<WinPulse> m_pulses;
    std::vector<SwapFX> m_swapfx;
};

} // namespace hyprview::model

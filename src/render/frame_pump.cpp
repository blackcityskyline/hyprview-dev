#include "frame_pump.hpp"

#include <hyprland/src/render/Renderer.hpp>

#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>

namespace hyprview::render {

namespace {
// tick BETWEEN frames, not during a commit
constexpr auto kPumpInterval = std::chrono::milliseconds(8);
}

FramePump::~FramePump() { stop(); }

void FramePump::stop() {
  m_armed = false;
  if (!m_timer)
    return;
  m_timer->cancel();
  if (g_pEventLoopManager)
    g_pEventLoopManager->removeTimer(m_timer);
  // NOT reset(): dropping the last shared pointer destroys the timer, and this object is reachable from its callback -- SIGSEGV 2026-10-13, _delete -> nudgeTimers.
}

void FramePump::damage(const PHLMONITOR &m) const {
  if (m && g_pHyprRenderer)
    g_pHyprRenderer->damageMonitor(m);
}

void FramePump::ensure(bool busy, const PHLMONITOR &mon) {
  if (!busy) {
    // Disarm, never reset (SIGSEGV 2026-10-13), and clear the flag the callback re-arms on.
    m_armed = false;
    if (m_timer)
      m_timer->cancel();
    return;
  }
  if (m_timer && m_timer->armed()) {
    m_armed = true;
    return;
  }

  m_armed = true;
  m_timer = makeShared<CEventLoopTimer>(
      kPumpInterval,
      [this, mon](SP<CEventLoopTimer> self, void *) {
        if (!m_armed) {
          self->cancel();
          return;
        }
        damage(mon);
        if (mon)
          mon->scheduleFrame();
        self->updateTimeout(kPumpInterval);
      },
      nullptr);
  if (g_pEventLoopManager)
    g_pEventLoopManager->addTimer(m_timer);
}

} // namespace hyprview::render
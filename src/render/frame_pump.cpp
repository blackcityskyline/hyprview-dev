#include "frame_pump.hpp"

#include <hyprland/src/render/Renderer.hpp>

#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>

namespace gloview::render {

namespace {
// tick BETWEEN frames, not during a commit
constexpr auto kPumpInterval = std::chrono::milliseconds(8);
} // namespace

FramePump::~FramePump() { stop(); }

void FramePump::stop() {
  if (!m_timer)
    return;
  m_timer->cancel();
  if (g_pEventLoopManager)
    g_pEventLoopManager->removeTimer(m_timer);
  // NOT reset(): dropping the last shared pointer destroys the timer, and this object is
  // reachable from its callback -- SIGSEGV 2026-10-13, _delete -> nudgeTimers.
}

void FramePump::damage(const PHLMONITOR &m) const {
  if (m && g_pHyprRenderer)
    g_pHyprRenderer->damageMonitor(m);
}

void FramePump::ensure(bool busy, const PHLMONITOR &mon) {
  if (!busy) {
    // Disarm, never reset: this runs from inside the timer's own callback (SIGSEGV 2026-10-13).
    if (m_timer)
      m_timer->cancel();
    return;
  }
  if (m_timer && m_timer->armed())
    return;

  m_timer = makeShared<CEventLoopTimer>(
      kPumpInterval,
      [this, mon](SP<CEventLoopTimer> self, void *) {
        damage(mon);
        if (mon)
          mon->scheduleFrame();
        self->updateTimeout(kPumpInterval);
      },
      nullptr);
  if (g_pEventLoopManager)
    g_pEventLoopManager->addTimer(m_timer);
}

} // namespace gloview::render
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
  // NOT reset(). Dropping the last shared pointer destroys the timer, and this object is
  // reachable from the timer callback: if that is the stack we are on, the destruction
  // happens inside CEventLoopManager::nudgeTimers(), which then walks freed memory. That
  // is the SIGSEGV that killed the compositor on 2026-10-13:
  //   CSharedPointer<CEventLoopTimer>::_delete -> nudgeTimers -> wl_event_loop_dispatch_idle
  // The timer outlives us instead, disarmed and no longer in the loop's list.
}

void FramePump::damage(const PHLMONITOR &m) const {
  if (m && g_pHyprRenderer)
    g_pHyprRenderer->damageMonitor(m);
}

void FramePump::ensure(bool busy, const PHLMONITOR &mon) {
  if (!busy) {
    // Disarm, never reset. This runs from inside the timer OWN callback, while the event
    // loop iterates its timer list: dropping the last shared pointer here destroys the timer
    // from inside CEventLoopManager::nudgeTimers(), which then walks freed memory. This is
    // the SIGSEGV that killed the compositor on 2026-10-13:
    //   CSharedPointer<CEventLoopTimer>::_delete -> nudgeTimers -> wl_event_loop_dispatch_idle
    // The timer lives as long as this object; it is re-armed below when work resumes.
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
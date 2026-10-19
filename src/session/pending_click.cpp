#include "pending_click.hpp"

#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>

namespace gloview::session {

bool PendingClick::arm(std::chrono::milliseconds ms, const PHLWINDOW &w,
                       const std::function<void(PHLWINDOW)> &onFire) {
  if (!w || !g_pEventLoopManager)
    return false;
  cancel(); // drop any OTHER window's still-pending timer first
  m_win = w;
  m_timer = makeShared<CEventLoopTimer>(
      ms,
      [this, onFire](SP<CEventLoopTimer>, void *) {
        const auto pw = m_win.lock();
        // Disarm BEFORE the callback: it can close a window, tear the owner down and destroy
        // this, so nothing here may touch a member after onFire or leave the timer armed.
        cancel();
        if (pw)
          onFire(pw);
      },
      nullptr);
  g_pEventLoopManager->addTimer(m_timer);
  return true;
}

void PendingClick::cancel() {
  m_win.reset();
  if (!m_timer)
    return;
  m_timer->cancel();
  if (g_pEventLoopManager)
    g_pEventLoopManager->removeTimer(m_timer);
  m_timer.reset();
}

} // namespace gloview::session
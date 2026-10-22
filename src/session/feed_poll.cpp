#include "feed_poll.hpp"

#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>

namespace hyprview::session {

bool FeedPoll::start(std::chrono::milliseconds ms, std::function<void()> onTick) {
  if (!g_pEventLoopManager)
    return false;
  cancel();
  m_timer = makeShared<CEventLoopTimer>(
      ms,
      [ms, onTick](SP<CEventLoopTimer> self, void *) {
        if (onTick)
          onTick();
        self->updateTimeout(ms);
      },
      nullptr);
  g_pEventLoopManager->addTimer(m_timer);
  return true;
}

void FeedPoll::cancel() {
  if (!m_timer)
    return;
  m_timer->cancel();
  if (g_pEventLoopManager)
    g_pEventLoopManager->removeTimer(m_timer);
  m_timer.reset();
}

}

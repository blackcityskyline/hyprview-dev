#pragma once

#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <chrono>
#include <functional>

namespace hyprview::session {

class FeedPoll {
  public:
    FeedPoll() = default;
    ~FeedPoll() { cancel(); }

    FeedPoll(const FeedPoll &)            = delete;
    FeedPoll &operator=(const FeedPoll &) = delete;

    bool start(std::chrono::milliseconds ms, std::function<void()> onTick);
    void cancel();

    [[nodiscard]] bool running() const { return static_cast<bool>(m_timer); }

  private:
    SP<CEventLoopTimer> m_timer;
};

}

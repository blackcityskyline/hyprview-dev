#pragma once

#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <cstdint>

namespace hyprview::render {

class FramePump {
  public:
    FramePump() = default;
    ~FramePump();

    FramePump(const FramePump &)            = delete;
    FramePump &operator=(const FramePump &) = delete;

    void damage(const PHLMONITOR &m) const;

    void ensure(bool busy, const PHLMONITOR &m);

    // Cancels AND removes the timer, leaving the last shared pointer alone: the timer must
    // never fire against a destroyed owner (onTimerFire walks a COPY of its timer list).
    void stop();

    // The stall guard reads this, compensating for a hole in the frame stream.
    [[nodiscard]] std::uint64_t tick() const { return m_tick; }
    void tickFrame() { ++m_tick; }

  private:
    SP<CEventLoopTimer> m_timer;
    std::uint64_t m_tick = 0;
};

} // namespace hyprview::render
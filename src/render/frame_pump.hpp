#pragma once

#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <cstdint>

namespace gloview::render {

// Two ways to make the compositor draw another frame, kept out of Overview so nothing reaches
// into the lifecycle to say "redraw". The pump takes ONE boolean, "is there work", and stays a
// leaf because the caller already knows what the work is.
class FramePump {
  public:
    FramePump() = default;
    ~FramePump();

    FramePump(const FramePump &)            = delete;
    FramePump &operator=(const FramePump &) = delete;

    void damage(const PHLMONITOR &m) const;

    // Arms the timer while `busy`, disarms otherwise. Idempotent.
    void ensure(bool busy, const PHLMONITOR &m);

    // Cancels AND removes the timer, leaving the last shared pointer alone: the timer must
    // never fire against a destroyed owner. Safe from inside its own callback --
    // CEventLoopManager::onTimerFire walks a COPY of its timer list.
    void stop();

    // The stall guard reads this, compensating for a hole in the frame stream.
    [[nodiscard]] std::uint64_t tick() const { return m_tick; }
    void tickFrame() { ++m_tick; }

  private:
    SP<CEventLoopTimer> m_timer;
    std::uint64_t m_tick = 0;
};

} // namespace gloview::render
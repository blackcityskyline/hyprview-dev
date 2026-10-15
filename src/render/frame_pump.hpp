#pragma once

#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <cstdint>

namespace gloview::render {

// Two ways to make the compositor draw another frame.
//
// They are here rather than on Overview because twenty-four files ask for a damage and
// nine ask for the pump, and neither is the run's lifecycle -- an action, a hit-test and a
// hover all need a repaint. Owning them here means nothing reaches into the lifecycle to
// say "redraw".
//
// The pump takes ONE boolean, "is there work". It does not know what the work is, which is
// what keeps it a leaf: the caller already knows, and passing the question along instead of
// the answer is how the answer ends up needing the caller back.
class FramePump {
  public:
    FramePump() = default;
    ~FramePump();

    FramePump(const FramePump &)            = delete;
    FramePump &operator=(const FramePump &) = delete;

    void damage(const PHLMONITOR &m) const;

    // Arms the repeating timer while `busy`, disarms it otherwise. Idempotent: calling it
    // with an unchanged `busy` does nothing.
    void ensure(bool busy, const PHLMONITOR &m);

    // Cancels AND removes the timer from the event loop, leaving the last shared pointer
    // alone. This is what teardown wants: the timer must never fire again against a
    // destroyed owner.
    //
    // Safe to call from anywhere, including from inside the timer's own callback:
    // CEventLoopManager::onTimerFire iterates a COPY of its timer list, so removing from
    // the real one does not disturb the walk.
    void stop();

    // The stall guard reads this; see clock::Clocks' compensates a hole in the frame stream.
    [[nodiscard]] std::uint64_t tick() const { return m_tick; }
    void tickFrame() { ++m_tick; }

  private:
    SP<CEventLoopTimer> m_timer;
    std::uint64_t m_tick = 0;
};

} // namespace gloview::render
#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <chrono>
#include <functional>

namespace gloview::session {

// close_trigger == "doubleclick": the first click waits behind a timer, and that armed
// timer IS the record. A second click on the same window before it fires closes it.
//
// The timer is the state. There is no "a click is pending" flag that could disagree with
// it, and no way to be pending without an armed callback -- which is why cancel() must run
// before the owner is destroyed, and why the destructor does it too.
class PendingClick {
  public:
    PendingClick() = default;
    ~PendingClick() { cancel(); }

    PendingClick(const PendingClick &)            = delete;
    PendingClick &operator=(const PendingClick &) = delete;

    [[nodiscard]] bool armedFor(const PHLWINDOW &w) const {
      return m_timer && m_win.lock() == w;
    }

    // Arms for `w`, dropping whatever was pending. Returns false when there is no event
    // loop to arm against, which leaves nothing pending rather than a timer that never
    // fires.
    bool arm(std::chrono::milliseconds ms, const PHLWINDOW &w,
             const std::function<void(PHLWINDOW)> &onFire);

    void cancel();

    [[nodiscard]] bool armed() const { return static_cast<bool>(m_timer); }

  private:
    PHLWINDOWREF m_win;
    SP<CEventLoopTimer> m_timer;
};

} // namespace gloview::session
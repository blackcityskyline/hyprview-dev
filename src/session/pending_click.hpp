#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <chrono>
#include <functional>

namespace hyprview::session {

// close_trigger == "doubleclick": the armed timer IS the state -- no flag can disagree with it.
// Nothing can be pending without an armed callback, so the destructor cancels too.
class PendingClick {
  public:
    PendingClick() = default;
    ~PendingClick() { cancel(); }

    PendingClick(const PendingClick &)            = delete;
    PendingClick &operator=(const PendingClick &) = delete;

    [[nodiscard]] bool armedFor(const PHLWINDOW &w) const {
      return m_timer && m_win.lock() == w;
    }

    // Arms for `w`, dropping whatever was pending; false when there is no event loop to arm against.
    bool arm(std::chrono::milliseconds ms, const PHLWINDOW &w,
             const std::function<void(PHLWINDOW)> &onFire);

    void cancel();

    [[nodiscard]] bool armed() const { return static_cast<bool>(m_timer); }

  private:
    PHLWINDOWREF m_win;
    SP<CEventLoopTimer> m_timer;
};

}

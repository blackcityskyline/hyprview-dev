#pragma once

// Hyprland forwards physical presses only -- repeat is a Wayland client thing -- so a held arrow
// arrives once. This timer IS the repeat, on input:repeat_*, until the key is released.

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

namespace hyprview::input {

class KeyRepeat {
  public:
    KeyRepeat() = default;
    ~KeyRepeat() { cancel(); }

    KeyRepeat(const KeyRepeat &)            = delete;
    KeyRepeat &operator=(const KeyRepeat &) = delete;

    // The caller has already stepped on the press; this only asks again while the key stays down.
    void arm(int keycode, std::function<void()> step) {
      cancel();
      static auto PDELAY = CConfigValue<Hyprlang::INT>("input:repeat_delay");
      static auto PRATE  = CConfigValue<Hyprlang::INT>("input:repeat_rate");
      const int rate = static_cast<int>(*PRATE);
      if (!g_pEventLoopManager || rate <= 0) // 0 is Hyprland's own "no repeat"
        return;

      m_keycode  = keycode;
      m_step     = std::move(step);
      m_interval = std::chrono::milliseconds(std::max(1000 / rate, 1));
      m_timer    = makeShared<CEventLoopTimer>(
          std::chrono::milliseconds(std::max(static_cast<int>(*PDELAY), 1)),
          [this](SP<CEventLoopTimer> self, void *) {
            if (!m_step) {
              self->cancel();
              return;
            }
            m_step();
            self->updateTimeout(m_interval);
          },
          nullptr);
      g_pEventLoopManager->addTimer(m_timer);
    }

    void release(int keycode) {
      if (keycode == m_keycode)
        cancel();
    }

    void cancel() {
      m_keycode = -1;
      m_step    = nullptr;
      if (!m_timer)
        return;
      m_timer->cancel();
      if (g_pEventLoopManager)
        g_pEventLoopManager->removeTimer(m_timer);
      m_timer.reset();
    }

  private:
    int                       m_keycode = -1;
    std::chrono::milliseconds m_interval{50};
    SP<CEventLoopTimer>       m_timer;
    std::function<void()>     m_step;
};

}

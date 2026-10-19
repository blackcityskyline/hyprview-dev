#pragma once

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace gloview::session {

// Function hooks are global: left installed after unload, the next shouldRenderWindow runs into
// freed code. Event listeners die with the object. This type owns both; the destructor removes.
class Hooks {
  public:
    Hooks() = default;
    ~Hooks();

    Hooks(const Hooks &)            = delete;
    Hooks &operator=(const Hooks &) = delete;

    // False when a required hook is missing, having already notified the user.
    bool install(HANDLE handle, void *self);

    // The originals, valid only after install() returned true.
    [[nodiscard]] void *shouldRenderOrig() const { return m_shouldRenderOrig; }
    [[nodiscard]] void *damageSurfaceOrig() const { return m_damageSurfaceOrig; }
    [[nodiscard]] void *setPointerFocusOrig() const { return m_setPointerFocusOrig; }
    [[nodiscard]] void *setKeyboardFocusOrig() const {
      return m_setKeyboardFocusOrig;
    }

  private:
    HANDLE m_handle = nullptr;
    CFunctionHook *m_shouldRenderHook = nullptr;
    CFunctionHook *m_damageSurfaceHook = nullptr;
    CFunctionHook *m_setPointerFocusHook = nullptr;
    CFunctionHook *m_setKeyboardFocusHook = nullptr;
    CHyprSignalListener m_renderStageL;
    CHyprSignalListener m_mouseButtonL;
    CHyprSignalListener m_mouseAxisL;
    CHyprSignalListener m_mouseMoveL;
    CHyprSignalListener m_keyL;
    void *m_shouldRenderOrig = nullptr;
    void *m_damageSurfaceOrig = nullptr;
    void *m_setPointerFocusOrig = nullptr;
    void *m_setKeyboardFocusOrig = nullptr;
};

}

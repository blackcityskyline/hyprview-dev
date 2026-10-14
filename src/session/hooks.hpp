#pragma once

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace gloview::session {

// Everything that attaches to the compositor and has to come back off it.
//
// Two kinds, and they fail differently. Function hooks are global: leaving one installed
// after the plugin is unloaded sends the next shouldRenderWindow straight into freed code.
// Event listeners are held by value and die with the object, so they only need to exist as
// long as the owner does.
//
// This type owns both, and its destructor removes the hooks. That asymmetry is the whole
// point of holding them: nothing else in the plugin has to know which of them are global.
class Hooks {
  public:
    Hooks() = default;
    ~Hooks();

    Hooks(const Hooks &)            = delete;
    Hooks &operator=(const Hooks &) = delete;

    // Returns false when a required hook is missing, having already notified the user.
    // The destructor removes whatever did get installed.
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

} // namespace gloview::session
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <utility>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

namespace {
using PSHOULDRENDER = bool (*)(void *, PHLWINDOW, PHLMONITOR);
PSHOULDRENDER g_shouldRenderOrig = nullptr;

// --- input routing ---------------------------------------------------------
//
// shouldRenderWindow hides the real windows, but it hides them from the RENDERER
// only. CSeatManager still hands them pointer and keyboard focus, so a window under
// the cursor receives enter and motion while the overview is on screen, and a keypress
// lands in whatever Hyprland happened to have focused. Both are invisible to
// shouldRenderWindow and neither is reachable through Event::bus(): focus is not an
// event the bus carries.
//
// These are the two functions where input BECOMES addressed, which is why they are
// hooked rather than the send* calls: blocking a send leaves the seat believing the
// window still has focus, and the compositor keeps routing to it.
using PSETKEYFOCUS = void (*)(void *, SP<CWLSurfaceResource>);
using PSETPTRFOCUS = void (*)(void *, SP<CWLSurfaceResource>, const Vector2D &);
PSETKEYFOCUS g_setKeyboardFocusOrig = nullptr;
PSETPTRFOCUS g_setPointerFocusOrig = nullptr;

// True while the overview owns the screen. m_pendingDeactivate is deliberately NOT
// included: the final frame still draws the overlay, and by then the desktop is
// expected to be interactive again. openedOrParked() rather than active(), because the
// parked frames are already the overview's -- the overlay is merely not drawn yet, and
// input must not reach the windows it is covering.
bool inputRoutedAway() {
  return g_overview && g_overview->openedOrParked();
}

// The hooks refuse to move focus to a window, but they do not drop focus from the one
// that already had it. Without this the window under the cursor keeps receiving motion
// for the whole session. refocus() re-runs Hyprland's own resolution, which now lands
// on nullptr because the hooks are in the way -- and lands on the real window again once
// the overview is gone, so no separate restore path is needed.

void hkSetKeyboardFocus(void *thisptr, SP<CWLSurfaceResource> surf) {
  if (inputRoutedAway()) {
    // nullptr is Hyprland's own "no window has focus" value, so the seat ends up in
    // the state it would be in with nothing to focus, rather than keeping a stale one.
    g_setKeyboardFocusOrig(thisptr, nullptr);
    return;
  }
  g_setKeyboardFocusOrig(thisptr, surf);
}

void hkSetPointerFocus(void *thisptr, SP<CWLSurfaceResource> surf,
                       const Vector2D &local) {
  if (inputRoutedAway()) {
    g_setPointerFocusOrig(thisptr, nullptr, local);
    return;
  }
  g_setPointerFocusOrig(thisptr, surf, local);
}

bool hkShouldRenderWindow(void *thisptr, PHLWINDOW window, PHLMONITOR monitor) {
  const bool hide = g_overview && g_overview->shouldHideWindow(window, monitor);
  if (g_overview && g_overview->opening()) {
    debug::dbg("SRW tick=" + std::to_string(g_overview->frameTick()) +
               " win=" + std::to_string(reinterpret_cast<std::uintptr_t>(window.get())) +
               " class=" + (window ? window->m_class : "?") +
               " active=" + std::to_string(g_overview->active()) +
               " hide=" + std::to_string(hide));
  }
  if (hide)
    return false;
  return g_shouldRenderOrig ? g_shouldRenderOrig(thisptr, window, monitor) : true;
}

// commitWindow's "visible" is !isHidden, NOT shouldRenderWindow: a hidden
  // window would still pin the compositor
using PDAMAGESURFACE = void (*)(void *, SP<CWLSurfaceResource>, double, double,
                                double);
PDAMAGESURFACE g_damageSurfaceOrig = nullptr;

void hkDamageSurface(void *thisptr, SP<CWLSurfaceResource> pSurface, double x,
                     double y, double scale) {
  if (g_overview && g_overview->active() && pSurface) {
    const auto WLSURF = Desktop::View::CWLSurface::fromResource(pSurface);
    if (WLSURF) {
      const auto view = WLSURF->view();
      if (view) {
        const auto w = Desktop::View::CWindow::fromView(view);
        if (w) {
          const auto mon = g_overview->monitor();
          if (g_overview->shouldHideWindow(w, mon)) {
            // live tile previews MUST keep damage (video)
            if (g_overview->snapshotMode() || !g_overview->isTileWindow(w))
              return;
          }
        }
      }
    }
  }
  if (g_damageSurfaceOrig)
    g_damageSurfaceOrig(thisptr, pSurface, x, y, scale);
}
} // namespace

Overview::Overview(HANDLE handle) : m_handle(handle) {}

Overview::~Overview() {
  m_active = false;
  m_opening = false;
  restoreLayers();
  restorePreviewVisibility();
  restoreFill();
  releaseNewWorkspaces();
  if (m_animPump) { m_animPump->cancel(); m_animPump.reset(); }
  m_scene.clear();
  m_strip.clear();
  cancelPendingClick(); // no timer callback against a half-destroyed Overview
  if (m_shouldRenderHook) {
    HyprlandAPI::removeFunctionHook(m_handle, m_shouldRenderHook);
    m_shouldRenderHook = nullptr;
  }
  if (m_damageSurfaceHook) {
    HyprlandAPI::removeFunctionHook(m_handle, m_damageSurfaceHook);
    m_damageSurfaceHook = nullptr;
  }
  if (m_setPointerFocusHook) {
    HyprlandAPI::removeFunctionHook(m_handle, m_setPointerFocusHook);
    m_setPointerFocusHook = nullptr;
  }
  if (m_setKeyboardFocusHook) {
    HyprlandAPI::removeFunctionHook(m_handle, m_setKeyboardFocusHook);
    m_setKeyboardFocusHook = nullptr;
  }
  g_shouldRenderOrig = nullptr;
  g_damageSurfaceOrig = nullptr;
  g_setPointerFocusOrig = nullptr;
  g_setKeyboardFocusOrig = nullptr;
}

bool Overview::initialize() {
  auto &events = Event::bus()->m_events;

  m_renderStageL = events.render.stage.listen(
      [this](eRenderStage stage) { renderStage(stage); });
  m_mouseButtonL = events.input.mouse.button.listen(
      [this](const IPointer::SButtonEvent &event, Event::SCallbackInfo &info) {
        const auto copied = event;
        if (onMouseButton(copied))
          info.cancelled = true;
      });
  m_mouseAxisL = events.input.mouse.axis.listen(
      [this](const IPointer::SAxisEvent &event, Event::SCallbackInfo &info) {
        if (onMouseAxis(event))
          info.cancelled = true;
      });
  m_mouseMoveL = events.input.mouse.move.listen(
      [this](const Vector2D &, Event::SCallbackInfo &) { onMouseMove(); });
  m_keyL = events.input.keyboard.key.listen(
      [this](const IKeyboard::SKeyEvent &event, Event::SCallbackInfo &info) {
        bool cancel = false;
        onKey(event, cancel);
        if (cancel)
          info.cancelled = true;
      });

  const auto matches =
      HyprlandAPI::findFunctionsByName(m_handle, "shouldRenderWindow");
  void *addr = nullptr;
  for (const auto &mt : matches) {
    // the demangled match names the REAL type, not PHLMONITOR
    if (mt.demangled.find("shouldRenderWindow(Hyprutils::Memory::"
                          "CSharedPointer<Desktop::View::CWindow>, "
                          "Hyprutils::Memory::CSharedPointer<Monitor::"
                          "CMonitor>)") != std::string::npos) {
      addr = mt.address;
      break;
    }
  }
  if (!addr) {
    HyprlandAPI::addNotification(
        m_handle, "[gloview] could not find shouldRenderWindow to hook",
        CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
    return false;
  }
  m_shouldRenderHook = HyprlandAPI::createFunctionHook(
      m_handle, addr, reinterpret_cast<void *>(&hkShouldRenderWindow));
  if (!m_shouldRenderHook || !m_shouldRenderHook->hook()) {
    HyprlandAPI::addNotification(
        m_handle,
        "[gloview] failed to hook shouldRenderWindow — is another gloview "
        "instance loaded (hyprpm)?",
        CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
    return false;
  }
  g_shouldRenderOrig =
      reinterpret_cast<PSHOULDRENDER>(m_shouldRenderHook->m_original);

  const auto matchesDamage =
      HyprlandAPI::findFunctionsByName(m_handle, "damageSurface");
  void *addrDamage = nullptr;
  for (const auto &mt : matchesDamage) {
    if (mt.demangled.find("damageSurface(Hyprutils::Memory::"
                          "CSharedPointer<CWLSurfaceResource>, double, double, "
                          "double)") != std::string::npos) {
      addrDamage = mt.address;
      break;
    }
  }
  if (addrDamage) {
    m_damageSurfaceHook = HyprlandAPI::createFunctionHook(
        m_handle, addrDamage, reinterpret_cast<void *>(&hkDamageSurface));
    if (m_damageSurfaceHook && m_damageSurfaceHook->hook())
      g_damageSurfaceOrig =
          reinterpret_cast<PDAMAGESURFACE>(m_damageSurfaceHook->m_original);
    else {
      HyprlandAPI::addNotification(
          m_handle, "[gloview] failed to hook damageSurface",
          CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
      m_damageSurfaceHook = nullptr;
    }
  } else {
    HyprlandAPI::addNotification(
        m_handle, "[gloview] could not find damageSurface to hook",
        CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
  }

    // Input routing. Both are required: half of this is worse than none, because the
  // overview would look modal for the keyboard and not for the mouse.
  struct FocusHook {
    const char *name;
    const char *sig;
    void *hookFn;
    CFunctionHook **slot;
    void **orig;
  };
  const FocusHook focusHooks[] = {
      {"setPointerFocus",
       "setPointerFocus(Hyprutils::Memory::CSharedPointer<CWLSurfaceResource>, "
       "Hyprutils::Math::Vector2D const&)",
       reinterpret_cast<void *>(&hkSetPointerFocus),
       &m_setPointerFocusHook, reinterpret_cast<void **>(&g_setPointerFocusOrig)},
      {"setKeyboardFocus", "setKeyboardFocus(Hyprutils::Memory::"
                           "CSharedPointer<CWLSurfaceResource>)",
       reinterpret_cast<void *>(&hkSetKeyboardFocus),
       &m_setKeyboardFocusHook,
       reinterpret_cast<void **>(&g_setKeyboardFocusOrig)},
  };
  for (const auto &fh : focusHooks) {
    void *addrFocus = nullptr;
    for (const auto &mt :
         HyprlandAPI::findFunctionsByName(m_handle, fh.name)) {
      if (mt.demangled.find(fh.sig) != std::string::npos) {
        addrFocus = mt.address;
        break;
      }
    }
    if (!addrFocus) {
      HyprlandAPI::addNotification(
          m_handle,
          std::string("[gloview] could not find ") + fh.name +
              " to hook — input will reach the windows under the overview",
          CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
      return false;
    }
    *fh.slot = HyprlandAPI::createFunctionHook(m_handle, addrFocus, fh.hookFn);
    if (!*fh.slot || !(*fh.slot)->hook()) {
      *fh.slot = nullptr;
      HyprlandAPI::addNotification(
          m_handle, std::string("[gloview] failed to hook ") + fh.name,
          CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
      return false;
    }
    *fh.orig = (*fh.slot)->m_original;
  }
  return true;
}

} // namespace gloview

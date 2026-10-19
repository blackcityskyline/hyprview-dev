#include "hooks.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include "../debug/log.hpp"
#include "../overview.hpp"

namespace gloview::session {

namespace {

// The installed hooks, one pointer rather than four raw originals: every hook body needs the
// function it replaces.
Hooks *g_hooks = nullptr;

using PSHOULDRENDER = bool (*)(void *, PHLWINDOW, PHLMONITOR);

// --- input routing ---------------------------------------------------------

// shouldRenderWindow hides the real windows from the RENDERER only: CSeatManager still hands
// them pointer and keyboard focus, and Event::bus() carries no focus event to block.

// Hooked where input BECOMES addressed, not at the send* calls: blocking a send leaves the
// seat believing the window still has focus, and the compositor keeps routing to it.
using PSETKEYFOCUS = void (*)(void *, SP<CWLSurfaceResource>);
using PSETPTRFOCUS = void (*)(void *, SP<CWLSurfaceResource>, const Vector2D &);

// openedOrParked() rather than active(): the parked frames are already the overview's and
// input must not reach the windows it covers. m_pendingDeactivate is NOT included.
bool inputRoutedAway() {
  return g_overview && g_overview->openedOrParked();
}

// The hooks refuse to move focus but do not drop the one already held: without this the
// window under the cursor keeps receiving motion. refocus() re-runs Hyprland's resolution.
void hkSetKeyboardFocus(void *thisptr, SP<CWLSurfaceResource> surf) {
  const auto orig = reinterpret_cast<PSETKEYFOCUS>(
      g_hooks ? g_hooks->setKeyboardFocusOrig() : nullptr);
  if (!orig)
    return;
  // nullptr is Hyprland's own "no window has focus" value: the seat ends in a clean state
  // rather than a stale one.
  orig(thisptr, inputRoutedAway() ? nullptr : surf);
}

void hkSetPointerFocus(void *thisptr, SP<CWLSurfaceResource> surf,
                       const Vector2D &local) {
  const auto orig = reinterpret_cast<PSETPTRFOCUS>(
      g_hooks ? g_hooks->setPointerFocusOrig() : nullptr);
  if (!orig)
    return;
  orig(thisptr, inputRoutedAway() ? nullptr : surf, local);
}

bool hkShouldRenderWindow(void *thisptr, PHLWINDOW window, PHLMONITOR monitor) {
  const bool hide = g_overview && g_overview->shouldHideWindow(window, monitor);
  if (g_overview && g_overview->opening()) {
    debug::dbg("SRW tick=" + std::to_string(g_overview->frameTick()) +
               " win=" +
               std::to_string(reinterpret_cast<std::uintptr_t>(window.get())) +
               " class=" + (window ? window->m_class : "?") +
               " active=" + std::to_string(g_overview->active()) +
               " hide=" + std::to_string(hide));
  }
  if (hide)
    return false;
  const auto orig = reinterpret_cast<PSHOULDRENDER>(
      g_hooks ? g_hooks->shouldRenderOrig() : nullptr);
  return orig ? orig(thisptr, window, monitor) : true;
}

// commitWindow's "visible" is !isHidden, NOT shouldRenderWindow: a hidden window would still
// pin the compositor
using PDAMAGESURFACE =
    void (*)(void *, SP<CWLSurfaceResource>, double, double, double);

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
  const auto orig = reinterpret_cast<PDAMAGESURFACE>(
      g_hooks ? g_hooks->damageSurfaceOrig() : nullptr);
  if (orig)
    orig(thisptr, pSurface, x, y, scale);
}

} // namespace

Hooks::~Hooks() {
  const auto drop = [this](CFunctionHook *&slot) {
    if (!slot)
      return;
    HyprlandAPI::removeFunctionHook(m_handle, slot);
    slot = nullptr;
  };
  drop(m_shouldRenderHook);
  drop(m_damageSurfaceHook);
  drop(m_setPointerFocusHook);
  drop(m_setKeyboardFocusHook);
  m_shouldRenderOrig     = nullptr;
  m_damageSurfaceOrig    = nullptr;
  m_setPointerFocusOrig  = nullptr;
  m_setKeyboardFocusOrig = nullptr;
  if (g_hooks == this)
    g_hooks = nullptr;
}

bool Hooks::install(HANDLE handle, void *self) {
  m_handle = handle;
  g_hooks  = this;
  auto *o  = static_cast<Overview *>(self);
  auto &events = Event::bus()->m_events;

  m_renderStageL =
      events.render.stage.listen([o](eRenderStage stage) { o->renderStage(stage); });
  m_mouseButtonL = events.input.mouse.button.listen(
      [o](const IPointer::SButtonEvent &event, Event::SCallbackInfo &info) {
        const auto copied = event;
        if (o->onMouseButton(copied))
          info.cancelled = true;
      });
  m_mouseAxisL = events.input.mouse.axis.listen(
      [o](const IPointer::SAxisEvent &event, Event::SCallbackInfo &info) {
        if (o->onMouseAxis(event))
          info.cancelled = true;
      });
  m_mouseMoveL = events.input.mouse.move.listen(
      [o](const Vector2D &, Event::SCallbackInfo &) { o->onMouseMove(); });
  m_keyL = events.input.keyboard.key.listen(
      [o](const IKeyboard::SKeyEvent &event, Event::SCallbackInfo &info) {
        bool cancel = false;
        o->onKey(event, cancel);
        if (cancel)
          info.cancelled = true;
      });

  // The demangled match names the REAL type, not PHLMONITOR.
  const auto findBySignature = [&](const char *name, const std::string &sig) {
    for (const auto &mt : HyprlandAPI::findFunctionsByName(m_handle, name))
      if (mt.demangled.find(sig) != std::string::npos)
        return static_cast<void *>(mt.address);
    return static_cast<void *>(nullptr);
  };

  const auto *addr = findBySignature(
      "shouldRenderWindow",
      "shouldRenderWindow(Hyprutils::Memory::CSharedPointer<Desktop::View::"
      "CWindow>, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>)");
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
  m_shouldRenderOrig = m_shouldRenderHook->m_original;

  const auto *addrDamage = findBySignature(
      "damageSurface",
      "damageSurface(Hyprutils::Memory::CSharedPointer<CWLSurfaceResource>, "
      "double, double, double)");
  if (addrDamage) {
    m_damageSurfaceHook = HyprlandAPI::createFunctionHook(
        m_handle, addrDamage, reinterpret_cast<void *>(&hkDamageSurface));
    if (m_damageSurfaceHook && m_damageSurfaceHook->hook())
      m_damageSurfaceOrig = m_damageSurfaceHook->m_original;
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

  // Both input hooks are required: half of this is worse than none -- modal for the keyboard
  // and not for the mouse.
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
       reinterpret_cast<void *>(&hkSetPointerFocus), &m_setPointerFocusHook,
       &m_setPointerFocusOrig},
      {"setKeyboardFocus",
       "setKeyboardFocus(Hyprutils::Memory::CSharedPointer<CWLSurfaceResource>)",
       reinterpret_cast<void *>(&hkSetKeyboardFocus), &m_setKeyboardFocusHook,
       &m_setKeyboardFocusOrig},
  };
  for (const auto &fh : focusHooks) {
    const auto *addrFocus = findBySignature(fh.name, fh.sig);
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

} // namespace gloview::session
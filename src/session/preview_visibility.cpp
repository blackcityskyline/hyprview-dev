#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"
#include "preview_visibility.hpp"
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

namespace gloview {

namespace session {

// The saved values are what make force/restore a PAIR: calling either alone leaves the
// desktop wrong for the rest of the session, and nothing else in the plugin notices.
void PreviewVisibility::force(const std::vector<model::Tile> &tiles,
                              const std::vector<model::StripItem> &strip) {
  for (const auto &wsref : State::workspaceState()->workspaces()) {
    const auto ws = wsref.lock();
    if (!ws || !ws->m_alpha)
      continue;
    bool seen = false;
    for (const auto &e : m_ws)
      if (e.ws.get() == ws.get()) { seen = true; break; }
    if (!seen) {
      ForcedWsAlpha e;
      e.ws  = ws;
      e.val = ws->m_alpha->value();
      m_ws.push_back(e);
    }
    ws->m_alpha->setValueAndWarp(1.F);
    *ws->m_alpha = 1.F;
  }

  const auto forceWindow = [this](const PHLWINDOW &w) {
    if (!w)
      return;
    constexpr auto FS_ALPHA = Desktop::View::WINDOW_ALPHA_FULLSCREEN;
    bool wseen = false;
    for (const auto &e : m_wins)
      if (e.w.get() == w.get()) { wseen = true; break; }
    if (!wseen) {
      ForcedWinVis e;
      e.w        = w;
      e.alphaVal = w->alpha(FS_ALPHA)->value();
      m_wins.push_back(e);
    }
    if (const auto &a = w->alpha(FS_ALPHA))
      a->setValueAndWarp(1.F);
    if (!w->wlSurface() || !w->wlSurface()->resource())
      return;
    w->wlSurface()->resource()->breadthfirst(
        [this](SP<CWLSurfaceResource> s, const Vector2D &, void *) {
          if (!s)
            return;
          const auto HL = Desktop::View::CWLSurface::fromResource(s);
          if (!HL)
            return;
          bool seen = false;
          for (const auto &e : m_regions)
            if (e.hl.get() == HL.get()) { seen = true; break; }
          if (!seen) {
            ForcedRegion e;
            e.hl     = HL;
            e.region = HL->m_visibleRegion;
            m_regions.push_back(e);
          }
          HL->m_visibleRegion = {};
        },
        nullptr);
  };

  for (const auto &t : tiles)
    forceWindow(t.win.lock());
  for (const auto &it : strip)
    for (const auto &sw : it.wins)
      forceWindow(sw.win.lock());
}

void PreviewVisibility::restore() {
  for (const auto &[ws, val] : m_ws)
    if (ws && ws->m_alpha)
      ws->m_alpha->setValueAndWarp(val);
  m_ws.clear();

  for (const auto &[w, aVal] : m_wins) {
    if (!w)
      continue;
    constexpr auto FS_ALPHA = Desktop::View::WINDOW_ALPHA_FULLSCREEN;
    if (const auto &a = w->alpha(FS_ALPHA))
      *a = aVal;
  }
  m_wins.clear();

  for (const auto &[hl, region] : m_regions)
    if (hl)
      hl->m_visibleRegion = region;
  m_regions.clear();
}

} // namespace session

void Overview::forcePreviewVisibility() {
  m_previewVis.force(m_scene.tiles(), m_strip.items());
}

void Overview::restorePreviewVisibility() { m_previewVis.restore(); }

} // namespace gloview
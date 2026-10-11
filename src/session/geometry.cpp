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

std::string Overview::cursorMode() const {
    return cfg::behavior.cursor_mode.get();
}

Overview::Anchor Overview::stripAnchor() const {
  std::string a = cfg::strip.anchor.get();
  if (a.empty()) // back-compat knob
    a = cfg::strip.bar_position.get();
  if (a == "bottom")
    return Anchor::Bottom;
  if (a == "left")
    return Anchor::Left;
  if (a == "right")
    return Anchor::Right;
  return Anchor::Top;
}

bool Overview::stripHorizontal() const {
  const Anchor a = stripAnchor();
  return a == Anchor::Top || a == Anchor::Bottom;
}

double Overview::stripThickness() const {
  const auto m = m_monitor.lock();
  if (!m)
    return 150.0;
  const double cross = stripHorizontal() ? m->m_size.y : m->m_size.x;
  return std::clamp(
      static_cast<double>(cfg::strip.height), 100.0,
      cross * cfg::strip.offset_max);
}

double Overview::stripOffset() const {
  const auto m = m_monitor.lock();
  if (!m)
    return 0.0;
  const double cross = stripHorizontal() ? m->m_size.y : m->m_size.x;
  return std::clamp(
      static_cast<double>(cfg::strip.offset), 0.0,
      cross * 0.4);
}

LRect Overview::stripBand() const {
  const auto m = m_monitor.lock();
  if (!m)
    return LRect{0, 0, 0, 0};
  const double T = stripThickness();
  const double off = stripOffset();
  const double W = m->m_size.x;
  const double H = m->m_size.y;
  switch (stripAnchor()) {
  case Anchor::Bottom:
    return LRect{0, H - T - off, W, T};
  case Anchor::Left:
    return LRect{off, 0, T, H};
  case Anchor::Right:
    return LRect{W - T - off, 0, T, H};
  case Anchor::Top:
  default:
    return LRect{0, off, W, T};
  }
}

Vector2D Overview::stripSlide(double e) const {
  const double d = (1.0 - e) * stripThickness() * cfg::strip.slide_frac;
  switch (stripAnchor()) {
  case Anchor::Bottom:
    return Vector2D{0.0, d};
  case Anchor::Left:
    return Vector2D{-d, 0.0};
  case Anchor::Right:
    return Vector2D{d, 0.0};
  case Anchor::Top:
  default:
    return Vector2D{0.0, -d};
  }
}

// The band yields to a preview whose edge has reached it: the penetration depth becomes an
// outward displacement, so contact reads as the window PUSHING the band instead of sliding
// under it (measured before: the preview simply overrode the band and the two read as one
// overlapping blob). The deepest preview wins -- the band is a single object. Capped at the
// band's own thickness so a fullscreen window shoves it out of reach instead of dragging it
// across the screen.
// Generic yield, for chrome that is not the strip: displaced along the SHORTEST way out by
// the overlap depth, so contact reads as the preview pushing it rather than drawing over it.
// Deepest preview wins. The distance is bounded by the element's own size on the escape axis,
// so nothing is ever dragged across the screen and no cap is needed.
Vector2D Overview::pushOut(const LRect &r) const {
  double best = 0.0;
  Vector2D dir{0.0, 0.0};
  const auto &tiles = m_scene.tiles();
  for (size_t i = 0; i < tiles.size(); ++i) {
    const LRect b = currentBox(tiles[i], static_cast<int>(i));
    if (b.w < 1.0 || b.h < 1.0)
      continue;
    const double ox = std::min(b.x + b.w, r.x + r.w) - std::max(b.x, r.x);
    const double oy = std::min(b.y + b.h, r.y + r.h) - std::max(b.y, r.y);
    if (ox <= 0.0 || oy <= 0.0)
      continue; // not touching

    // SWALLOWED: the preview covers the element whole, so there is no cheaper axis to leave
    // by and both overlaps equal the element's size. Eject toward the nearest edge instead,
    // otherwise the displacement degenerates to one element-sized step and stops there -- the
    // measured "nudges once, then stands still while the window draws over it".
    Vector2D d{0.0, 0.0};
    double depth = 0.0;
    if (ox >= r.w && oy >= r.h) {
      double near = r.x - b.x;
      d = Vector2D{-1.0, 0.0};
      if (b.x + b.w - (r.x + r.w) < near) {
        near = b.x + b.w - (r.x + r.w);
        d = Vector2D{1.0, 0.0};
      }
      if (r.y - b.y < near) {
        near = r.y - b.y;
        d = Vector2D{0.0, -1.0};
      }
      if (b.y + b.h - (r.y + r.h) < near) {
        near = b.y + b.h - (r.y + r.h);
        d = Vector2D{0.0, 1.0};
      }
      depth = near;
    } else if (ox < oy) {
      depth = ox;
      d = Vector2D{std::copysign(1.0, r.cx() - b.cx()), 0.0};
    } else {
      depth = oy;
      d = Vector2D{0.0, std::copysign(1.0, r.cy() - b.cy())};
    }
    if (depth > best) {
      best = depth;
      dir = d;
    }
  }
  if (best <= 0.0)
    return {};
  return Vector2D{dir.x * best, dir.y * best};
}

Vector2D Overview::stripOffset(double e) const {
  return stripSlide(e) + stripPush(stripBand());
}

Vector2D Overview::stripPush(const LRect &band) const {
  double deepest = 0.0;
  const auto &tiles = m_scene.tiles();
  for (size_t i = 0; i < tiles.size(); ++i) {
    const LRect b = currentBox(tiles[i], static_cast<int>(i));
    if (b.w < 1.0 || b.h < 1.0)
      continue;
    double pen = 0.0;
    switch (stripAnchor()) {
    case Anchor::Bottom: pen = (b.y + b.h) - band.y; break;
    case Anchor::Top:    pen = (band.y + band.h) - b.y; break;
    case Anchor::Left:   pen = (band.x + band.w) - b.x; break;
    case Anchor::Right:  pen = (b.x + b.w) - band.x; break;
    }
    deepest = std::max(deepest, pen);
  }
  if (deepest <= 0.0)
    return {};
  // Uncapped: the depth is bounded by the tile, and a strip shoved past the screen edge is
  // exactly "задвигает". The cap this replaces held it to one band thickness, which cleared
  // the band for a shallow overlap and left the workspace cards — riding the same slide —
  // painted over the tile edge once the window grew to cover the strip (measured on exit).
  const double d = deepest;
  switch (stripAnchor()) {
  case Anchor::Bottom: return Vector2D{0.0, d};
  case Anchor::Top:    return Vector2D{0.0, -d};
  case Anchor::Left:   return Vector2D{-d, 0.0};
  case Anchor::Right:  return Vector2D{d, 0.0};
  }
  return {};
}

bool Overview::blurEnabled() const { return blurStrength() > 0.0F; }

float Overview::blurStrength() const {
  return std::clamp(cfg::blur.strength.get(), 0.0F, 1.0F);
}

int Overview::blurPasses() const {
  return std::clamp(cfg::blur.passes.get(), 1, 16);
}

int Overview::blurSize() const {
  return std::clamp(cfg::blur.size.get(), 1, 200);
}

int Overview::blurResolution() const {
  return std::clamp(cfg::blur.resolution.get(), 1, 32);
}

// The close lands on m_workspace, which focusAndClose re-points at the focused window's
// workspace when one is committed. So "will this window be there" is exactly "is it on
// m_workspace" -- one comparison, no policy of its own; anim::tileExit holds the policy.
bool Overview::tileGoesHome(const model::Tile &t) const {
  const auto w    = t.win.lock();
  const auto land = m_workspace.lock();
  if (!w || !land)
    return true; // nothing to compare against: keep the flight we have always done
  return w->m_workspace == land;
}

// The compositor decides what to draw from three pieces of state, and for a window that is
// not on the ACTIVE workspace all three say "no": the workspace is faded out, the window's
// fullscreen alpha is 0, and the window's surfaces carry a visibleRegion left over from
// whenever they were last on screen. The live preview then reads a resource with no
// committed buffer and a geometry that was never given the layout.
//
// ScrollOverview clears all three before drawing a preview and puts them back afterwards.
// Same three, same calls -- forceWorkspaceAlphaVisible / forceWindowVisible /
// forceWindowSurfaceVisibility.
void Overview::forcePreviewVisibility() {
  for (const auto &wsref : State::workspaceState()->workspaces()) {
    const auto ws = wsref.lock();
    if (!ws || !ws->m_alpha)
      continue;
    bool seen = false;
    for (const auto &e : m_forcedAlphaWs)
      if (e.ws.get() == ws.get()) { seen = true; break; }
    if (!seen) {
      ForcedWsAlpha e;
      e.ws  = ws;
      e.val = ws->m_alpha->value();
      m_forcedAlphaWs.push_back(e);
    }
    ws->m_alpha->setValueAndWarp(1.F);
    *ws->m_alpha = 1.F;
  }

  const auto forceWindow = [this](const PHLWINDOW &w) {
    if (!w)
      return;
    constexpr auto FS_ALPHA = Desktop::View::WINDOW_ALPHA_FULLSCREEN;
    bool wseen = false;
      for (const auto &e : m_forcedWindows)
        if (e.w.get() == w.get()) { wseen = true; break; }
      if (!wseen) {
        ForcedWinVis e;
        e.w        = w;
        e.alphaVal = w->alpha(FS_ALPHA)->value();
        m_forcedWindows.push_back(e);
      }
    if (const auto& a = w->alpha(FS_ALPHA))
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
          for (const auto &e : m_forcedRegions)
            if (e.hl.get() == HL.get()) { seen = true; break; }
          if (!seen) {
            ForcedRegion e;
            e.hl     = HL;
            e.region = HL->m_visibleRegion;
            m_forcedRegions.push_back(e);
          }
          HL->m_visibleRegion = {};
        },
        nullptr);
  };

  for (const auto &t : m_scene.tiles())
    forceWindow(t.win.lock());
  for (const auto &it : m_strip)
    for (const auto &sw : it.wins)
      forceWindow(sw.win.lock());
}

void Overview::restorePreviewVisibility() {
  for (const auto &[ws, val] : m_forcedAlphaWs)
    if (ws && ws->m_alpha)
      ws->m_alpha->setValueAndWarp(val);
  m_forcedAlphaWs.clear();

  for (const auto &[w, aVal] : m_forcedWindows) {
    if (!w)
      continue;
    constexpr auto FS_ALPHA = Desktop::View::WINDOW_ALPHA_FULLSCREEN;
    if (const auto& a = w->alpha(FS_ALPHA))
      *a = aVal;
  }
  m_forcedWindows.clear();

  for (const auto &[hl, region] : m_forcedRegions)
    if (hl)
      hl->m_visibleRegion = region;
  m_forcedRegions.clear();
}

} // namespace gloview

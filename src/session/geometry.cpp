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

Overview::Anchor Overview::stripAnchor() const {
  return render::parseAnchor(cfg::strip.anchor.get(), cfg::strip.bar_position.get());
}

bool Overview::stripHorizontal() const {
  return render::isHorizontal(stripAnchor());
}

double Overview::stripThickness() const {
  const auto m = m_monitor.lock();
  if (!m)
    return 150.0;
  const double cross = stripHorizontal() ? m->m_size.y : m->m_size.x;
  return render::stripThickness(cross,
                                static_cast<double>(cfg::strip.height),
                                cfg::strip.offset_max);
}

double Overview::stripOffset() const {
  const auto m = m_monitor.lock();
  if (!m)
    return 0.0;
  const double cross = stripHorizontal() ? m->m_size.y : m->m_size.x;
  return render::stripOffset(cross,
                             static_cast<double>(cfg::strip.offset));
}

LRect Overview::stripBand() const {
  const auto m = m_monitor.lock();
  if (!m)
    return LRect{0, 0, 0, 0};
  return render::stripBand(stripAnchor(), m->m_size.x, m->m_size.y, stripThickness(),
                           stripOffset());
}

Vector2D Overview::stripSlide(double e) const {
  const auto s = render::stripSlide(stripAnchor(), e, stripThickness(),
                                    cfg::strip.slide_frac);
  return Vector2D{s.x, s.y};
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

Vector2D Overview::pushOut(const LRect &r) const {
  double best = 0.0;
  Vector2D dir{0.0, 0.0};
  const auto &tiles = m_scene.tiles();
  for (size_t i = 0; i < tiles.size(); ++i) {
    const LRect b = currentBox(tiles[i], static_cast<int>(i));
    if (b.w < 1.0 || b.h < 1.0)
      continue;
    // A tab's card has nothing in it: frost, an outline, a caption. It cannot cover the
    // chrome it would be displacing, so it must not displace it. Yielding to one made the
    // strip get shoved on every step of the run -- the cards change places on every step, and
    // the shoving stopped only once the ladder wrapped and the set of boxes settled. A
    // window's card is its own content and that one still yields.
    if (m_tabs.isTab(tiles[i].subj))
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


} // namespace gloview

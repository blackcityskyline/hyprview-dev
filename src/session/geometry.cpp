#include "../render/geometry.hpp"
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

namespace hyprview {

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
  // An occupied desktop hands over to the compositor: the curtain travels all the way out, no fade.
  const double frac = m_landingEmpty ? cfg::strip.slide_frac : 1.0;
  const auto s = render::stripSlide(stripAnchor(), e, stripThickness(), frac);
  return Vector2D{s.x, s.y};
}

Vector2D Overview::stripOffset(double e) const {
  return stripSlide(e) + stripPush(stripBand());
}

Vector2D Overview::stripPush(const LRect &band) const {
  std::vector<LRect> covers;
  const auto &tiles = m_scene.tiles();
  for (size_t i = 0; i < tiles.size(); ++i) {
    const LRect b = currentBox(tiles[i], static_cast<int>(i));
    if (b.w < 1.0 || b.h < 1.0)
      continue;
    // Shove clear of whatever would COVER it; same rule as pushOut(), answered by cardOf().
    if (!cardOf(tiles[i]).coversChrome)
      continue;
    covers.push_back(b);
  }
  const LVec2 v = render::stripPush(band, stripAnchor(), covers);
  return Vector2D{v.x, v.y};
}

// The cards that can cover chrome: the rule that uses them lives in render::caption.
std::vector<LRect> Overview::coveringCards() const {
  std::vector<LRect> covers;
  const auto &tiles = m_scene.tiles();
  for (size_t i = 0; i < tiles.size(); ++i) {
    const LRect b = currentBox(tiles[i], static_cast<int>(i));
    if (b.w < 1.0 || b.h < 1.0)
      continue;
    // Same rule as stripPush(): a tab's card covers nothing and must not displace chrome.
    if (!cardOf(tiles[i]).coversChrome)
      continue;
    covers.push_back(b);
  }
  return covers;
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


}

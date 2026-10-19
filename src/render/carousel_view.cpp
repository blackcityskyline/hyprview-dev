
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "gl_util.hpp"
#include "tile_look.hpp"
#include "window_content.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {

// Functions, not namespace-scope constants: config read before PLUGIN_INIT crashed the session (2.5).
double nearTone()   { return cfg::carousel.tone_near; }
double farTone()    { return cfg::carousel.tone_far; }
double depthTaper() { return cfg::carousel.depth_taper; }

} // namespace

void Overview::relayoutCarousel() {
  if (!carouselMode() || m_scene.tiles().empty())
    return;
  // Never mid-drag: a relayout moves every slot, under the window the user is carrying.
  if (draggedTile() >= 0 || m_drag.press() == model::Drag::Press::StripWin)
    return;
  // Never during a close: maximized windows share one natural rectangle and all land in one point.
  if (!opening() || m_life.deactivating())
    return;
  if (m_scene.selected() < 0 || m_scene.selected() >= static_cast<int>(m_scene.tiles().size()))
    return;
  if (m_scene.selected() == m_view.carouselFor())
    return;
  auto shown = captureCurrentBoxes();
  layoutTiles();
  startTileGlide(shown);
}

void Overview::renderCarousel() const {
  const auto m = m_monitor.lock();
  if (!m || m_scene.tiles().empty() || m_view.carousel().cards.size() != m_scene.tiles().size())
    return;

  // Paint order is far -> near by DEPTH: slot k is m_view.slot()[k], the array order is untouched.
  std::vector<int> paint;
  paint.reserve(m_scene.size());
  for (size_t i = 0; i < m_scene.size(); ++i)
    paint.push_back(static_cast<int>(i));
  std::stable_sort(paint.begin(), paint.end(), [this](int a, int b) {
    const auto kOf = [&](const int tile) {
      const int k = m_view.slotOf(static_cast<std::size_t>(tile));
      return static_cast<std::size_t>(k < 0 ? 0 : k);
    };
    const auto &ca = m_view.carousel().cards[kOf(a)];
    const auto &cb = m_view.carousel().cards[kOf(b)];
    if (ca.visible != cb.visible)
      return !ca.visible;
    return ca.depth > cb.depth;
  });

  for (const int i : paint) {
    const int k = m_view.slotOf(static_cast<std::size_t>(i));
    // No slot: this tile is not on the run -- never substitute slot 0, which is the FOCUSED card.
    if (k < 0)
      continue;
    const auto &card = m_view.carousel().cards[static_cast<std::size_t>(k)];
    if (!card.visible)
      continue;
    if (i == draggedTile())
      continue;

    const auto &t = m_scene.tiles()[static_cast<size_t>(i)];
    // Already drawn by renderSwapFX during a grid flight: skip it in the run too.
    if (swapfxActive(tileWindow(t), false))
      continue;

    TileLook look;
    look.box = currentBox(t, i);
    // progress.raw() runs DOWN on close; every exit fraction reads it through exitT.
    // The deck's ONE hide rule, same predicate as the grid's: a cancel takes the whole deck out.
    if (!opening() && anim::cardHides(card.depth, frame().goesHome(t))) {
      const double k =
          anim::hideShrink(exitT(), anim::param(anim::Param::Collapse));
      const double cx = look.box.cx(), cy = look.box.cy();
      look.box = LRect{cx - look.box.w * k * 0.5, cy - look.box.h * k * 0.5,
                       look.box.w * k, look.box.h * k};
    }
    // Clamped to the shrinking box: a radius past half the shorter side does not round at all.
    look.round = clampRound(cfg::look.preview_round, look.box.w, look.box.h);
    look.frame = card.depth == 0;         // only the centred card is framed
    look.contain = true;
    look.tone =
        card.depth > 0
            ? (card.depth == 1 ? nearTone() : farTone()) *
                  std::pow(depthTaper(), static_cast<double>(card.depth - 1))
            : 0.0;
    look.button = tileCloseButtons();
    // The counter is drawn with the centred card's pill and pushed with it: one owner, one push.
    look.counter = card.depth == 0;
    drawTile(static_cast<size_t>(i), look);
  }
}

} // namespace hyprview

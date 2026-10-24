
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

}

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
  startTileGlide(shown, frame().monW);
}

void Overview::renderCarousel() const {
  const auto m = m_monitor.lock();
  if (!m || m_scene.tiles().empty() || m_view.carousel().cards.size() != m_scene.tiles().size())
    return;

  // Paint order is far -> near by DEPTH: slot k is m_view.slot()[k], the array order is untouched.
  std::vector<render::CarouselCardRef> refs;
  refs.reserve(m_scene.size());
  for (size_t i = 0; i < m_scene.size(); ++i) {
    const int k = m_view.slotOf(i);
    render::CarouselCardRef ref;
    ref.tile = static_cast<int>(i);
    if (k >= 0 && static_cast<std::size_t>(k) < m_view.carousel().cards.size()) {
      ref.depth = m_view.carousel().cards[static_cast<std::size_t>(k)].depth;
      ref.visible = m_view.carousel().cards[static_cast<std::size_t>(k)].visible;
    }
    refs.push_back(ref);
  }
  const auto paint = render::carouselPaintOrder(refs);
  std::vector<std::pair<int, TileLook>> looks;
  looks.reserve(paint.size());

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
    if (swapfxActiveSubject(t.subj, false))
      continue;

    // The hide motion is the placement's: one rule for the grid and the run.
    const LRect box = currentBox(t, i);
    const float depth = t.depthFrom + (t.depthTo - t.depthFrom) * tileProgress(i);
    const auto look = runLook(box, depth,
                              render::carouselTone(depth, nearTone(), farTone(),
                                                   depthTaper()),
                              tileCloseButtons(), cfg::look.preview_round);
    looks.emplace_back(i, look);
  }

  // The captions first, the cards after: the cards cover a long title.
  for (const auto &[i, look] : looks)
    drawTileCaption(i, look);
  for (const auto &[i, look] : looks)
    drawTile(static_cast<size_t>(i), look, /*withCaption=*/false);
}

}

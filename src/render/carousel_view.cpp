
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

namespace gloview {

namespace {

// Read through functions, never cached at namespace scope: see the same note in fx.cpp
// about a global initialised from the config before PLUGIN_INIT.
double nearTone()   { return cfg::carousel.tone_near; }
double farTone()    { return cfg::carousel.tone_far; }
double depthTaper() { return cfg::carousel.depth_taper; }

} // namespace

// A selection move is a relayout + glide, detected once: renderStage vs m_view.carouselFor().
void Overview::relayoutCarousel() {
  if (!carouselMode() || m_scene.tiles().empty())
    return;
  // Never mid-drag: a relayout moves every slot, so the run would shift under the
  // window the user is carrying.
  if (draggedTile() >= 0 || m_drag.press() == model::Drag::Press::StripWin)
    return;
  // Never during a close: a relayout rewrites every natural, which for maximized
  // windows is one shared rectangle — all the cards land in one point.
  if (!opening() || m_life.deactivating())
    return;
  if (m_scene.selected() < 0 || m_scene.selected() >= static_cast<int>(m_scene.tiles().size()))
    return; // not a selection change; layoutTiles settles it next
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

  // Paint order is far -> near by DEPTH, but the cards are still the scene's tiles:
  // slot k is m_view.slot()[k], so the run can be ordered by recency without the
  // array itself being reordered.
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
    // No slot means this tile is not on the run -- and substituting slot 0 handed it the
    // FOCUSED card's geometry, which is `visible`, so a tile the run has already dropped was
    // painted full size behind the cards for the frame before it was collected: a window the
    // width of the screen, sitting under the carousel, that then settled at the end. There is
    // no card here to draw, so say so instead of borrowing one.
    if (k < 0)
      continue;
    const auto &card = m_view.carousel().cards[static_cast<std::size_t>(k)];
    if (!card.visible)
      continue;
    if (i == draggedTile())
      continue;

    const auto &t = m_scene.tiles()[static_cast<size_t>(i)];
    // A window with a live grid flight is already being drawn by renderSwapFX. The
    // run skipped this check while the grid renderer had it, so the card and its
    // flying copy were both on screen.
    //
    // horizontal hid it: the flight lerps from `from`, which is exactly where the card
    // is, so the two sat on top of each other. slidevert lifts on the first frame
    // (from.y - (h + LIFT) * q) and the card underneath became visible -- which is why
    // this only ever showed up as "vertical animations duplicate".
    if (swapfxActive(tileWindow(t), false))
      continue;

    TileLook look;
    look.box = currentBox(t, i);
    // m_place.progress.raw() runs DOWN on close (1 open -> 0 closed); every exit fraction
    // reads it through exitT. Read backwards it looks exactly like no animation.
    // The deck's ONE hide rule, same predicate as the grid's: a card that cannot land
    // hides whatever its depth, which is what makes a cancel take the whole deck out
    // instead of leaving the centred card pretending to be on its way somewhere. A card
    // that CAN land and is centred keeps currentBox's own lerp — one motion, one place.
    if (!opening() && anim::cardHides(card.depth, frame().goesHome(t))) {
      const double k =
          anim::hideShrink(exitT(), anim::param(anim::Param::Collapse));
      const double cx = look.box.cx(), cy = look.box.cy();
      look.box = LRect{cx - look.box.w * k * 0.5, cy - look.box.h * k * 0.5,
                       look.box.w * k, look.box.h * k};
    }
    // Clamped to the box it is about to round: the box shrinks toward zero on close, and a
    // radius past half the shorter side does not round at all -- the card would go square
    // and its ring would vanish instead of closing with it.
    look.round = clampRound(cfg::look.preview_round, look.box.w, look.box.h);
    look.frame = card.depth == 0;         // only the centred card is framed
    look.contain = true;                  // the window goes inside the box whole
    look.tone =
        card.depth > 0
            ? (card.depth == 1 ? nearTone() : farTone()) *
                  std::pow(depthTaper(), static_cast<double>(card.depth - 1))
            : 0.0;
    look.button = tileCloseButtons();
    // The counter is the centred card's caption sprout, so it is drawn with that card's
    // pill and pushed with it. One owner, one push: as a separate element above the run
    // the two drifted apart the moment a preview reached only one of them.
    look.counter = card.depth == 0;
    drawTile(static_cast<size_t>(i), look);
  }
}

} // namespace gloview

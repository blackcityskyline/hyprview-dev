#include <algorithm>
#include <chrono>
#include <cmath>

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "gl_util.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

// Both entry clocks done. Committing before this truncates what the tap was for.
bool Overview::entrySettled() const {
  return m_place.clocks.master.done(anim::cfg(anim::Id::Open).ms) && m_place.clocks.tiles.done(glideDur());
}

namespace {

class PainterPass final : public IPassElement {
public:
  explicit PainterPass(Overview *o) : m_owner(o) {}

  std::vector<UP<IPassElement>> draw() override {
    if (!m_owner)
      return {};
    // stale clipBox clips our geometry
    g_pHyprRenderer->m_renderData.clipBox = CBox();
    m_owner->paint();
    return {};
  }

  // false leaves blur residue
  bool needsLiveBlur() override { return m_owner && m_owner->blurEnabled(); }
  bool needsPrecomputeBlur() override { return false; }
  // simplify() + preview geometry can empty it
  bool disableSimplification() override { return true; }
  bool undiscardable() override { return true; }
  const char *passName() override { return "HyprviewPainter"; }
  ePassElementType type() override { return EK_CUSTOM; }
  std::optional<CBox> boundingBox() override {
    const auto m = m_owner ? m_owner->monitor() : nullptr;
    if (!m)
      return std::nullopt;
    return CBox{{}, m->m_size};
  }

private:
  Overview *m_owner = nullptr;
};

}

// BUILD time (RENDER_LAST_MOMENT): mutate here, draw in draw().
void Overview::renderStage(eRenderStage stage) {
  if (stage == RENDER_LAST_MOMENT) {
    m_pump.tickFrame();
    syncRecency();
  }

  // Parked open: the pass is registered anyway so draw() can run primeBackdrop (private FB, no draw).
  if (m_life.parked() && stage == RENDER_LAST_MOMENT) {
    const auto pm  = m_monitor.lock();
    const auto prm = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (pm && prm == pm)
      g_pHyprRenderer->m_renderPass.add(makeUnique<PainterPass>(this));
    return;
  }

  if (!active())
    return;
  const auto rm = g_pHyprRenderer->m_renderData.pMonitor.lock();
  const auto m = m_monitor.lock();
  if (!rm || !m || rm != m)
    return;

  if (stage != RENDER_LAST_MOMENT)
    return;

  updateAnimation();
  if (!active())
    return;

  if (debug::enabled()) {
    const double e  = eased();
    const float  k  = static_cast<float>(e);
    const float  kk = opening() ? k : std::pow(k, 0.45F);
    const auto   sol = rm->m_solitaryClient.lock();
    // blurOK answers "is the cache good", not "has a texture": the FB holds the stale blur.
    const auto   blurTex = backdropBlurTexture();
    const bool   blurOK = blurTex && blurTex->ok() && !backdropBlurStale();
    const auto ext = g_pHyprRenderer->m_renderData.damage.copy().getExtents();
    debug::dbg("F t=+" +
        std::to_string(
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_openStamp)
                .count()) +
        "ms tick=" + std::to_string(m_pump.tick()) +
        " forceFF=" + std::to_string(rm->m_forceFullFrames) +
        " open=" + std::to_string(opening()) +
        " prog=" + std::to_string(m_place.progress.raw()).substr(0, 5) +
        " kk=" + std::to_string(kk).substr(0, 6) +
        " frost=" + std::to_string(1.0 - e).substr(0, 6) +
        " blurOK=" + std::to_string(blurOK) +
        " bdDrawn=" + std::to_string(backdropDrawn()) +
        " soli=" + std::to_string((bool)sol) +
        " dso=" + std::to_string(rm->m_directScanoutIsActive) +
        " dmg=" + std::to_string(ext.w) + "x" + std::to_string(ext.h) +
        " rects=" +
        std::to_string(g_pHyprRenderer->m_renderData.damage.copy().getRects().size()) +
        " tiles=" + std::to_string(m_scene.tiles().size()) +
        " strip=" + std::to_string(m_strip.size()) +
        " ws=" +
        std::to_string(m_workspace.lock() ? m_workspace.lock()->m_id : -1) +
        " liveWs=" +
        std::to_string(m->m_activeWorkspace ? m->m_activeWorkspace->m_id : -1) +
        " drag=" + std::to_string((int)m_drag.press()) + "/" +
        std::to_string(m_drag.lifted()) + "/" + std::to_string(m_drag.idx()) +
        " sfx=" + std::to_string(m_fx.swapfx().size()) +
        " dir=" + std::to_string(m_view.wsSlideDir()) +
        " gh=" + std::to_string(m_fx.ghosts().size()) +
        " pop=" + std::to_string(!m_place.clocks.rebuild.done(m_place.recipes.entry.ms)));
  }

  updateHover();
  if (m_altTabbing && m_life.openingNow() && !m_commitPending && g_pInputManager &&
      !(g_pInputManager->getModsFromAllKBs() & altTabModBit()))
    m_commitPending = true;
  relayoutCarousel();
  syncTiles();
  // syncTiles() just rebuilt the deck: re-assert the three pieces of state every frame, idempotently.
  forcePreviewVisibility();
  updateSnapshots();

  if (opening() && m->m_activeWorkspace != m_liveWsAtOpen.lock()) {
    m_liveWsAtOpen = m->m_activeWorkspace;
    if (cfg::behavior.exit_on_switch != 0) {
      m_workspace = m->m_activeWorkspace;
      close();
    } else if (!showAllWorkspaces()) {
      // capture + replay reflow, else stale ghosts survive
      if (const auto prev = m_workspace.lock())
        m_view.setWsSlideDir( m->m_activeWorkspace->m_id > prev->m_id ? 1 : -1);
      m_workspace = m->m_activeWorkspace;
      const auto shown = captureCurrentBoxes();
      buildTiles();
      buildStrip();
      layoutTiles();
      startTileGlide(shown);
      // startTileGlide marks matched windows landed = 1: right for a reflow, wrong on a switch.
      m_scene.armEntry();
      m_place.clocks.rebuild.begin();
      debug::trace::animStart("entry", "entry", "", m_place.recipes.entry.ms, m_place.recipes.entry.curve.c_str());
      // Arm the slide AFTER the rebuild clock starts: updateAnimation clears wsSlideDir when
      // both entry clocks read done, which they do on a hop from an empty workspace.
      if (const auto prev = m_workspace.lock())
        m_view.setWsSlideDir( m->m_activeWorkspace->m_id > prev->m_id ? 1 : -1);
      ensureAnimPump();
      {
        int appearing = 0;
        for (const auto &t : m_scene.tiles())
          if (t.landed < 1.0)
            ++appearing;
        debug::dbg("WSFOLLOW ->" + std::to_string(m->m_activeWorkspace->m_id) +
                   " dir=" + std::to_string(m_view.wsSlideDir()) +
                   " style=[" + anim::moveName(m_place.recipes.enterStyle) + "] tiles=" +
                   std::to_string(m_scene.tiles().size()) + " animating=" +
                   std::to_string(appearing) + " oldBoxes=" +
                   std::to_string(shown.size()));
      }
      // no blur invalidation: source id rechecked per frame
      if (m_scene.selected() < 0 || m_scene.selected() >= static_cast<int>(m_scene.tiles().size()))
        m_scene.select(m_scene.tiles().empty() ? -1 : 0);
      damage();
    } else {
      buildStrip();
      damage();
    }
  }

  g_pHyprRenderer->m_renderPass.add(makeUnique<PainterPass>(this));

  {
    const bool busy = secondaryAnimsActive() ||
                      !m_place.clocks.tiles.done(glideDur()) || m_newCardAnim ||
                      m_drag.lifted() || !m_fx.pulses().empty() ||
                      !m_fx.swapfx().empty() ||
                      (opening() ? m_place.progress.raw() < 1.0 : m_place.progress.raw() > 0.0);
    if (busy)
      m->m_forceFullFrames = std::max(m->m_forceFullFrames, 1);
  }
}

// Z-slots, top to bottom: on-top-of-content draws after it.
void Overview::paint() {
  if (m_life.parked()) {
    primeBackdrop();
    return;
  }
  const render::GLState glEnter = readGLState();
  glStateFence("paint");
  renderBackdrop();

  if (carouselMode())
    renderCarousel();
  else
    renderTiles();
  renderGhosts();
  if (!carouselMode())
    renderPulses(false);

  renderStrip();
  renderStripWindows();
  renderStripButtons();
  renderPulses(true);
  renderSwapFX();

  const bool dragging =
      draggedTile() >= 0 ||
      (m_drag.press() == model::Drag::Press::StripWin && m_drag.lifted());
  if (dragging) {
    // Kill zone FIRST, then the preview over it: drawing it after stacked the 1px glow lines
    // over the very card being dragged.
    renderKillZone();
    renderDragTile();
    renderDragWindow();
  }

  renderAboveLayers();

  renderCursorOnTop();

  rearmanim(); // tail: damage() here hits already-committed damage
  glStateReport(glEnter, "paint");
  if (m_commitPending && m_life.openingNow() && !m_life.deactivating() &&
      entrySettled()) {
    m_commitPending = false;
    activateSelection();
  }
  finishPendingDeactivate();
}

void Overview::renderCursorOnTop() const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  m_cursor.renderOnTop(m, cfg::blur.backdrop.get(1.0));
}

}

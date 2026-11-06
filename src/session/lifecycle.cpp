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

namespace {
void dropFocusFromWindows() {
  if (g_pInputManager)
    g_pInputManager->refocus(std::nullopt);
}}

void Overview::toggle() {
  debug::dbg("TOGGLE tick=" + std::to_string(m_pump.tick()) +
             " active=" + std::to_string(active()) +
             " opening=" + std::to_string(opening()));
  // While the overview is up the toggle means "go to what I am looking at", the same thing Enter means.
  switch (anim::toggleIntent(m_life.openingNow(), m_scene.focused() >= 0)) {
    case anim::DismissIntent::Open:
      open();
      break;
    case anim::DismissIntent::Commit:
      activateSelection();
      break;
    case anim::DismissIntent::Cancel:
      close();
      break;
  }
}

void Overview::toggleOne() {
  if (m_life.openingNow()) {
    toggle(); // open: commit the pick, or close when nothing is focused
    return;
  }
  m_view.setAllOverride(0);
  open();
  if (!openedOrParked())
    m_view.setAllOverride(-1);
}

void Overview::toggleBrowser() {
  if (!m_life.openingNow()) {
    m_view.setDomain(model::Domain::Tabs); // open(true) keeps it instead of re-reading tile_mode
    m_view.setAllOverride(1);
    if (!openStepped(cfg::behavior.browser_advances != 0 ? 1 : 0, true))
      m_view.setAllOverride(-1);
    return;
  }
  toggleBrowserView();
}

void Overview::toggleCarousel() {
  if (!m_life.openingNow()) {
    m_view.setCarouselOn( true); // BEFORE open(): buildTiles reads it
    m_view.setAllOverride( 1);
    open();
    if (!openedOrParked())
      m_view.setAllOverride( -1);
    return;
  }
  if (!showAllWorkspaces()) {
    m_view.setCarouselOn( true); // BEFORE replayReflow -> layoutTiles reads it
    toggleAllWorkspaces();
    return;
  }
  m_view.setCarouselOn( !m_view.carouselOn());
  // The SAME rebuild the 'a' binding does: a bare layoutTiles() leaves the carousel era's boxes scattered.
  m_view.setWsSlideDir( 0);
  m_view.setExpoFlip( 0);
  auto oldBoxes = captureCurrentBoxes();
  replayReflow(oldBoxes);
  debug::dbg("TOGGLE-CAROUSEL on=" + std::to_string(m_view.carouselOn()));
}

void Overview::switchDomain(model::Domain d) {
  if (d == m_view.domain())
    return;
  const bool wasTabs = model::showsTabs(m_view.domain());
  const bool nowTabs = model::showsTabs(d);
  m_view.setDomain(d);
  if (!openedOrParked())
    return;
  // Browser view covers every desktop: force all-workspaces, and put it back on the way out.
  if (nowTabs && !wasTabs && !showAllWorkspaces()) {
    m_allForced = 0;
    m_view.setAllOverride(1);
  } else if (wasTabs && !nowTabs && m_allForced >= 0) {
    m_view.setAllOverride(m_allForced);
    m_allForced = -1;
  }
  // A rebuild, not a relayout: the cards are DIFFERENT SUBJECTS now (lifecycle.cpp:218 re-reads tile_mode).
  loadTabs();
  if (nowTabs)
    requestTabShots();
  m_view.setWsSlideDir( 0);
  m_view.setExpoFlip( 0);
  auto oldBoxes = captureCurrentBoxes();
  replayReflow(oldBoxes);
  debug::trace::emitState(std::string("DOMAIN|to=") + model::domainName(m_view.domain()) +
                          "|tiles=" + std::to_string(m_scene.size()));
  debug::dbg("DOMAIN-SWITCH to=" + std::string(model::domainName(m_view.domain())));
}

void Overview::toggleBrowserView() {
  switchDomain(m_view.domain() == model::Domain::Tabs ? model::Domain::Windows
                                                      : model::Domain::Tabs);
}

render::MenuLayout Overview::menuBoxes() const {
  const auto m = m_monitor.lock();
  if (!m)
    return {};
  return render::menuLayout(m_menuAt.x, m_menuAt.y, m->m_size.x, m->m_size.y);
}

double Overview::menuProgress() const {
  const double r = m_place.clocks.menu.raw(menuDur());
  return m_menuOpen ? r : 1.0 - r;
}

void Overview::toggleMenu(double x, double y) {
  const double shown = std::clamp(menuProgress(), 0.0, 1.0);
  if (m_menuOpen) {
    m_menuOpen = false;
    m_place.clocks.menu.seek(1.0 - shown, menuDur());
  } else {
    m_menuOpen = true;
    m_menuAt = LRect{x, y, 0.0, 0.0};
    m_place.clocks.menu.seek(shown, menuDur());
    const auto c = anim::cfg(anim::Id::Card);
    debug::trace::animStart("menu", "card", c.curve.c_str(), c.ms, c.curve.c_str());
  }
  damage();
}

void Overview::toggleAllWorkspaces() {
  if (!m_life.openingNow()) {
    m_view.setAllOverride(
        1); // BEFORE open(), so buildTiles sees it
    if (!openStepped(cfg::behavior.all_advances != 0 ? 1 : 0))
      m_view.setAllOverride( -1);
    return;
  }
  const bool wasAll = showAllWorkspaces();
  m_view.setAllOverride( wasAll ? 0 : 1);
  // BEFORE the rebuild: glideLeaf()/entryLeaf()/ghostLeaf() read it (clocks.cpp:24-38).
  m_view.setExpoFlip( wasAll ? -1 : 1);
  auto oldBoxes = captureCurrentBoxes();
  replayReflow(oldBoxes);
  // AFTER the rebuild: capture is empty on an empty workspace, so arming entry before it is a no-op.
  if (oldBoxes.empty()) {
    m_scene.armEntry();
    // rebuild must start BEFORE the direction, or updateAnimation clears it in the same tick (clocks.cpp:145).
    m_place.clocks.rebuild.begin();
    debug::trace::animStart("entry", "entry", "slide", m_place.recipes.entry.ms, m_place.recipes.entry.curve.c_str());
    for (std::size_t i = 0; i < m_scene.tiles().size(); ++i)
      m_scene.setNatural(i, m_scene.tiles()[i].target);
  }
  // AFTER the rebuild clock starts (painter.cpp arms its own after); an empty workspace has no clock to delay it.
  m_view.setWsSlideDir( 1);
}

void Overview::altTabInvoke(bool reverse) {
  if (!m_life.openingNow()) {
    m_view.setAllOverride( 1);
    if (!openStepped(cfg::behavior.switcher_advances != 0 ? (reverse ? -1 : 1) : 0)) {
      m_view.setAllOverride( -1);
      return;
    }
    m_altTabbing = true; // armed AFTER open(): a plain open resets it
    if (carouselMode() && !m_scene.empty())
      layoutTiles();
    damage();
    return;
  }
  stepAltTab(reverse ? -1 : 1);
}

// The entry aimed `step` cards along the run: ONE motion of ONE duration, 0 = the focused card.
bool Overview::openStepped(int step, bool keepDomain) {
  m_pendingStep = step;
  open(keepDomain);
  // open() sets the carousel after its own layout, so a fresh session stands the run up next.
  if (m_pendingStep != 0 && carouselMode() && !m_scene.empty())
    layoutTiles();
  m_pendingStep = 0;
  return openedOrParked();
}

void Overview::toggleDesktop() {
  if (m_life.openingNow())
    setDesktopMode(!m_view.desktop());
}

void Overview::setDesktopMode(bool on) {
  m_view.setDesktop( on);

  auto oldBoxes = captureCurrentBoxes();
  m_scene.clearHover();
  replayReflow(oldBoxes);
}

void Overview::open(bool keepDomain) {
  const auto openEntryStamp = std::chrono::steady_clock::now();
  if (m_life.openingNow())
    return;
  m_gridScroll.reset();
  // Before anything else: forcing the compositor to render inactive workspaces crashes it (hyprlandCrashReport389377, 603004).
  forcePreviewVisibility();

  const auto m = State::monitorState()
                    ->query()
                    .vec(g_pInputManager->getMouseCoordsInternal())
                    .run();
  if (!m || !m->m_activeWorkspace)
    return;

  m_monitor = m;
  m_workspace = m->m_activeWorkspace;
  m_liveWsAtOpen =
      m->m_activeWorkspace;
  m_scene.clearHover();

  m->m_blurFBDirty = true;

  m->m_forceFullFrames = 8;

  m_drag = {};
  m_pendingWindow.reset();
  m_landingGroup = model::GroupRef{};
  cancelPendingClick();
  m_altTabbing = false;
  m_menuOpen = false;
  m_view.setDesktop( false);
  m_newCardAnim = false;
  m_newCardId = 0;
  releaseNewWorkspaces();

  if (!keepDomain)
    m_view.setDomain(model::parseDomain(cfg::behavior.tile_mode.get()));
  if (model::showsTabs(m_view.domain()) && !showAllWorkspaces()) {
    m_allForced = 0;
    m_view.setAllOverride(1);
  }
  loadTabs();
  if (model::showsTabs(m_view.domain()))
    requestTabShots();
  buildTiles();
  buildStrip();
  if (m_scene.tiles().empty() && m_strip.size() <= 1)
    return;

  // Seed selection BEFORE layoutTiles: m_lastFocus remembers the pick while the compositor's focus lags.
  m_scene.select(m_scene.tiles().empty() ? -1 : 0);
  const auto seed = m_lastFocus.lock() ? m_lastFocus.lock() : Desktop::focusState()->window();
  if (seed) {
    for (size_t i = 0; i < m_scene.tiles().size(); ++i)
      if (tileWindow(m_scene.tiles()[i]) == seed) {
        m_scene.select(static_cast<int>(i));
        break;
      }
  }

  layoutTiles();

  m_view.setCarouselOn( cfg::carousel.on_open != 0);
  m_life.requestOpen();
  debug::trace::life("park", m_life.describe());
  m_view.setJumpMode( false);
  m_openStamp = std::chrono::steady_clock::now();
  debug::dbg("OPEN tick=" + std::to_string(m_pump.tick()) +
             " entryToActiveUs=" +
             std::to_string(std::chrono::duration<double, std::micro>(
                                 m_openStamp - openEntryStamp)
                                 .count()));
  m_place.progress.closed();
  // Parked already counts as owned (see inputRoutedAway): the real windows lose focus on the first parked frame.
  dropFocusFromWindows();
  m_landingEmpty = !workspaceHasWindows(m_liveWsAtOpen.lock());
  debug::dbg("open: ws=" + std::to_string(m_liveWsAtOpen.lock() ? m_liveWsAtOpen.lock()->m_id : -1) +
             " empty=" + std::to_string(m_landingEmpty ? 1 : 0));
  m_place.clocks.master.begin();
  debug::trace::animStart("master", "master", "", m_place.recipes.glide.ms, m_place.recipes.glide.curve.c_str());
  m_place.clocks.tiles.begin();
  debug::trace::animStart("open", "glide", m_place.recipes.enterStyle.travel == anim::Travel::Pop ? "pop" : "slide", m_place.recipes.glide.ms, m_place.recipes.glide.curve.c_str());
  m_lastAnimTick = {};
  // hideLayers() is NOT called here: it runs at promotion, else the desktop is bare on frames where the overview draws nothing.
  m_cursor.onOpen(m);
  debug::dbg("=== OPEN ===");
  damage();
}

void Overview::close() {
  if (m_life.abortPark()) {
    debug::trace::life("abort", m_life.describe());
    return;
  }
  if (!active())
    return;
  // Re-read: the landing workspace can change while the overview was up (all-workspaces, digit jump).
  m_landingEmpty = !workspaceHasWindows(m_workspace.lock());
  // Keep the overlay up until Hyprland's own blur rebuilds, or releasing early uncovers a sharp desktop.
  if (const auto m = m_monitor.lock())
    m->m_forceFullFrames = std::max(m->m_forceFullFrames, 8);
  debug::dbg("close: ws=" + std::to_string(m_workspace.lock() ? m_workspace.lock()->m_id : -1) +
             " empty=" + std::to_string(m_landingEmpty ? 1 : 0));
  m_life.endEntry();
  m_life.consumeJumpClosing();
  m_menuOpen = false;
  m_place.clocks.menu.pinEnd(menuDur());

  cancelPendingClick();

  if (const auto m = m_monitor.lock()) {
    // Two passes on purpose: the pin test needs the focused tile's natural, which the loop rewrites as it goes.
    LRect selNat{};
    if (m_scene.focused() >= 0) {
      if (const auto w = ownerWindow(m_scene.tiles()[static_cast<size_t>(m_scene.focused())])) {
        const auto p = w->positionAnimation()->goal();
        const auto s = w->sizeAnimation()->goal();
        selNat = LRect{p.x - m->m_position.x, p.y - m->m_position.y,
                       std::max(1.0, s.x), std::max(1.0, s.y)};
      }
    }
    for (size_t i = 0; i < m_scene.size(); ++i) {
      auto &t = m_scene.tileAt(i);
      t.target = currentBox(t, static_cast<int>(i));
      // The natural box is where the tile RETURNS TO; otherwise the card dissolves instead (entryFade).
      if (const auto w = ownerWindow(t)) {
        const auto wws = w->m_workspace;
        if ((wws && wws != m_workspace.lock()) ||
            anim::tileExit(frame().goesHome(t)) == anim::TileExit::InPlace) {
          t.natural = t.target;
          continue;
        }
        const auto p = w->positionAnimation()->goal();
        const auto s = w->sizeAnimation()->goal();
        t.natural = LRect{p.x - m->m_position.x, p.y - m->m_position.y,
                          std::max(1.0, s.x), std::max(1.0, s.y)};
        if (carouselMode() && static_cast<int>(i) != m_scene.focused() &&
            selNat.w >= 1.0 &&
            std::abs(t.natural.x - selNat.x) < 1.0 &&
            std::abs(t.natural.y - selNat.y) < 1.0 &&
            std::abs(t.natural.w - selNat.w) < 1.0 &&
            std::abs(t.natural.h - selNat.h) < 1.0)
          t.natural = t.target;
      }
    }
  }

  restoreLayers();
  m_cursor.onClose();
  // resume from CURRENT progress (1 - raw())
  m_place.clocks.master.seek(1.0 - m_place.progress.raw(), animDuration());
  m_lastAnimTick = {}; // else a phantom gap rewinds the clocks
  debug::dbg("=== CLOSE from progress " + std::to_string(m_place.progress.raw()).substr(0, 5) + " ===");
  damage();
}

// sync teardown for unload: close() only STARTS it; unload first -> SEGV
void Overview::hardClose() {
  restorePreviewVisibility();
  cancelPendingClick();
  m_pump.stop(); // the pump's timer must not fire against a torn-down Overview

  restoreLayers();
  restoreFill();
  releaseNewWorkspaces();

  m_life.hardClose();
  debug::trace::life("close", m_life.describe());
  m_view.setDesktop( false);
  m_menuOpen = false;
  m_place.clocks.menu.reset();
  m_newCardAnim = false;
  m_newCardId = 0;
  m_place.progress.closed();
  m_altTabbing = false;
  m_scene.clear();
  m_strip.clear();
  m_pixel.clearSnapshots();
  m_pixel.clearArt();
  m_scene.clearHover();
  m_scene.select(-1);
  m_focusOutside.reset();

  damage();
}

bool Overview::showAllWorkspaces() const {
  // The precedence rule lives in view::State; only the config read is this side of it.
  return m_view.allWorkspaces(cfg::behavior.show_all_workspaces != 0);
}

bool Overview::belongs(const PHLWINDOW &w) const {
  return tileBelongs(w, m_monitor.lock(), m_workspace.lock());
}

// buildTiles() and syncTiles() MUST share this
bool Overview::tileBelongs(const PHLWINDOW &w, const PHLMONITOR &m,
                           const PHLWORKSPACE &ws) const {
  if (!w || !w->m_isMapped || w->isHidden())
    return false;
  const auto wws = w->m_workspace;
  if (!wws)
    return false;
  if (showAllWorkspaces()) {
    if (wws->m_isSpecialWorkspace &&
        cfg::behavior.show_special == 0)
      return false;
    return wws->m_monitor.lock() == m;
  }
  return wws == ws;
}

bool Overview::workspaceHasWindows(const PHLWORKSPACE &ws) {
  if (!ws)
    return false;
  for (const auto &w : Desktop::windowState()->windows())
    if (w && w->m_isMapped && !w->isHidden() && w->m_workspace == ws)
      return true;
  return false;
}

bool Overview::shouldHideWindow(const PHLWINDOW &w,
                                const PHLMONITOR &mon) const {
  const auto m = m_monitor.lock();
  if (!active() || !w || mon != m)
    return false;
  // fullscreen hidden: a transition force-render leaks it
  if (Fullscreen::controller()->isFullscreen(w))
    return true;
  for (const auto &t : m_scene.tiles())
    if (tileWindow(t) == w)
      return true;
  if (const auto aw = m->m_activeWorkspace; aw && w->m_workspace == aw)
    return true;
  return false;
}

bool Overview::isTileWindow(const PHLWINDOW &w) const {
    if (!w)
        return false;
    for (const auto &t : m_scene.tiles())
        if (tileWindow(t) == w)
            return true;
    return false;
}

// A hidden window is drawn at its preview's box, not at its own place.
void Overview::damageWindowPreview(const PHLWINDOW &w) const {
  const auto m = m_monitor.lock();
  if (!m || !g_pHyprRenderer || !w)
    return;
  auto hit = [m](LRect b) {
    b.grow(48.0);
    g_pHyprRenderer->damageBox(CBox{b.x + m->m_position.x, b.y + m->m_position.y, b.w, b.h});
  };
  int boxes = 0;
  for (std::size_t i = 0; i < m_scene.tiles().size(); ++i) {
    if (tileWindow(m_scene.tiles()[i]) != w)
      continue;
    hit(currentBox(m_scene.tiles()[i], static_cast<int>(i)));
    ++boxes;
  }
  for (std::size_t i = 0; i < m_strip.size(); ++i) {
    bool shows = false;
    for (const auto &sw : m_strip[i].wins)
      if (m_windows.windowOf(sw.subj) == w) {
        shows = true;
        break;
      }
    if (!shows)
      continue;
    hit(stripCardAt(i));
    ++boxes;
  }
  debug::trace::emitState("DMGPREV|win=" + std::to_string(reinterpret_cast<std::uintptr_t>(w.get())) +
                          "|boxes=" + std::to_string(boxes));
}

// The pick is final at once: recency must not wait for the focus our own hooks hold.
void Overview::commitFocus(const PHLWINDOW &w) {
  m_pendingWindow = w;
  if (!w)
    return;
  m_lastFocus = w;
  m_recency.promote(model::Recency::key(subjectOf(w)));
}

void Overview::syncRecency() {
  if (active())
    return;
  const auto now = Desktop::focusState()->window();
  if (now == m_lastFocus)
    return;
  m_lastFocus = now;
  if (now)
    m_recency.promote(model::Recency::key(subjectOf(now)));
}

int Overview::tileOf(const PHLWINDOW &w) const {
  for (size_t i = 0; i < m_scene.size(); ++i)
    if (tileWindow(m_scene.tiles()[i]) == w)
      return static_cast<int>(i);
  return -1;
}

// Recency is a cache of the live window set and must never outlive it: Hyprland reuses window addresses.
void Overview::pruneRecency() {
  std::vector<const void *> live;
  live.reserve(m_scene.size());
  for (const auto &t : m_scene.tiles())
    live.push_back(model::Recency::key(t.subj));
  m_recency.forgetDead(live);
}

void Overview::deactivate() {
  restorePreviewVisibility();
  // Let go of the windows: holding one alive past its own run keeps it out of the compositor's teardown.
  m_windows.release();
  if (const auto mm = m_monitor.lock()) {
    mm->m_blurFBDirty = true;
    // The window is still flying into its tile, so Hyprland's blur is not rendering yet; without it the FB pops.
    mm->m_forceFullFrames = 8;
  }
  restoreLayers();
  restoreFill();

  if (const auto m = m_monitor.lock()) {
    if (const auto ws = m_workspace.lock(); ws && ws != m->m_activeWorkspace) {
      const auto oldWs = m->m_activeWorkspace; // capture BEFORE the switch
      m->changeWorkspace(ws, false, true, false);
      // changeWorkspace always starts the native slide; warp to goal
      if (oldWs && oldWs != ws) {
        oldWs->m_alpha->setValueAndWarp(oldWs->m_alpha->goal());
        oldWs->m_renderOffset->setValueAndWarp(oldWs->m_renderOffset->goal());
      }
      ws->m_alpha->setValueAndWarp(ws->m_alpha->goal());
      ws->m_renderOffset->setValueAndWarp(ws->m_renderOffset->goal());
    }
  }

  if (const auto pw = m_pendingWindow.lock(); pw && pw->m_isMapped && !pw->isHidden())
    Desktop::focusState()->fullWindowFocus(pw, Desktop::FOCUS_REASON_KEYBIND);
  m_pendingWindow.reset();

  debug::dbg("deactivate (natural close end)");
  m_pump.stop();
  releaseNewWorkspaces();

  m_life.hardClose();
  // The screen is released, so the hooks pass focus through again; this is what gives the desktop its focus back.
  dropFocusFromWindows();
  m_view.setDesktop( false);
  m_view.setAllOverride( -1);
  m_allForced = -1;
  m_altTabbing = false;
  m_newCardAnim = false;
  m_newCardId = 0;
  m_place.progress.closed();
  m_scene.clear();
  m_strip.clear();
  m_pixel.clearSnapshots();
  m_pixel.clearArt();
  m_scene.clearHover();
  m_scene.select(-1);
  damage();
}

void Overview::releaseNewWorkspaces() {
  for (const auto &wref : m_newWs)
    if (const auto ws = wref.lock())
      ws->setPersistent(false);
  m_newWs.clear();
}


// EXECUTION tail: m_scene.tiles() must survive the frame
void Overview::finishPendingDeactivate() {
  if (!m_life.consumeDeactivate())
    return;
  debug::trace::life("deactivate", m_life.describe());
  debug::dbg("handoff: final overlay frame painted, deactivating");
  deactivate();
}


bool Overview::snapshotMode() const {
  return cfg::behavior.preview_mode.get() == "snapshot";
}

void Overview::updateSnapshots() {
  if (!snapshotMode())
    return;
  const auto grab = [this](const PHLWINDOW &w) {
    if (!w || !w->wlSurface() || !w->wlSurface()->resource())
      return;
    if (m_pixel.snapshot(w))
      return;
    const auto tex = w->wlSurface()->resource()->m_current.texture;
    if (!tex || !tex->ok())
      return;
    // live texture is re-uploaded per commit: copy to a private FB
    const int W = tex->m_size.x;
    const int H = tex->m_size.y;
    if (W <= 0 || H <= 0)
      return;

    auto fb = g_pHyprRenderer->createFB("hyprview snapshot");
    if (!fb)
      return;
    fb->alloc(W, H);

    auto       guard = g_pHyprRenderer->bindTempFB(fb);
    const auto oldProjType     = g_pHyprRenderer->m_renderData.projectionType;
    const auto oldFbSize       = g_pHyprRenderer->m_renderData.fbSize;
    const auto oldTransformDmg = g_pHyprRenderer->m_renderData.transformDamage;
    const auto oldModif        = g_pHyprRenderer->m_renderData.renderModif;

    g_pHyprRenderer->m_renderData.fbSize = Vector2D{W, H};
    g_pHyprRenderer->setProjectionType(Render::RPT_EXPORT);
    g_pHyprRenderer->m_renderData.transformDamage = false;
    g_pHyprRenderer->m_renderData.renderModif     = Render::SRenderModifData{};
    g_pHyprOpenGL->setViewport(0, 0, W, H);

    CRegion fullDamage = {0, 0, (double)W, (double)H};
    g_pHyprRenderer->draw(CClearPassElement::SClearData{{0.F, 0.F, 0.F, 0.F}});
    g_pHyprRenderer->draw(CTexPassElement::SRenderData{.tex = tex, .box = CBox{0, 0, (double)W, (double)H}, .damage = fullDamage},
                          fullDamage);

    g_pHyprRenderer->m_renderData.fbSize          = oldFbSize;
    g_pHyprRenderer->m_renderData.transformDamage = oldTransformDmg;
    g_pHyprRenderer->m_renderData.renderModif     = oldModif;
    g_pHyprRenderer->setProjectionType(oldProjType);
    const auto PMON = g_pHyprRenderer->m_renderData.pMonitor;
    g_pHyprOpenGL->setViewport(0, 0, PMON ? (int)PMON->m_pixelSize.x : W, PMON ? (int)PMON->m_pixelSize.y : H);
    guard.reset();

    m_pixel.putSnapshot(w, fb->getTexture());
  };
  for (const auto &t : m_scene.tiles())
    if (const auto w = tileWindow(t))
      grab(w);
  for (const auto &it : m_strip)
    for (const auto &sw : it.wins)
      if (const auto w = m_windows.windowOf(sw.subj))
        grab(w);
}

}

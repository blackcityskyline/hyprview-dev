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

namespace {
void dropFocusFromWindows() {
  if (g_pInputManager)
    g_pInputManager->refocus(std::nullopt);
}} // namespace

void Overview::toggle() {
  debug::dbg("TOGGLE tick=" + std::to_string(m_frameTick) +
             " active=" + std::to_string(active()) +
             " opening=" + std::to_string(opening()));
  // While the overview is up the toggle means "go to what I am looking at", the same thing
  // Enter means. It used to close unconditionally, so the one bind the user reaches for
  // first either dropped them on the workspace they started from or -- once the deck held
  // windows from elsewhere -- animated a focus it then cancelled.
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

// Asymmetric on purpose, NOT a copy of the "a" binding: one -> all with the run
// on; all -> all, run only flips. No path back — that is "a"'s job.
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
  // The SAME rebuild the 'a' binding does: a bare layoutTiles() only rewrites
  // targets, so the carousel era's boxes survive and the grid is scattered.
  m_view.setWsSlideDir( 0); // the view is not changing, so no slide
  m_view.setExpoFlip( 0);
  auto oldBoxes = captureCurrentBoxes();
  replayReflow(oldBoxes);
  debug::dbg("TOGGLE-CAROUSEL on=" + std::to_string(m_view.carouselOn()));
}

void Overview::toggleAllWorkspaces() {
  if (!m_life.openingNow()) {
    m_view.setAllOverride(
        1); // BEFORE open(), so buildTiles sees it
    open();
    if (!openedOrParked())
      m_view.setAllOverride( -1);
    return;
  }
  const bool wasAll = showAllWorkspaces();
  m_view.setAllOverride( wasAll ? 0 : 1);
  // expoFlip stays BEFORE the rebuild: glideLeaf()/entryLeaf()/ghostLeaf() read it to pick
  // the animation (clocks.cpp:24-38), so arming it afterwards would lay out against the
  // previous direction.
  m_view.setExpoFlip( wasAll ? -1 : 1);
  auto oldBoxes = captureCurrentBoxes();
  replayReflow(oldBoxes);
  // An empty workspace means no tile has ever been on screen, so captureCurrentBoxes()
  // returns empty and startTileGlide takes its `else` branch: settleAppear(), appear=1.
  // tileAppearNow() then answers 1.0 for every card, and the gate at clocks.cpp:145
  // (`if (ap >= 1.0) return r`) returns BEFORE the slide branch. Armed in this order the
  // fix is a no-op -- replayReflow writes appear=1 straight back over it -- so it has to
  // land AFTER the rebuild, like the direction below.
  //
  // What is on screen otherwise is just the natural->target glide, which is why it reads
  // as a fullscreen window shrinking into its slot rather than as an entry. The flip's
  // content is new either way; see the same argument in painter.cpp.
  if (oldBoxes.empty()) {
    m_scene.armAppear();
    // And the clock, which is the half that was missing. replayReflow() begins only the
    // tile glide (m_clocks.tiles) -- nothing begins the rebuild clock, so it reads done,
    // m_clocks.rebuild.raw() answers 1.0, tileAppearNow() answers 1.0 for every card and
    // the gate at clocks.cpp:145 returns before the slide branch. What was on screen was
    // just the natural->target glide, which for a maximized window reads as a fullscreen
    // card shrinking into its slot.
    //
    // painter.cpp arms the same pair on the workspace-follow path; this flip was doing
    // neither. Order matters: the clock starts BEFORE the direction, or updateAnimation
    // clears it in the same tick.
    m_clocks.rebuild.begin();
    debug::trace::animStart("entry", "entry", "slide", m_entry.ms, m_entry.curve.c_str());
    // A slide is an ENTRY, not a relocation: the card is already the right shape and
    // arrives from the edge. Left at natural, every card starts at the window's REAL
    // geometry and shrinks into its slot -- 1354x756 down to 909x508, measured -- and that
    // shrink is the handoff an OPEN wants, where the preview takes over from a window
    // that was on screen. This flip has no such window: the overlay was already up
    // showing an empty workspace, so there is nothing to hand off from and the motion is
    // pure leftover. With natural == target the glide has zero distance and the entry
    // offset is the only thing moving.
    for (std::size_t i = 0; i < m_scene.tiles().size(); ++i)
      m_scene.setNatural(i, m_scene.tiles()[i].target);
  }
  // Direction is armed AFTER the rebuild clock starts, for the reason painter.cpp arms it
  // after its own: updateAnimation clears it the moment the rebuild and entry clocks read
  // done, and a flip made from an EMPTY workspace has nothing to animate -- capture
  // CurrentBoxes() comes back empty, no clock runs, and the wipe lands in the very frame
  // the direction was set. The tiles then fell through the gate in clocks.cpp into the
  // scale-in, which reads as arriving from the centre instead of from the edge.
  //
  // painter.cpp carries a comment claiming "same slide, same style key, whichever bind
  // got here". That held for the slide and not for the order: this is the bind that still
  // had it backwards.
  m_view.setWsSlideDir( 1); // all<->one slides horizontally (no ws order)
}

void Overview::altTabInvoke(bool reverse) {
  if (!m_life.openingNow()) {
    // Armed BEFORE open() and consumed by the layout it triggers, so the entry is
    // aimed from its first frame: ONE motion of ONE duration.
    m_pendingStep = cfg::behavior.alttab_advances != 0 ? (reverse ? -1 : 1) : 0;
    m_view.setAllOverride( 1);
    open();
    m_pendingStep = 0;
    if (!openedOrParked()) {
      m_view.setAllOverride( -1);
      return;
    }
    m_altTabbing = true; // armed AFTER open(): a plain open resets it
    // open() already seeded the selection to the window that had the compositor
    // focus. Selecting index 0 here threw that away and landed on whatever window
    // happened to be first in the scene, which is why the same window came up on
    // every invocation.
    if (carouselMode() && !m_scene.empty())
      layoutTiles();
    damage();
    return;
  }
  stepAltTab(reverse ? -1 : 1);
}

void Overview::toggleDesktop() {
  if (m_life.openingNow())
    setDesktopMode(!m_view.desktop());
}

// canvas never moves or resizes a real window
void Overview::setDesktopMode(bool on) {
  m_view.setDesktop( on);

  auto oldBoxes = captureCurrentBoxes();
  m_scene.clearHover();
  // Selection survives: dropping it stopped the carousel relayout coming back.
  replayReflow(oldBoxes);
}

void Overview::open() {
  const auto openEntryStamp = std::chrono::steady_clock::now();
  if (m_life.openingNow())
    return;
  // Before anything else: make every workspace, and every window we may draw, eligible to
  // be rendered. Taken from ScrollOverview (yayuuu/hyprland-scroll-overview), which solves
  // exactly this and does it here:
  //
  //   * an inactive workspace's windows carry no committed buffer and a default
  //     geometry (measured: three windows of one scrolling workspace all reporting
  //     pos=6,6 size=1354, buf=0), so the live preview reads a texture that is not theirs
  //     -- a blank card, a flicker on every border crossing, a neighbour's pixels on the
  //     hovered one;
  //   * a window's HLSurface keeps a visibleRegion computed for the workspace it was last
  //     drawn on, and the surface pass clips against it, so a card draws nothing.
  //
  // The alternative was asking the compositor to render inactive workspaces: that crashes
  // it inside endRender -> useFP16 -> println (hyprlandCrashReport389377 and 603004). This
  // touches no compositor hook and moves no focus.
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

  // force several full-damage frames: one damage() = one image
  m->m_forceFullFrames = 8;

  m_drag = {};
  m_pendingFocus.reset();
  cancelPendingClick();
  m_altTabbing = false;
  m_view.setDesktop( false);
  m_newCardAnim = false;
  m_newCardId = 0;
  releaseNewWorkspaces();

  buildTiles();
  buildStrip();
  if (m_scene.tiles().empty() && m_strip.size() <= 1)
    return;

  // seed selection BEFORE layoutTiles
  m_scene.select(m_scene.tiles().empty() ? -1 : 0);
  if (const auto fw = Desktop::focusState()->window()) {
    for (size_t i = 0; i < m_scene.tiles().size(); ++i)
      if (m_scene.tiles()[i].win.lock() == fw) {
        m_scene.select(static_cast<int>(i));
        break;
      }
  }

  layoutTiles();

  m_view.setCarouselOn( cfg::carousel.on_open != 0);
  // Parked, NOT active: the pass computes the backdrop privately (primeBackdrop) and flips
  // m_active the moment a non-black composite exists. Going visible now is what put a
  // backdrop-less frame on screen.
  m_life.requestOpen();
  debug::trace::life("park", m_life.describe());
  m_view.setJumpMode( false);
  m_openStamp = std::chrono::steady_clock::now();
  debug::dbg("OPEN tick=" + std::to_string(m_frameTick) +
             " entryToActiveUs=" +
             std::to_string(std::chrono::duration<double, std::micro>(
                                 m_openStamp - openEntryStamp)
                                 .count()));
  m_progress = 0.0;
  // Parked already counts as owned (see inputRoutedAway), so the real windows lose
  // focus on the first parked frame, not when the overlay actually appears.
  dropFocusFromWindows();
  // Immersive fade only when the desktop underneath has no windows: with windows, their
  // Hyprland blur is what covers the gap the ramp would open.
  m_landingEmpty = !workspaceHasWindows(m_liveWsAtOpen.lock());
  debug::dbg("open: ws=" + std::to_string(m_liveWsAtOpen.lock() ? m_liveWsAtOpen.lock()->m_id : -1) + " empty=" + std::to_string(m_landingEmpty ? 1 : 0));
  m_clocks.master.begin();
  debug::trace::animStart("master", "master", "", m_glide.ms, m_glide.curve.c_str());
  m_clocks.tiles.begin();
  debug::trace::animStart("open", "glide", m_enterStyle == anim::WsStyle::Pop ? "pop" : "slide", m_glide.ms, m_glide.curve.c_str());
  m_lastAnimTick = {};
  // hideLayers() is deliberately NOT called here: it runs at promotion instead. Fading the
  // layers now would leave the desktop bare on frames where the overview draws nothing.
  m_cursor.onOpen(m, cursorMode());
  // The backdrop composite is kept and never expires here. renderBackdropSource compares the
  // source texture against the one the composite was made from and redraws when it really
  // changed; it must not be reset by a timer, because right after an idle the wallpaper
  // texture is mid-recreation and any from-scratch composite of it is black.
  debug::dbg("=== OPEN ===");
  damage();
}

void Overview::close() {
  if (m_life.abortPark()) { // never became visible, so there is nothing to animate away
    debug::trace::life("abort", m_life.describe());
    return;
  }
  if (!active())
    return;
  // The landing workspace can have changed while the overview was up (all-workspaces,
  // digit jump), and it is THIS one we land back on — re-read it, do not reuse open()'s.
  m_landingEmpty = !workspaceHasWindows(m_workspace.lock());
  // Ask Hyprland for the frames in which it rebuilds its own blur, and keep the overlay
  // up until they are spent: blur under a still-moving window is not rendered yet, so
  // releasing earlier just uncovers a sharp desktop.
  if (const auto m = m_monitor.lock())
    m->m_forceFullFrames = std::max(m->m_forceFullFrames, 8);
  debug::dbg("close: ws=" + std::to_string(m_workspace.lock() ? m_workspace.lock()->m_id : -1) + " empty=" + std::to_string(m_landingEmpty ? 1 : 0));
  m_life.endEntry();
  m_life.consumeJumpClosing();

  cancelPendingClick();

  // close: target = shown, natural = real geometry
  if (const auto m = m_monitor.lock()) {
    // Two passes on purpose: the pin test needs the focused tile's natural, which
    // the loop rewrites as it goes — testing inside it reads the pre-close value.
    LRect selNat{};
    if (m_scene.focused() >= 0) {
      if (const auto w = m_scene.tiles()[static_cast<size_t>(m_scene.focused())].win.lock()) {
        const auto p = w->positionAnimation()->goal();
        const auto s = w->sizeAnimation()->goal();
        selNat = LRect{p.x - m->m_position.x, p.y - m->m_position.y,
                       std::max(1.0, s.x), std::max(1.0, s.y)};
      }
    }
    for (size_t i = 0; i < m_scene.size(); ++i) {
      auto &t = m_scene.tileAt(i);
      t.target = currentBox(t, static_cast<int>(i));
      // The natural box is where the tile RETURNS TO -- so it is only a destination when
      // the window will really be there. Opening on an empty workspace, switching to all
      // and pressing Escape leaves nothing on the landing workspace: every card's natural
      // box then belonged to a workspace that is not coming, and the exit spent itself
      // flying each one toward a tile it could never occupy before dropping it in a
      // single frame. Those cards leave standing and dissolve instead (entryFade).
      if (const auto w = t.win.lock()) {
        if (anim::tileExit(tileGoesHome(t)) == anim::TileExit::InPlace) {
          t.natural = t.target;
          continue;
        }
        const auto p = w->positionAnimation()->goal();
        const auto s = w->sizeAnimation()->goal();
        t.natural = LRect{p.x - m->m_position.x, p.y - m->m_position.y,
                          std::max(1.0, s.x), std::max(1.0, s.y)};
        // When several windows share one geometry — maximized windows, off-screen
        // workspaces — the unfocused cards land under the focused one (drawn last), so
        // their exit is invisible whatever it animates to. Each gets its own exit.
        if (carouselMode() && static_cast<int>(i) != m_scene.focused() &&
            selNat.w >= 1.0 &&
            std::abs(t.natural.x - selNat.x) < 1.0 &&
            std::abs(t.natural.y - selNat.y) < 1.0 &&
            std::abs(t.natural.w - selNat.w) < 1.0 &&
            std::abs(t.natural.h - selNat.h) < 1.0)
          t.natural = t.target; // leaves by shrinking in place, not by vanishing
      }
    }
  }

  restoreLayers();
  m_cursor.onClose();
  // resume from CURRENT progress (1 - raw())
  m_clocks.master.seek(1.0 - m_progress, animDuration());
  m_lastAnimTick = {}; // else a phantom gap rewinds the clocks
  debug::dbg("=== CLOSE from progress " + std::to_string(m_progress).substr(0, 5) + " ===");
  damage();
}

// sync teardown for unload: close() only STARTS it; unload first -> SEGV
void Overview::hardClose() {
  restorePreviewVisibility();
  cancelPendingClick();
  if (m_animPump) {
    m_animPump->cancel();
    m_animPump.reset();
  }

  restoreLayers();
  restoreFill();
  releaseNewWorkspaces();

  m_life.hardClose();
  debug::trace::life("close", m_life.describe());
  m_view.setDesktop( false);
  m_newCardAnim = false;
  m_newCardId = 0;
  m_progress = 0.0;
  m_altTabbing = false;
  m_scene.clear();
  m_strip.clear();
  m_pixel.clearSnapshots();
  m_scene.clearHover();
  m_scene.select(-1);
  m_focusOutside.reset();

  damage();
}

bool Overview::showAllWorkspaces() const {
  return m_view.allOverride() < 0
             ? (cfg::behavior.show_all_workspaces != 0)
             : (m_view.allOverride() != 0);
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
    if (t.win.lock() == w)
      return true;
  if (const auto aw = m->m_activeWorkspace; aw && w->m_workspace == aw)
    return true;
  return false;
}

bool Overview::isTileWindow(const PHLWINDOW &w) const {
    if (!w)
        return false;
    for (const auto &t : m_scene.tiles())
        if (t.win.lock() == w)
            return true;
    return false;
}

// Feeds the recency service from the COMPOSITOR's focus, and only while the
// overview is closed. Inside, the run's selection is internal, so nothing here may
// read it: promoting on an internal hover would make the order depend on where the
// pointer happened to be. Called every frame and deliberately cheap — one pointer
// compare.
void Overview::syncRecency() {
  if (active())
    return;
  const auto now = Desktop::focusState()->window();
  if (now == m_lastFocus)
    return;
  m_lastFocus = now;
  if (now)
    m_recency.promote(now.get());
}

int Overview::tileOf(const PHLWINDOW &w) const {
  for (size_t i = 0; i < m_scene.size(); ++i)
    if (m_scene.tiles()[i].win.lock() == w)
      return static_cast<int>(i);
  return -1;
}

// The recency service is a cache of the live window set and must never outlive it.
// Hyprland reuses window addresses, so an entry for a closed window would hand its
// rank to an unrelated window that lands on the same address.
void Overview::pruneRecency() {
  std::vector<const void *> live;
  live.reserve(m_scene.size());
  for (const auto &t : m_scene.tiles())
    live.push_back(t.win.lock().get());
  m_recency.forgetDead(live);
}

void Overview::deactivate() {
  restorePreviewVisibility();
  if (const auto mm = m_monitor.lock()) {
    mm->m_blurFBDirty = true;
    // The window is still flying into its tile here, so Hyprland's own blur is not
    // rendering yet: for the first frames after the overlay goes the desktop shows
    // through unblurred. Force the full-damage frames open() asks for, or the blur FB
    // is not rebuilt in time and the exit pops.
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

  // re-assert committed focus LAST (stale auto-focus)
  if (const auto pw = m_pendingFocus.lock(); pw && pw->m_isMapped && !pw->isHidden())
    Desktop::focusState()->fullWindowFocus(pw, Desktop::FOCUS_REASON_KEYBIND);
  m_pendingFocus.reset();

  // release EVERY created workspace
  debug::dbg("deactivate (natural close end)");
  if (m_animPump) {
    m_animPump->cancel();
    m_animPump.reset();
  }
  releaseNewWorkspaces();

  m_life.hardClose();
  // The screen is released, so the hooks pass focus through again; this is what actually
  // gives the desktop its focus back rather than waiting for the next mouse move.
  dropFocusFromWindows();
  m_view.setDesktop( false);
  m_view.setAllOverride( -1);
  m_altTabbing = false;
  m_newCardAnim = false;
  m_newCardId = 0;
  m_progress = 0.0;
  m_scene.clear();
  m_strip.clear();
  m_pixel.clearSnapshots();
  m_scene.clearHover();
  m_scene.select(-1);
  damage();
}

void Overview::releaseNewWorkspaces() {
  for (const auto &wref : m_fx.newWorkspaces())
    if (const auto ws = wref.lock())
      ws->setPersistent(false);
  m_fx.clearNewWorkspaces();
}

void Overview::damage() const {
  if (const auto m = m_monitor.lock(); m && g_pHyprRenderer)
    g_pHyprRenderer->damageMonitor(m);
}

// EXECUTION tail: m_scene.tiles() must survive the frame
void Overview::finishPendingDeactivate() {
  if (!m_life.consumeDeactivate())
    return;
  debug::trace::life("deactivate", m_life.describe());
  debug::dbg("handoff: final overlay frame painted, deactivating");
  deactivate();
}

void Overview::rearmanim() const {
  if (!animBusy())
    return;
  if (g_pEventLoopManager)
    const_cast<Overview *>(this)->ensureAnimPump();
}

void Overview::ensureAnimPump() {
  if (!animBusy()) {
    // Disarm, never reset. This runs from inside the timer OWN callback, while the event loop
    // iterates its timer list: dropping the last shared pointer here destroys the timer from
    // inside CEventLoopManager::nudgeTimers(), which then walks freed memory. Backtrace of the
    // Hyprland SIGSEGV that killed the compositor on reload:
    //   CSharedPointer<CEventLoopTimer>::_delete -> nudgeTimers -> wl_event_loop_dispatch_idle
    // The object lives as long as the Overview; it is re-armed below when work resumes.
    if (m_animPump)
      m_animPump->cancel();
    return;
  }
  if (m_animPump && m_animPump->armed())
    return;

  // tick BETWEEN frames, not during a commit
  m_animPump = makeShared<CEventLoopTimer>(
      std::chrono::milliseconds(8),
      [this](SP<CEventLoopTimer> self, void *) {
        if (!animBusy()) {
          self->cancel();
          return;
        }
        damage();
        if (const auto m = m_monitor.lock())
          m->scheduleFrame();
        self->updateTimeout(std::chrono::milliseconds(8));
      },
      nullptr);
  if (g_pEventLoopManager)
    g_pEventLoopManager->addTimer(m_animPump);
}

// snapshot mode: per-window last committed texture, frozen
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

    auto fb = g_pHyprRenderer->createFB("gloview snapshot");
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
    if (const auto w = t.win.lock())
      grab(w);
  for (const auto &it : m_strip)
    for (const auto &sw : it.wins)
      if (const auto w = sw.win.lock())
        grab(w);
}

} // namespace gloview

#include "../config/config.hpp"
#include "../overview.hpp"
#include "../debug/log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <utility>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {


bool Overview::onMouseAxis(const IPointer::SAxisEvent &e) {
  if (!active())
    return false;
  const double notches = e.deltaDiscrete != 0
                             ? static_cast<double>(e.deltaDiscrete)
                             : e.delta / 15.0;

  bool overStrip = false;
  if (const auto m = m_monitor.lock()) {
    const auto mc = g_pInputManager->getMouseCoordsInternal();
    const LRect band = stripBand();
    const double lx = mc.x - m->m_position.x;
    const double ly = mc.y - m->m_position.y;
    overStrip = band.contains(lx, ly);
  }

  // The run pages on the wheel: notches * carousel_scroll_speed = cards moved. Its
  // own switch, not scroll_switches_workspace, which is about WORKSPACES.
  if (!overStrip && carouselMode()) {
    const double speed =
        std::clamp(static_cast<double>(cfg::carousel.scroll_speed), 0.05, 20.0);
    const double cards = notches * speed;
    // Below one card per notch the leftover is carried, so a slow setting moves.
    m_scrollAccum += cards;
    const int whole = static_cast<int>(std::trunc(m_scrollAccum));
    if (whole != 0) {
      m_scrollAccum -= static_cast<double>(whole);
      stepAltTab(whole);
    }
    return true;
  }

  if (!overStrip &&
      cfg::behavior.scroll_switches_workspace != 0) {
    if (notches != 0.0)
      stepWorkspace(notches > 0 ? 1 : -1);
    return true;
  }

  // Modal: swallow the wheel even with nothing to scroll.
  if (!m_scroll.scrollable())
    return true;
  const double step = stripThickness() * 0.9; // ~one card per notch
  animateStripTo(m_scroll.current(),
                 std::clamp(m_scroll.target() + notches * step, 0.0, m_scroll.max()));
  updateHover();
  damage();
  return true;
}

void Overview::onMouseMove() {
  // Damage only the cursor's old+new boxes, never the whole monitor.
  const auto m = m_monitor.lock();
  if (!m || !Pointer::mgr()) {
    updateHover();
    return;
  }
  // The closing zone's glow. BEFORE the hardware-cursor return below, which leaves
  // it untouched and kept the zone dark for anyone with a hardware cursor.
  {
    double g = 0.0;
    if (active() && (draggedTile() >= 0 ||
                     m_drag.press() == model::Drag::Press::StripWin)) {
      const auto mc = g_pInputManager->getMouseCoordsInternal();
      g = killZoneGlow(mc.x - m->m_position.x, mc.y - m->m_position.y);
    }
    // Held just outside the falloff must not repaint every frame.
    if (std::abs(g - m_killGlow) > 0.02) {
      m_killGlow = g;
      damage();
    }
  }
  if (m_cursor.hasHardwareCursor(m)) {
    updateHover();
    return;
  }
  const CBox unionBox = m_cursor.moveDamage(m);
  updateHover();
  if (!active() || unionBox.w <= 0 || unionBox.h <= 0)
    return;
  if (g_pHyprRenderer)
    g_pHyprRenderer->damageBox(unionBox);
}

int Overview::tileAt(double lx, double ly) const {
  if (carouselMode()) {
    // Cards overlap: first-match hits the wrong one, so scan front to back.
    int best = -1, bestDepth = 1 << 30;
    const auto &slot = m_view.slot();
    for (size_t k = 0; k < m_view.carousel().cards.size() && k < slot.size(); ++k) {
      const auto i = static_cast<int>(slot[k]);
      if (!m_view.carousel().cards[k].visible)
        continue;
      // slot is a cache that outlives a shrink of the tile array and can point past its end.
      if (i < 0 || i >= static_cast<int>(m_scene.tiles().size()))
        continue;
      if (!currentBox(m_scene.tiles()[static_cast<size_t>(i)], i).contains(lx, ly))
        continue;
      if (m_view.carousel().cards[k].depth < bestDepth) {
        bestDepth = m_view.carousel().cards[k].depth;
        best = i;
      }
    }
    return best;
  }
  for (size_t i = 0; i < m_scene.tiles().size(); ++i)
    if (currentBox(m_scene.tiles()[i], static_cast<int>(i)).contains(lx, ly))
      return static_cast<int>(i);
  return -1;
}

int Overview::stripItemAt(double lx, double ly) const {
  for (size_t i = 0; i < m_strip.size(); ++i)
    if (stripCardAt(i).contains(lx, ly))
      return static_cast<int>(i);
  return -1;
}

// A press that never travelled is a click, not a drag: drop the lift so the card
// lands back down. Both release paths need this.
void Overview::dropLiftIfNotMoved() {
  if (m_drag.lifted() && m_drag.travelSq() < 64.0) {
    m_drag.setLifted(false);
    m_clocks.dragLift.reset();
    damage();
  }
}

int Overview::draggedTile() const {
  return (m_drag.onTile() && m_drag.lifted() &&
          m_drag.idx() < static_cast<int>(m_scene.tiles().size()))
             ? m_drag.idx()
             : -1;
}

void Overview::updateHover() {
  if (!active())
    return;
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const auto mc = g_pInputManager->getMouseCoordsInternal();
  const double lx = mc.x - m->m_position.x;
  const double ly = mc.y - m->m_position.y;

  if (m_drag.armed()) {
    if (m_drag.lifted()) {
      m_drag.moveTo(lx, ly);
      m_scene.hover(m_drag.onTile() ? m_drag.idx() : -1, stripItemAt(lx, ly));
      damage();
      return;
    }
  }

  const int newTile = tileAt(lx, ly);
  const int newStrip = stripItemAt(lx, ly);
  if (newTile != m_scene.hoveredTile() || newStrip != m_scene.hoveredStrip()) {
    const int oldTile = m_scene.hoveredTile(), oldStrip = m_scene.hoveredStrip(),
              oldSel = m_scene.selected();
    m_scene.hover(newTile, newStrip);
    // Carousel: hover must NOT move the selection (slots are built around it).
    if (newTile >= 0 && cfg::behavior.focus_follows_mouse != 0 &&
        !carouselMode()) {
      m_scene.select(newTile);
    }
    const auto m = m_monitor.lock();
    if (m) {
      // A selection move is not a two-tile repaint. The focused tile changes its ring,
      // its content radius and its exemption from the entry ladder, and the run rebuilds
      // every slot around it -- so the pixels that changed are not confined to the tiles
      // the pointer crossed. Damaging only those two left the rest of the frame holding
      // the previous one's image, which is the reported symptom exactly: a card that
      // flickers as the pointer crosses its border, and a slice of a DIFFERENT card
      // showing up on the one being hovered.
      //
      // Cost is one extra full-monitor repaint on a boundary crossing, which is a
      // fraction of a frame. Cheaper than guessing which tiles the move touched.
      if (oldSel != m_scene.selected()) {
        damage();
        return;
      }
      auto dmgTile = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_scene.tiles().size()))
          return;
        LRect b = currentBox(m_scene.tiles()[idx], idx);
        b.grow(48.0);
        g_pHyprRenderer->damageBox(
            CBox{b.x + m->m_position.x, b.y + m->m_position.y, b.w, b.h});
      };
      auto dmgCard = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_strip.size()))
          return;
        LRect b = stripCardAt(idx);
        b.grow(40.0);
        // absolute logical coords, no pxb scale
        g_pHyprRenderer->damageBox(
            CBox{b.x + m->m_position.x, b.y + m->m_position.y, b.w, b.h});
      };
      dmgTile(oldTile);
      dmgTile(newTile);
      if (oldSel != m_scene.selected())
        dmgTile(oldSel);
      dmgCard(oldStrip);
      dmgCard(newStrip);
    }
  }
}

namespace {
// close_trigger=doubleclick: a touchpad double-tap is the pair.
const auto dblclickWindow = [] {
  return std::chrono::milliseconds(
      std::max(1, cfg::behavior.click_window_ms.get()));
};
} // namespace

bool Overview::onMouseButton(const IPointer::SButtonEvent &e) {
  if (!active())
    return false;

  const auto m = m_monitor.lock();
  if (!m) {
    if (e.state == WL_POINTER_BUTTON_STATE_PRESSED)
      close();
    return true;
  }
  const auto mc = g_pInputManager->getMouseCoordsInternal();
  const double lx = mc.x - m->m_position.x;
  const double ly = mc.y - m->m_position.y;

  if (e.state == WL_POINTER_BUTTON_STATE_PRESSED) {
    m_drag.clear();
    m_drag.miss(e.button);

    if (e.button == BTN_MIDDLE) {
      const int idx = stripItemAt(lx, ly);
      if (idx >= 0 && m_strip[idx].kind != model::StripItem::Kind::Plus && m_strip[idx].kind != model::StripItem::Kind::All)
        closeWorkspaceWindows(m_strip[idx]);
      return true;
    }

    if (tileCloseButtons()) {
      // tileBox is what drawTile used, so the hit-test cannot drift from it.
      const int dragIdx = draggedTile();
      for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
        if (static_cast<int>(i) == dragIdx)
          continue;
        if (carouselMode()) {
          const int k = m_view.slotOf(i);
          // off-run: nothing of it is drawn, so nothing of it can be hit
          if (k < 0 || !m_view.carousel().cards[static_cast<size_t>(k)].visible)
            continue;
        }
        if (closeButtonRect(tileRect(i)).contains(lx, ly)) {
          m_drag.consume(e.button); // handled on press
          closeTileWindow(static_cast<int>(i));
          return true;
        }
      }
    }

    if (m_view.desktop() || closeButtonsAlwaysOn()) {
      for (size_t i = 0; i < m_strip.size(); ++i) {
        if (m_strip[i].kind == model::StripItem::Kind::Plus || m_strip[i].kind == model::StripItem::Kind::All)
          continue;
        const LRect c = stripCardAt(i);
        const LRect br = closeButtonRect(c);
        if (br.contains(lx, ly)) {
          m_drag.consume(e.button);
          closeWorkspaceWindows(m_strip[i]);
          return true;
        }
      }
    }

    for (size_t i = 0; i < m_strip.size(); ++i) {
      const LRect c = stripCardAt(i);
      if (!c.contains(lx, ly))
        continue;
      auto &it = m_strip[i];
      if (it.kind == model::StripItem::Kind::All) {
        m_drag.consume(e.button);
        toggleAllWorkspaces();
      } else if (it.kind == model::StripItem::Kind::Plus) {
        m_drag.consume(e.button);
        addWorkspace();
      } else {
        bool hitSlot = false;
        for (size_t j = 0; j < it.wins.size(); ++j) {
          const auto w = it.wins[j].win.lock();
          if (!w || !w->m_isMapped || w->isHidden())
            continue;
          const LRect wb = stripWinSlotRect(it, c, j);
          if (wb.contains(lx, ly)) {
            m_drag.beginStripWin(static_cast<int>(i), static_cast<int>(j), lx, ly,
                                  e.button, lx - wb.x, ly - wb.y,
                                  stripWinSlotRect(it, c, j), w,
                                  std::chrono::steady_clock::now());
            hitSlot = true;
            break;
          }
        }
        if (!hitSlot) {
          m_drag.consume(e.button);
          switchToWorkspace(it);
        }
      }
      return true;
    }
    if (const int hit = tileAt(lx, ly); hit >= 0) {
      const LRect b = currentBox(m_scene.tiles()[hit], hit);
      m_drag.beginTile(hit, lx, ly, e.button, lx - b.x, ly - b.y,
                       tileBox(static_cast<size_t>(hit), b),
                       std::chrono::steady_clock::now());
      return true;
    }
    m_drag.miss(e.button);
    return true;
  }

  if (e.button == BTN_MIDDLE)
    return true; // middle was fully handled on press

  switch (m_drag.press()) {
  case model::Drag::Press::Consumed: { // handled on press
    debug::trace::drag("release", m_drag.describe());
    m_drag.clear();
    m_killGlow = 0.0;
    damage();
    return true;
  }
  case model::Drag::Press::Tile: {
    const int press = m_drag.idx();
    m_killGlow = 0.0; // a stale glow would stay painted after the drop
    const auto w = m_scene.tiles()[press].win.lock();
    const double grabDX = m_drag.grabX(), grabDY = m_drag.grabY();
    debug::trace::drag("release", m_drag.describe() + "|rmb=" +
                       std::to_string(m_drag.button() == BTN_RIGHT) + "|win=" +
                       (w ? "valid" : "expired"));
    dropLiftIfNotMoved();
    if (m_drag.lifted()) {
      const bool rmb = m_drag.button() == BTN_RIGHT;
      m_lastDragBox = dragVisualBox();
      auto oldBoxes = captureCurrentBoxes(w);
      m_drag.clear();
      int cardIdx = -1;
      for (int i = 0; i < static_cast<int>(m_strip.size()); ++i)
        if (stripCardAt(i).contains(lx, ly)) {
          cardIdx = i;
          break;
        }
      if (cardIdx >= 0) {
        // Any strip card is a drop target, not just a Ws one. "+" is Plus, so matching only
        // Kind::Ws left cardIdx at -1 and skipped the whole block below -- INCLUDING
        // dropOnStripCard -- and the tile always flew back (measured: branch=Tile->flyback
        // with dropOnStripCard never entered). A press on "+" is a click, but a DRAG that
        // ends there starts elsewhere and never goes through the press handler.
        const bool wsCard = m_strip[cardIdx].kind == model::StripItem::Kind::Ws;
        debug::trace::dragBranch("Tile->stripCard",
                                 "rmb=" + std::to_string(rmb) + "|kind=" +
                                     std::to_string(wsCard ? 1 : 0));
        if (rmb && wsCard) {
          PHLWINDOW vj;
          const auto &sit = m_strip[cardIdx];
          for (size_t j = 0; j < sit.wins.size(); ++j) {
            const auto v = sit.wins[j].win.lock();
            if (!v || v == w || !v->m_isMapped || v->isHidden())
              continue;
            if (stripWinSlotRect(sit, stripCardAt(cardIdx), j).contains(lx, ly)) {
              vj = v;
              break;
            }
          }
          if (vj) {
            if (swapWindows(w, vj, m_lastDragBox, true))
              kickPulse(w), kickPulse(vj);
            else damage();
          } else {
            swapOnWorkspace(w, m_strip[cardIdx]); // no slot hit → last-focused fallback
            kickPulse(w);
            landAfterSwap(w, m_lastDragBox, m_lastDragBox, m_lastDragBox,
                          anim::cfg(anim::Id::Drop).ms, anim::cfg(anim::Id::Drop).curve,
                          true);
          }
          return true;
        }
        // LMB on any card, rmb on a non-Ws one: dropOnStripCard creates the workspace for
        // "+" (dropOnWorkspace already handles Kind::Plus) and moves the window in.
        if (dropOnStripCard(w, lx, ly, -1)) {
          kickPulse(w);
          landAfterSwap(w, m_lastDragBox, m_lastDragBox, m_lastDragBox,
                        anim::cfg(anim::Id::Drop).ms, anim::cfg(anim::Id::Drop).curve,
                        true);
          return true;
        }
        // A drop that resolved to nothing still flies back: fall through to the checks below.
      }
      // The closing zone wins over everything else: a card dropped on it closes,
      // whether the run is a carousel or a plain layout.
      if (w && killZoneRect().contains(lx, ly)) {
        debug::trace::dragBranch("Tile->killZone");
        // Hand the centre to whatever lands in the closed window's place. By
        // index: the tile is being rebuilt and its neighbour shifts into this slot.
        if (press == m_scene.selected() && m_scene.tiles().size() > 1) {
          m_scene.select(static_cast<int>(press % (m_scene.tiles().size() - 1)));
          m_scene.select(std::clamp(m_scene.selected(), 0,
                                   static_cast<int>(m_scene.tiles().size()) - 2));
        }
        closeTileWindow(press); // carries the flight out with it
        return true;
      }
      // Nearest tile within gap/2, not containment: a natural drop lands in the gap.
      if (!m_view.desktop() && w &&
          cfg::behavior.drag_to_swap != 0) {
        int best = -1;
        double bestDist2 = 1e18;
        for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
          if (static_cast<int>(i) == press)
            continue;
          const LRect b = currentBox(m_scene.tiles()[i], static_cast<int>(i));
          const double cx = std::clamp(lx, b.x, b.x + b.w);
          const double cy = std::clamp(ly, b.y, b.y + b.h);
          const double dx = lx - cx, dy = ly - cy;
          const double d2 = dx * dx + dy * dy;
          if (d2 < bestDist2) {
            bestDist2 = d2;
            best = static_cast<int>(i);
          }
        }
        const double tol =
            std::max(0.0, cfg::grid.gap / 2.0);
        if (best >= 0 && bestDist2 <= tol * tol) {
          debug::trace::dragBranch("Tile->gridSwap");
          // The release point, not the slot it was picked up from: the card has to land
          // from under the cursor.
          swapTiles(press, best, m_lastDragBox);
          return true;
        }
      }
      // Desktop mode parks the preview; the box lives on the Tile so
      // rebuilds keep it.
      if (m_view.desktop() && w) {
        debug::trace::dragBranch("Tile->desktopPark");
        const LRect cur =
            m_scene.tiles()[press].target;
        const LRect parked{lx - grabDX, ly - grabDY, cur.w, cur.h};
        m_scene.park(static_cast<size_t>(press), parked);
        m_scene.hover(-1, m_scene.hoveredStrip());
        damage();
        return true;
      }
      debug::trace::dragBranch("Tile->flyback");
      // Its own glide source: captureCurrentBoxes excludes it, so startTileGlide
      // called it a newcomer and it faded in while a swap FX flew a second copy.
      if (w)
        oldBoxes.emplace_back(w, m_lastDragBox);
      replayReflow(oldBoxes);
      ensureAnimPump();
      damage();
      return true;
    }
    m_drag.clear();
    // The run comes to the card you clicked; only the FOCUSED card commits.
    // Setting m_scene.selected() is enough — relayoutCarousel glides the run around it.
    if (carouselMode() && press >= 0 && press < static_cast<int>(m_scene.tiles().size()) &&
        press != m_scene.selected()) {
      debug::trace::dragBranch("Tile->carouselFocus", "idx=" + std::to_string(press));
      m_scene.select(press);
      damage();
      return true;
    }
    if (closeOnDoubleClick() && w && g_pEventLoopManager) {
      if (m_clickTimer && m_pendingClickWin.lock() == w) {
        cancelPendingClick();
        closeTileWindow(press);
        return true;
      }
      cancelPendingClick(); // drop any OTHER tile's still-pending timer first
      m_pendingClickWin = w;
      m_clickTimer = makeShared<CEventLoopTimer>(
          dblclickWindow(),
          [this](SP<CEventLoopTimer>, void *) {
            const auto pw = m_pendingClickWin.lock();
            m_pendingClickWin.reset();
            if (pw && active())
              focusAndClose(pw, Desktop::FOCUS_REASON_CLICK);
          },
          nullptr);
      g_pEventLoopManager->addTimer(m_clickTimer);
      return true;
    }
    focusAndClose(w, Desktop::FOCUS_REASON_CLICK);
    return true;
  }

  case model::Drag::Press::StripWin: {
    const int stripItem = m_drag.idx();
    const auto w = m_drag.win().lock();
    const bool rmb = m_drag.button() == BTN_RIGHT;
    dropLiftIfNotMoved();
    const bool lifted = m_drag.lifted();
    const LRect fromBox = dragVisualBox();
    m_lastDragBox = fromBox;
    debug::trace::drag("release", m_drag.describe() + "|rmb=" +
                       std::to_string(rmb) + "|win=" +
                       (w ? "valid" : "expired"));
    m_drag.clear();
    if (lifted) {
      if (w) {
        // Slot hit swaps tiling slots for RMB or a same-workspace partner.
        for (size_t i = 0; i < m_strip.size(); ++i) {
          const auto &it = m_strip[i];
          if (it.kind != model::StripItem::Kind::Ws)
            continue;
          for (size_t j = 0; j < it.wins.size(); ++j) {
            const auto v = it.wins[j].win.lock();
            if (!v || v == w || !v->m_isMapped || v->isHidden())
              continue;
            if (!stripWinSlotRect(it, stripCardAt(i), j).contains(lx, ly))
              continue;
            const bool sameWs =
                v->m_workspace == w->m_workspace;
            if (!rmb && !sameWs)
              break;
            if (swapWindows(w, v, fromBox, true)) {
              debug::trace::dragBranch("StripWin->slotSwap");
              kickPulse(w);
              kickPulse(v);
              return true;
            }
            debug::trace::dragBranch("StripWin->slotSwapFailed");
            damage();
            return true;
          }
        }
      }
      if (w) {
        if (dropOnStripCard(w, lx, ly, stripItem)) {
          debug::trace::dragBranch("StripWin->dropOnStripCard");
          kickPulse(w);
          landAfterSwap(w, fromBox, fromBox, fromBox, anim::cfg(anim::Id::Drop).ms,
                        anim::cfg(anim::Id::Drop).curve, true);
          return true;
        }
        const int homeIdx = stripItemAt(lx, ly);
        if (homeIdx == stripItem || homeIdx < 0) {
          debug::trace::dragBranch("StripWin->snapBack");
          LRect ownSlot = fromBox;
          for (size_t si = 0; si < m_strip.size(); ++si) {
            const auto &sit = m_strip[si];
            if (sit.kind != model::StripItem::Kind::Ws)
              continue;
            for (size_t j = 0; j < sit.wins.size(); ++j) {
              if (sit.wins[j].win.lock() == w) {
                ownSlot = stripWinSlotRect(sit, stripCardAt(si), j);
                break;
              }
            }
          }
          beginSwapFX(w, fromBox,
                      parseSwapStyle(anim::stripSwapStyle()),
                      anim::cfg(anim::Id::SwapFly).ms, anim::cfg(anim::Id::SwapFly).curve);
          // beginSwapFX may reuse a slot: force `from` on the entry it just pushed.
          m_fx.retargetLastSwap(fromBox);
          kickPulse(w);
          damage();
          return true;
        }
        // The strip band is NOT a drop target: releasing back over it is a cancel.
        bool onBand = false;
        if (const auto m = m_monitor.lock())
          onBand = stripBand().contains(lx, ly);
        const auto mm = m_monitor.lock();
        if (!onBand && mm &&
            LRect{0, 0, mm->m_size.x, mm->m_size.y}.contains(lx, ly)) {
          if (rmb) {
            int best = -1;
            double bestDist2 = 1e18;
            for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
              const auto tv = m_scene.tiles()[i].win.lock();
              if (!tv || tv == w) continue;
              const LRect b = currentBox(m_scene.tiles()[i], static_cast<int>(i));
              const double cx = std::clamp(lx, b.x, b.x + b.w);
              const double cy = std::clamp(ly, b.y, b.y + b.h);
              const double d2 = (lx-cx)*(lx-cx) + (ly-cy)*(ly-cy);
              if (d2 < bestDist2) { bestDist2 = d2; best = static_cast<int>(i); }
            }
            const double tol = std::max(0.0, cfg::grid.gap / 2.0 + 8.0);
            if (best >= 0 && bestDist2 <= tol * tol) {
              const auto vj = m_scene.tiles()[best].win.lock();
              if (vj && swapWindows(w, vj, fromBox, true)) {
                debug::trace::dragBranch("StripWin->gridTileSwap");
                kickPulse(w); kickPulse(vj);
                return true;
              }
            }
            for (const auto &it : m_strip) {
              if (it.kind != model::StripItem::Kind::Plus &&
                  it.kind != model::StripItem::Kind::All && it.active) {
                swapOnWorkspace(w, it);
                debug::trace::dragBranch("StripWin->swapOnWorkspace");
                kickPulse(w);
                landAfterSwap(w, fromBox, fromBox, fromBox, anim::cfg(anim::Id::Drop).ms,
                              anim::cfg(anim::Id::Drop).curve, true);
                return true;
              }
            }
          } else {
            for (const auto &it : m_strip) {
              if (it.kind != model::StripItem::Kind::Plus &&
                  it.kind != model::StripItem::Kind::All && it.active) {
                dropOnWorkspace(w, it);
                debug::trace::dragBranch("StripWin->dropOnWorkspace");
                kickPulse(w);
                landAfterSwap(w, fromBox, fromBox, fromBox, anim::cfg(anim::Id::Drop).ms,
                              anim::cfg(anim::Id::Drop).curve, true);
                return true;
              }
            }
          }
        }
      }
      if (lifted) {
        debug::trace::dragBranch("StripWin->flyback");
        if (auto *home = homeStripCardFor(w))
          switchToWorkspace(*home);
        beginSwapFX(w, fromBox, model::SwapStyle::Horizontal,
                    anim::cfg(anim::Id::SwapFly).ms, anim::cfg(anim::Id::SwapFly).curve);
      }
      damage();
      return true;
    }
    debug::trace::dragBranch("StripWin->shortClick");
    if (auto *home = homeStripCardFor(w))
      switchToWorkspace(*home);
    return true;
  }
  default:
    break;
  }

  if (cfg::behavior.exit_on_click != 0)
    close();
  return true;
}

// True if focusing `w` switches the monitor's real workspace (it slides).
bool Overview::crossesRealWorkspace(const PHLWINDOW &w) const {
  const auto m = m_monitor.lock();
  return w && m && w->m_workspace && w->m_workspace != m->m_activeWorkspace;
}

void Overview::focusAndClose(const PHLWINDOW &w, Desktop::eFocusReason reason) {
  // Cross-workspace commit must not focus here; m_pendingFocus defers to
  // deactivate(), after the overlay is gone.
  const bool deferFocus = crossesRealWorkspace(w);
  if (w) {
    if (const auto ws = w->m_workspace;
        ws && (showAllWorkspaces() || ws != m_workspace.lock())) {
      // Nothing is rebuilt here. focusAndClose runs DURING the close and
      // layoutTiles() recomputes m_view.run(), so with m_view.allOverride() already 0 the deck
      // stopped being a carousel before close() read it — every card flew to its
      // own box, all the same fullscreen rectangle, and met in one point.
      m_view.setAllOverride( 0);
      m_workspace = ws;
      m_scene.clearHover();
      m_scene.select(-1);
      for (size_t i = 0; i < m_scene.tiles().size(); ++i)
        if (m_scene.tiles()[i].win.lock() == w) {
          m_scene.select(static_cast<int>(i));
          break;
        }
    }
  }
  m_pendingFocus = w;
  close();
  if (w && !deferFocus)
    Desktop::focusState()->fullWindowFocus(w, reason);
}

void Overview::cancelPendingClick() {
  m_pendingClickWin.reset();
  if (m_clickTimer) {
    m_clickTimer->cancel();
    if (g_pEventLoopManager)
      g_pEventLoopManager->removeTimer(m_clickTimer);
    m_clickTimer.reset();
  }
}

} // namespace gloview

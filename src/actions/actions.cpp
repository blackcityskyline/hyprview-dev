#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <utility>

#include <hyprland/src/desktop/history/WindowHistoryTracker.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

model::StripItem *Overview::homeStripCardFor(const PHLWINDOW &w) {
  if (!w)
    return nullptr;
  for (auto &it : m_strip)
    if (it.kind == model::StripItem::Kind::Ws && it.ws.lock() == w->m_workspace)
      return &it;
  return nullptr;
}

bool Overview::dropOnStripCard(const PHLWINDOW &w, double lx, double ly,
                               int skipItem) {
  const int idx = stripItemAt(lx, ly);
  // One to_string per card, built on every drop even with logs off.
  if (debug::enabled()) {
    std::string s = "[DROP] at=" + std::to_string(static_cast<int>(lx)) + "," +
                    std::to_string(static_cast<int>(ly)) +
                    " idx=" + std::to_string(idx) +
                    " skip=" + std::to_string(skipItem) + " cards=" +
                    std::to_string(m_strip.size()) + " [";
    for (size_t i = 0; i < m_strip.size(); ++i) {
      const LRect c = stripCardAt(i);
      s += std::to_string(static_cast<int>(c.x)) + "," +
           std::to_string(static_cast<int>(c.y)) + " " +
           std::to_string(static_cast<int>(c.w)) + "x" +
           std::to_string(static_cast<int>(c.h)) + ";";
    }
    debug::dbg(s + "]");
  }
  if (idx < 0 || idx == skipItem)
    return false;
  // COPY: the rebuild below frees the element a reference would dangle on.
  const auto it = m_strip[idx];
  if (it.kind != model::StripItem::Kind::Ws) {
    if (m_drag.button() == BTN_RIGHT)
      swapOnWorkspace(w, it);
    else
      dropOnWorkspace(w, it);
    return true;
  }

  PHLWINDOW vj;
  for (size_t j = 0; j < it.wins.size(); ++j) {
    const auto v = it.wins[j].win.lock();
    if (!v || v == w || !v->m_isMapped || v->isHidden())
      continue;
    if (stripWinSlotRect(it, stripCardAt(idx), j).contains(lx, ly)) {
      vj = v;
      break;
    }
  }

  // assignToSpace splits beside vj without focusing it (RASSERT off-ws).
  if (vj && cfg::behavior.tile_layout.get() == "dwindle") {
    const auto tvj = vj->layoutTarget();
    const auto tw  = w->layoutTarget();
    if (tw && tvj && tvj->space()) {
      auto oldBoxes = captureCurrentBoxes(w);
      const auto vjPos  = vj->positionAnimation()->goal();
      const auto vjSize = vj->sizeAnimation()->goal();
      const Vector2D focalPoint{vjPos.x + vjSize.x / 2.0,
                                vjPos.y + vjSize.y / 2.0};
      tw->assignToSpace(tvj->space(), focalPoint);
      replayReflow(oldBoxes);
      damage();
      ensureAnimPump();
      return true;
    }
  }

  if (vj)
    swapWindows(w, vj);
  else if (m_drag.button() == BTN_RIGHT)
    swapOnWorkspace(w, it);
  else
    dropOnWorkspace(w, it);
  return true;
}

void Overview::dropOnWorkspace(const PHLWINDOW &w, const model::StripItem &it) {
  if (w && w->m_workspace == it.ws.lock()) {
    damage();
    return;
  }
  const auto m = m_monitor.lock();
  if (!w || !m) {
    damage();
    return;
  }

  PHLWORKSPACE target;
  if (it.kind == model::StripItem::Kind::Plus) {
    int id = 1;
    while (State::workspaceState()->query().id(id).run())
      ++id;
    target = State::workspaceState()->create(id, m->m_id);
    m_newCardId = id;
    m_clocks.newCard.begin();
    m_newCardAnim = true;
  } else if (it.virtualWs) {
    // Placeholder card: create exactly this id, persistent or it is reaped.
    target = State::workspaceState()->query().id(it.id).run();
    if (!target)
      target = State::workspaceState()->create(it.id, m->m_id);
    if (target) {
      target->setPersistent(true);
      m_fx.addNewWorkspace(target);
      m_newCardId = it.id;
      m_clocks.newCard.begin();
      m_newCardAnim = true;
    }
  } else
    target = it.ws.lock();

  if (!target || target == w->m_workspace) {
    damage();
    return;
  }

  auto oldBoxes = captureCurrentBoxes(w);

  Desktop::globalWindowController()->moveWindowToWorkspace(w, target);

  if (cfg::behavior.switch_on_drop != 0) {
    model::StripItem dst;
    dst.ws = target;
    switchToWorkspace(dst);
    return;
  }

  // replayReflow keeps the chrome settled (m_progress pinned at 1).
  replayReflow(oldBoxes);
  damage();
}

// Rebuild is load-bearing: without it the tile slot desyncs from the
// window's real goal().
bool Overview::swapWindows(const PHLWINDOW &wa, const PHLWINDOW &wb,
                           const LRect &fromA, bool draggedOnStrip) {
  if (!wa || !wb || wa == wb ||
      Fullscreen::controller()->isFullscreen(wa) ||
      Fullscreen::controller()->isFullscreen(wb))
    return false;
  const auto ta = wa->layoutTarget();
  const auto tb = wb->layoutTarget();
  if (!g_layoutManager || !ta || !tb)
    return false;

  auto oldBoxes = captureCurrentBoxes();
  // Captured before the rebuild, which replaces the strip: this is where the thumbnail
  // was, and it is the origin of the slide the bystander surface plays.
  const LRect thumbA = stripThumbRect(wa), thumbB = stripThumbRect(wb);
  g_layoutManager->switchTargets(ta, tb);
  replayReflow(oldBoxes);
  // The overlay's own exchange, and ONLY when the rebuild left nothing to animate.
  //
  // switchTargets moves the REAL windows, but only when it can: two windows already at
  // the same geometry (one behind the other, both tiled full) leave goal() unchanged,
  // the rebuild hands each one back the box it had, and the glide has no distance to
  // cover. Then the layout cannot tell them apart and the exchange has to be expressed
  // in the card slots, which is what the user is looking at.
  //
  // Guarded on tileAtRest because when the rebuild DID move them, startTileGlide has
  // already armed the exchange -- natural is the captured old box, target is the new
  // one -- and trading the slots again would land each window back on its own box and
  // cancel the flight. That is what made the FIRST swap animate and every one after it
  // stand still.
  const int ia = m_scene.tileIndexOf(wa.get());
  const int ib = m_scene.tileIndexOf(wb.get());
  if (m_scene.tileAtRest(ia) && m_scene.tileAtRest(ib)) {
    m_scene.swapTiles(ia, ib);
    // Both halves, together. The targets move the geometry; the slots carry the depth
    // that goes with it. Without the second the run keeps each tile's OLD card, and the
    // periphery -- now carrying depth 0 -- paints over the centre.
    m_view.swapSlots(static_cast<std::size_t>(ia), static_cast<std::size_t>(ib));
  }

  // The initiator flies from where it was RELEASED, which is under the cursor and not
  // the card slot it was picked up from -- that gap is the whole drag. Applied AFTER the
  // exchange decision, which reads natural == target and would see this as "the rebuild
  // moved it" and skip the swap.
  const bool dropped = fromA.w > 0.0 && fromA.h > 0.0;
  if (dropped)
    m_scene.setNatural(ia, fromA);

  for (const auto &[win, box] : oldBoxes) {
    const bool isA = win == wa;
    const auto leaf = isA ? anim::Id::SwapMain : anim::Id::SwapPartner;
    landAfterSwap(win, box, isA ? thumbA : thumbB, isA && dropped ? fromA : LRect{},
                  anim::cfg(leaf).ms, anim::cfg(leaf).curve, draggedOnStrip);
  }
  kickPulse(wa);
  kickPulse(wb);
  damage();
  ensureAnimPump();
  return true;
}

// Automation seam. One method, not five public accessors: everything a harness needs
// to drive the overlay deterministically, and nothing it does not.
//
// Returns "" on success, a short reason otherwise -- the caller (dispatcher or Lua)
// turns a failure into a visible error instead of a silent no-op, which is what makes
// a matrix run trustworthy.
std::string Overview::runCommand(const std::string &args) {
  std::vector<std::string> tok;
  std::string cur;
  for (const char c : args + ' ') {
    if (c == ' ')
      tok.emplace_back(std::exchange(cur, std::string()));
    else
      cur.push_back(c);
  }
  while (!tok.empty() && tok.back().empty())
    tok.pop_back();
  if (tok.empty())
    return "no command";

  const auto num = [&tok](std::size_t i) {
    return i < tok.size() ? std::atoi(tok[i].c_str()) : -1;
  };

  // "state" first: it is how a harness learns what it is driving, and it must work
  // whether or not the overlay is open.
  if (tok[0] == "state") {
    std::string out = "STATE|open=" + std::to_string(m_life.active()) +
                      "|pending=" + std::to_string(m_life.parked()) + "|tiles=" +
                      std::to_string(m_scene.size()) + "|strip=" +
                      std::to_string(m_strip.size()) + "|allws=" +
                      std::to_string(showAllWorkspaces()) + "|carousel=" +
                      std::to_string(carouselMode()) + "|ccx=" +
                      // Where the run is centred, so a harness can tell which card is
                      // the middle one without re-deriving the layout.
                      std::to_string(static_cast<int>(m_view.carousel().area.x +
                                                     m_view.carousel().originX +
                                                     m_view.carousel().area.w / 2.0));
    for (std::size_t i = 0; i < m_scene.size(); ++i) {
      const auto w = m_scene.tiles()[i].win.lock();
      const auto &t = m_scene.tiles()[i];
      // depth travels with the card, not the tile: the run reads it as
      // cards[slotOf(tile)].depth. Dumping it beside the box is what lets a harness
      // catch a tile that moved and kept someone else's depth -- the bug that put the
      // periphery over the centre, invisible to a geometry-only trace.
      int depth = -1;
      if (carouselMode()) {
        const int k = m_view.slotOf(i);
        if (k >= 0 && static_cast<std::size_t>(k) < m_view.carousel().cards.size())
          depth = m_view.carousel().cards[static_cast<std::size_t>(k)].depth;
      }
      out += "\nTILE|" + std::to_string(i) + "|" +
             (w ? w->m_class : std::string("gone")) + "|" +
             (w && w->m_workspace ? std::to_string(w->m_workspace->m_id) : std::string("-")) +
             "|" + std::to_string(static_cast<int>(t.target.x)) +
             "," + std::to_string(static_cast<int>(t.target.y)) +
             "," + std::to_string(static_cast<int>(t.target.w)) +
             "," + std::to_string(static_cast<int>(t.target.h)) +
             "|d=" + std::to_string(depth);
    }
    // Cards with their window counts. A strip-to-strip exchange needs two windows on ONE
    // workspace; asking on a card that holds one is a precondition, and without this the
    // harness cannot tell "nothing to swap with" from "the drop is broken".
    for (std::size_t i = 0; i < m_strip.size(); ++i) {
      const auto &it = m_strip[i];
      const char *kind = it.kind == model::StripItem::Kind::Ws   ? "Ws"
                         : it.kind == model::StripItem::Kind::Plus ? "Plus"
                                                                  : "All";
      out += "\nCARD|" + std::to_string(i) + "|" + kind + "|" +
             std::to_string(it.wins.size());
    }
    debug::trace::emitState(out);
    return "";
  }

  // Mode switches a matrix has to set up before it can measure anything. They call the
  // same methods the binds do, so a harness and a user drive identical code.
  if (tok[0] == "open") {
    if (!m_life.ownsScreen())
      open();
    return "";
  }
  if (tok[0] == "close") {
    if (openedOrParked())
      close();
    return "";
  }
  if (tok[0] == "allws") {
    // Explicit set, not a toggle: a matrix must be able to ask for the state it wants
    // without reading and reasoning about the current one first.
    const bool want = num(1) != 0;
    if (want != showAllWorkspaces())
      toggleAllWorkspaces();
    return "";
  }

  if (tok[0] == "tuning") {
    // What the harness believes it set. Without this a mismatch between "asked" and
    // "ran" has two possible homes -- the write or the read -- and guessing which is
    // exactly the mistake this file exists to prevent.
    std::string out = "TUNING|n=" + std::to_string(anim::tuningKeys().size());
    for (const auto &kv : anim::tuningKeys())
      out += "|" + kv;
    debug::trace::emitState(out);
    return "";
  }

  if (tok[0] == "set") {
    if (tok.size() < 3)
      return "set: need <key> <value>";
    if (!anim::setTuning(tok[1], tok[2]))
      return "set: unknown key " + tok[1];
    // Anim cfg is re-read every frame, but the damage makes the change visible at once
    // instead of on the next unrelated repaint.
    damage();
    return "";
  }

  if (!openedOrParked())
    return "overview not open";

  if (tok[0] == "sel") {
    const int n = num(1);
    if (n < 0 || n >= static_cast<int>(m_scene.size()))
      return "sel: index out of range";
    m_scene.select(n);
    damage();
    return "";
  }

  if (tok[0] == "swap") {
    const int a = num(1), b = num(2);
    const int n = static_cast<int>(m_scene.size());
    if (a < 0 || b < 0 || a >= n || b >= n || a == b)
      return "swap: bad indices";
    if (!swapWindows(m_scene.tiles()[a].win.lock(), m_scene.tiles()[b].win.lock(),
                     {}, false))
      return "swap: refused by Hyprland";
    kickPulse(m_scene.tiles()[a].win.lock());
    kickPulse(m_scene.tiles()[b].win.lock());
    damage();
    return "";
  }

  // --- drag scenarios ----------------------------------------------------------------
  // Each mirrors ONE release branch in input/mouse.cpp and calls the same functions the
  // pointer path calls, so the landing, the ghost set and the flight are the real ones.
  // What this does NOT cover is the hit test that picks the branch: it resolves the
  // target by index and synthesises the point, so "nearest tile within gap/2" and
  // "which card is under the cursor" stay untested. That is the trade for having any
  // coverage at all without moving a real pointer.
  const auto tile = [&](int i) -> PHLWINDOW {
    return i >= 0 && i < static_cast<int>(m_scene.size())
               ? m_scene.tiles()[static_cast<std::size_t>(i)].win.lock()
               : nullptr;
  };
  const auto cardCentre = [&](int i, double &x, double &y) {
    if (i < 0 || i >= static_cast<int>(m_strip.size()))
      return false;
    const LRect c = stripCardAt(static_cast<std::size_t>(i));
    x = c.x + c.w / 2.0;
    y = c.y + c.h / 2.0;
    return true;
  };

  // Grid tile onto a strip card. The "+" card is Kind::Plus and creates the workspace --
  // the same call the LMB branch makes, so "grid -> +" needs no separate command.
  if (tok[0] == "dropcard") {
    const int t = num(1), ci = num(2);
    const auto w = tile(t);
    double x = 0, y = 0;
    if (!w)
      return "dropcard: bad tile";
    if (!cardCentre(ci, x, y))
      return "dropcard: bad card";
    if (!dropOnStripCard(w, x, y, -1))
      return "dropcard: refused";
    kickPulse(w);
    landAfterSwap(w, m_lastDragBox, m_lastDragBox, m_lastDragBox,
                  anim::cfg(anim::Id::Drop).ms, anim::cfg(anim::Id::Drop).curve, true);
    return "";
  }

  // Strip thumbnail onto a slot in another card -- the strip-to-strip exchange.
  if (tok[0] == "dropslot") {
    const int t = num(1), ci = num(2), si = num(3);
    const auto w = tile(t);
    if (!w)
      return "dropslot: bad tile";
    if (ci < 0 || ci >= static_cast<int>(m_strip.size()))
      return "dropslot: bad card";
    const auto &sit = m_strip[static_cast<std::size_t>(ci)];
    if (si < 0 || si >= static_cast<int>(sit.wins.size()))
      return "dropslot: bad slot";
    const auto v = sit.wins[static_cast<std::size_t>(si)].win.lock();
    if (!v || v == w)
      return "dropslot: slot empty or is the same window";
    if (!swapWindows(w, v, m_lastDragBox, true))
      return "dropslot: refused by Hyprland";
    kickPulse(w);
    kickPulse(v);
    return "";
  }

  // Strip thumbnail onto the grid's other tile -- strip-to-grid.
  if (tok[0] == "stripgrid") {
    const int t = num(1), other = num(2);
    const auto w = tile(t), v = tile(other);
    if (!w || !v || t == other)
      return "stripgrid: bad tiles";
    if (!swapWindows(w, v, m_lastDragBox, true))
      return "stripgrid: refused by Hyprland";
    kickPulse(w);
    kickPulse(v);
    return "";
  }

  // Onto the closing zone. Closes the tile and flies it out, as Tile->killZone does.
  if (tok[0] == "killz") {
    const int t = num(1);
    const auto w = tile(t);
    if (!w)
      return "killz: bad tile";
    if (t == m_scene.selected() && m_scene.size() > 1)
      m_scene.select(static_cast<int>(t % (m_scene.size() - 1)));
    closeTileWindow(t);
    beginSwapFX(w, m_lastDragBox, model::SwapStyle::Horizontal,
                anim::cfg(anim::Id::SwapFly).ms, anim::cfg(anim::Id::SwapFly).curve, false);
    ensureAnimPump();
    damage();
    return "";
  }

  if (tok[0] == "carousel") {
    const bool want = num(1) != 0;
    if (want != carouselMode())
      toggleCarousel();
    return "";
  }

  // Enter commits the focused window, Escape cancels. Same decision the key handler makes,
  // so this exercises toggleIntent and the close path that follows it.
  if (tok[0] == "commit" || tok[0] == "cancel") {
    const bool commit = tok[0] == "commit";
    if (!openedOrParked())
      return "not open";
    const auto intent = anim::toggleIntent(true, m_scene.hasSelection() && commit);
    if (intent == anim::DismissIntent::Commit) {
      if (const auto w = m_scene.tiles()[static_cast<std::size_t>(std::max(0, m_scene.focused()))].win.lock())
        closeTileWindow(m_scene.focused());
    }
    close();
    damage();
    return "";
  }

  if (tok[0] == "movews") {
    const int t = num(1), ws = num(2);
    if (t < 0 || t >= static_cast<int>(m_scene.size()))
      return "movews: index out of range";
    const auto w = m_scene.tiles()[t].win.lock();
    const auto m = m_monitor.lock();
    if (!w || !m)
      return "movews: no window";
    auto target = State::workspaceState()->query().id(ws).run();
    if (!target)
      target = State::workspaceState()->create(ws, m->m_id);
    if (!target)
      return "movews: no workspace";
    auto oldBoxes = captureCurrentBoxes(w);
    Desktop::globalWindowController()->moveWindowToWorkspace(w, target);
    replayReflow(oldBoxes);
    landAfterSwap(w, LRect{}, LRect{}, m_lastDragBox, anim::cfg(anim::Id::Drop).ms,
                  anim::cfg(anim::Id::Drop).curve, false);
    damage();
    return "";
  }

  return "unknown command: " + tok[0];
}

void Overview::swapTiles(int a, int b, const LRect &fromA) {
  if (a < 0 || b < 0 || a == b || a >= static_cast<int>(m_scene.tiles().size()) ||
      b >= static_cast<int>(m_scene.tiles().size())) {
    damage();
    return;
  }
  if (!swapWindows(m_scene.tiles()[a].win.lock(), m_scene.tiles()[b].win.lock(),
                   fromA, false)) // dragged on the grid
    damage();
}

// RMB on a strip card swaps `w` with that workspace's last-focused window.

static const char *swapStyleName(model::SwapStyle s) {
  switch (s) {
  case model::SwapStyle::SlideVert: return "slidevert";
  case model::SwapStyle::Fade: return "fade";
  case model::SwapStyle::Pop: return "pop";
  case model::SwapStyle::Horizontal: break;
  }
  return "horizontal";
}

void Overview::beginSwapFX(const PHLWINDOW &w, const LRect &from,
                           model::SwapStyle style, double ms,
                           const std::string &curve, bool toStrip) {
  if (!w || !w->m_isMapped || w->isHidden())
    return;
  // Same surface only: a window in the all-workspaces view has two live flights at once,
  // and dropping by window alone meant the second one cancelled the first.
  m_fx.dropSwap([&w, toStrip](const model::SwapFX &f) {
    return f.win.lock() == w && f.toStrip == toStrip;
  });
  model::SwapFX fx;
  fx.win     = w;
  fx.from    = from;
  fx.toStrip = toStrip;
  fx.style   = style;
  fx.ms      = ms;
  fx.curve   = curve;
  fx.clock.begin();
  fx.traceSeq = debug::trace::nextSeq();
  fx.traceStart = std::chrono::steady_clock::now();

  // Emitted HERE, not in the painter: paint() is const and cannot mark the flight as
  // traced, and by this point the scene has already been rebuilt, so the destination
  // resolved is the one the next frame will draw. An empty destination is the honest
  // "asked for a flight, has nowhere to land" and is reported as a reject.
  if (fx.traceSeq) {
    const auto dests = swapDests(w, toStrip);
    if (dests.empty())
      debug::trace::animReject("swap", toStrip ? "no-strip-slot" : "no-tile");
    else
      debug::trace::animBegin(fx.traceSeq, "swap", curve.c_str(),
                              swapStyleName(style), ms, curve.c_str(),
                              w->m_class.c_str(), toStrip ? "strip" : "grid", from,
                              dests.front());
  }

  m_fx.addSwap(std::move(fx));
  ensureAnimPump();
  damage();
}

bool Overview::swapfxActive(const PHLWINDOW &w, bool strip) const {
  for (const auto &l : m_fx.swapfx())
    if (l.win.lock() == w && l.toStrip == strip)
      return true;
  return false;
}

// One surface's landing. `strip` says which one, and it picks the style key: a grid card
// and a strip thumbnail are different gestures with their own config.
void Overview::landAfterMove(const PHLWINDOW &w, const LRect &oldBox,
                             double ms, const std::string &curve, bool strip) {
  if (!w)
    return;
  // Every style flies, grid included. This used to branch on
  // gridSwapStyle() != Horizontal and fall through to pinNatural() otherwise, which
  // made the DEFAULT style the one that showed no motion of its own: the user saw the
  // pulse and two cards changing place with nothing travelling between the places.
  //
  // pinNatural() runs for the tile only -- it points the glide source back at where the
  // window was, so the landing is continuous. A thumbnail has no glide to point.
  if (!strip)
    m_scene.pinNatural(w.get(), oldBox);
  beginSwapFX(w, oldBox,
              parseSwapStyle(strip ? anim::stripSwapStyle()
                                   : anim::gridSwapStyle()),
              ms, curve, strip);
}

// Both surfaces, from their own origins.
//
// The one the user DRAGGED leaves the cursor -- that is the whole gesture, and it is the
// only one that may displace. The other is a bystander: it slides from where it already
// was to where the exchange put it. Running both from the cursor made the strip's
// thumbnail fly in from the middle of the screen, which read as a second window rather
// than as this one moving.
void Overview::landAfterSwap(const PHLWINDOW &w, const LRect &fromGrid,
                             const LRect &fromStrip, const LRect &drop, double ms,
                             const std::string &curve, bool draggedOnStrip) {
  const bool dragging = drop.w > 0.0 && drop.h > 0.0;
  // Each surface starts from where IT was. Only the surface the user let go of starts
  // from the cursor: the other one is a bystander sliding from its own old place.
  //
  // Handing the bystander the dragged surface's origin instead is what made a strip drag
  // launch the GRID card from the thumbnail's spot at the bottom of the screen -- it read
  // as the cards flying in from off-screen rather than trading places.
  landAfterMove(w, dragging && !draggedOnStrip ? drop : fromGrid, ms, curve, false);
  if (hasStripThumb(w))
    landAfterMove(w, dragging && draggedOnStrip ? drop : fromStrip, ms, curve, true);
}

LRect Overview::stripThumbRect(const PHLWINDOW &w) const {
  for (size_t i = 0; i < m_strip.size(); ++i) {
    const auto &it = m_strip[i];
    if (it.kind != model::StripItem::Kind::Ws)
      continue;
    for (size_t j = 0; j < it.wins.size(); ++j)
      if (it.wins[j].win.lock() == w)
        return stripWinSlotRect(it, stripCardAt(i), j);
  }
  return {};
}

bool Overview::hasStripThumb(const PHLWINDOW &w) const {
  for (const auto &it : m_strip) {
    if (it.kind != model::StripItem::Kind::Ws)
      continue;
    for (const auto &sw : it.wins)
      if (sw.win.lock() == w)
        return true;
  }
  return false;
}

void Overview::swapOnWorkspace(const PHLWINDOW &w, const model::StripItem &it) {
  if (w && w->m_workspace == it.ws.lock()) {
    damage();
    return;
  }
  if (!w || it.kind == model::StripItem::Kind::Plus || it.kind == model::StripItem::Kind::All) {
    damage();
    return;
  }
  PHLWORKSPACE target = it.ws.lock();
  if (it.virtualWs && !target) {
    dropOnWorkspace(w, it);
    return;
  }
  if (!target || target == w->m_workspace) {
    damage();
    return;
  }

  PHLWINDOW partner = target->m_lastFocusedWindow.lock();
  if (!partner || partner->m_workspace != target || !partner->m_isMapped ||
      partner->isHidden()) {
    partner.reset();
    for (const auto &cand : Desktop::windowState()->windows())
      if (cand && cand->m_isMapped && !cand->isHidden() &&
          cand->m_workspace == target) {
        partner = cand;
        break;
      }
  }
  if (!partner || Fullscreen::controller()->isFullscreen(w) ||
      Fullscreen::controller()->isFullscreen(partner)) {
    dropOnWorkspace(w, it);
    return;
  }

  const auto ta = w->layoutTarget();
  const auto tb = partner->layoutTarget();
  if (!g_layoutManager || !ta || !tb) {
    damage();
    return;
  }

  auto oldBoxes = captureCurrentBoxes();

  g_layoutManager->switchTargets(ta, tb);

  if (cfg::behavior.switch_on_drop != 0) {
    model::StripItem dst;
    dst.ws = target;
    switchToWorkspace(dst);
    return;
  }

  replayReflow(oldBoxes);
  landAfterSwap(w, m_lastDragBox, m_lastDragBox, m_lastDragBox,
                anim::cfg(anim::Id::Drop).ms, anim::cfg(anim::Id::Drop).curve,
                true); // dragged on the strip
  for (const auto &[win, box] : oldBoxes)
    if (win == partner)
      landAfterSwap(partner, box, box, {}, anim::cfg(anim::Id::SwapPartner).ms,
                    anim::cfg(anim::Id::SwapPartner).curve, true);
  damage();
}

void Overview::switchToWorkspace(const model::StripItem &it, bool instant) {
  const auto m = m_monitor.lock();
  if (!m)
    return;

  PHLWORKSPACE ws;
  if (it.kind == model::StripItem::Kind::Plus) {
    int id = 1;
    while (State::workspaceState()->query().id(id).run())
      ++id;
    ws = State::workspaceState()->create(id, m->m_id, "", false);
    if (!ws)
      return;
  } else if (it.virtualWs) {
    ws = State::workspaceState()->query().id(it.id).run();
    if (!ws)
      ws = State::workspaceState()->create(it.id, m->m_id, "", false);
    if (!ws)
      return;
    ws->setPersistent(
        true);
    m_fx.addNewWorkspace(ws);
  } else if (const auto target = it.ws.lock()) {
    ws = target;
    if (ws == m_workspace.lock())
    return;
  } else
    return;

  // Read the PREVIOUS ws BEFORE reassigning: m_view.wsSlideDir() arms from the id order.
  const auto oldWs = m_workspace.lock();
  m_workspace = ws;

  m_scene.clearHover();
  if (oldWs && ws && ws->m_id != oldWs->m_id)
    m_view.setWsSlideDir( ws->m_id > oldWs->m_id ? 1 : -1);
  // The frozen backdrop belongs to the PREVIOUS ws — do NOT invalidate it.
  const auto oldBoxes = captureCurrentBoxes();
  // The strip is the workspace axis and the run is the window axis inside it, so a
  // selection cannot survive the switch: the two tile sets are disjoint, and keeping
  // the INDEX just centres an arbitrary window. Land on the new workspace's most
  // recent one — the window a plain workspace switch would have focused.
  buildTiles();
  // The centred card has to be a window OF the chosen workspace. Measured: in
  // all-workspaces buildTiles does NOT filter by workspace, so nothing left the tile set.
  {
    std::vector<const void *> keys;
    std::vector<int> idx;
    for (size_t i = 0; i < m_scene.size(); ++i) {
      const auto w = m_scene.tiles()[i].win.lock();
      if (w && w->m_workspace == ws) {
        keys.push_back(w.get());
        idx.push_back(static_cast<int>(i));
      }
    }
    if (!keys.empty()) {
      std::vector<std::size_t> recent;
      m_recency.order(keys, recent);
      m_scene.select(idx[recent.front()]);
    }
  }
  buildStrip();
  layoutTiles();
  debug::dbg("SWITCH ->" + std::to_string(ws ? ws->m_id : -1) + " tiles=" +
      std::to_string(m_scene.tiles().size()));
  // instant=true (jump mode) skips the ws-slide so close() cannot collide.
  if (instant) {
    m_view.setWsSlideDir( 0);
    m_view.setJumpMode( true);
    m_fx.clearGhosts();
    m_scene.settleAppear();
    m_clocks.rebuild.pinEnd(std::max(m_entry.ms, m_ghost.ms));
    m_clocks.tiles.pinEnd(glideDur());
    m_progress = 1.0;
    m_life.replayEntry();
    m_clocks.master.pinEnd(animDuration());
    ensureAnimPump();
    damage();
    return;
  }
  m_scene.armAppear();
  m_fx.clearGhosts();
  // Whether every window of the old workspace is still in the tile set. Declared true
  // and never assigned, so the "identical set" branch below always ran and called
  // setWsSlideDir(0): switching by plugin bind or by clicking the strip cleared the
  // slide, and the tiles fell to the scale-in instead of the configured style.
  bool sameSet = !oldBoxes.empty();
  for (const auto &[oldWin, oldBox] : oldBoxes) {
    bool kept = false;
    for (const auto &t : m_scene.tiles())
      if (t.win.lock() == oldWin) { kept = true; break; }
    if (!kept) {
      m_fx.addGhost(model::Ghost{oldWin, oldBox});
      sameSet = false;
    }
  }
  // A ws slide is a CONTENT change; with the set identical it left frame one empty.
  if (sameSet && !oldBoxes.empty()) {
    m_view.setWsSlideDir(0);
    startTileGlide(oldBoxes);
  } else
    m_clocks.rebuild.begin();
  ensureAnimPump();
  if (!sameSet || oldBoxes.empty())
    m_clocks.tiles.pinEnd(glideDur());
  m_progress = 1.0;
  m_life.replayEntry();
  m_clocks.master.pinEnd(animDuration());
  damage();
}

void Overview::stepWorkspace(int dir) {
  if (m_strip.empty())
    return;
  std::vector<int> real;
  int activePos = -1;
  for (size_t i = 0; i < m_strip.size(); ++i) {
    if (m_strip[i].kind == model::StripItem::Kind::Plus || m_strip[i].kind == model::StripItem::Kind::All)
      continue;
    if (m_strip[i].active)
      activePos = static_cast<int>(real.size());
    real.push_back(static_cast<int>(i));
  }
  if (real.empty())
    return;
  if (activePos < 0)
    activePos = 0;
  int next = activePos + (dir > 0 ? 1 : -1);
  if (next < 0)
    next = static_cast<int>(real.size()) - 1;
  else if (next >= static_cast<int>(real.size()))
    next = 0;
  if (next != activePos)
    switchToWorkspace(m_strip[real[next]]);
}

void Overview::addWorkspace() {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  int id = 1;
  if (cfg::behavior.new_workspace_mode.get() == "linear") {
    for (const auto &wref : State::workspaceState()->workspaces())
      if (const auto ws = wref.lock(); ws && !ws->m_isSpecialWorkspace)
        id = std::max(id, static_cast<int>(ws->m_id) + 1);
  } else {
    while (State::workspaceState()->query().id(id).run())
      ++id;
  }
  const auto ws = State::workspaceState()->create(id, m->m_id, "", false);
  if (!ws)
    return;
  // Persistent, or a new empty workspace is reaped within a frame.
  ws->setPersistent(true);
  m_fx.addNewWorkspace(ws);
  m_newCardId = id;
  m_clocks.newCard.begin();
  m_newCardAnim = true;
  debug::dbg("added workspace " + std::to_string(id));
  if (cfg::behavior.switch_on_new_workspace != 0) {
    model::StripItem it;
    it.ws = ws;
    switchToWorkspace(
        it);
  } else {
    m_scene.clearHover();
    buildStrip();
    damage();
  }
}

void Overview::closeTileWindow(int i) {
  if (i < 0 || i >= static_cast<int>(m_scene.tiles().size()))
    return;
  const auto w = m_scene.tiles()[i].win.lock();
  if (!w)
    return;
  debug::dbg("close tile window");
  // sendClose is async; syncTiles() reflows once the window vanishes.
  w->sendClose();
}

void Overview::closeWorkspaceWindows(const model::StripItem &it) {
  if (it.kind == model::StripItem::Kind::Plus || it.kind == model::StripItem::Kind::All)
    return;
  const auto ws = it.ws.lock();
  if (!ws)
    return;
  int n = 0;
  for (const auto &w : Desktop::windowState()->windows())
    if (w && w->m_isMapped && !w->isHidden() && w->m_workspace == ws) {
      w->sendClose();
      ++n;
    }
  debug::dbg("middle-click workspace: closed " + std::to_string(n) + " window(s)");
}

} // namespace gloview

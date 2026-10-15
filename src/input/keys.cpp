#include "../config/config.hpp"
#include "../overview.hpp"
#include "key_combo.hpp"
#include "../debug/log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <utility>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

namespace {
uint32_t modBitForKeycode(int kc) {
  switch (kc) {
  case 42:
  case 54:
    return HL_MODIFIER_SHIFT;
  case 29:
  case 97:
    return HL_MODIFIER_CTRL;
  case 56:
  case 100:
    return HL_MODIFIER_ALT;
  case 125:
  case 126:
    return HL_MODIFIER_META;
  default:
    return 0;
  }
}
} // namespace

uint32_t Overview::altTabModBit() const {
  const std::string modName = cfg::keys.alt_tab_modifier.get();
  if (modName == "super" || modName == "meta" || modName == "win")
    return HL_MODIFIER_META;
  if (modName == "ctrl" || modName == "control")
    return HL_MODIFIER_CTRL;
  if (modName == "shift")
    return HL_MODIFIER_SHIFT;
  return HL_MODIFIER_ALT;
}

void Overview::onKey(const IKeyboard::SKeyEvent &e, bool &cancel) {
  if (!active())
    return;
  const bool passthrough = cfg::behavior.passthrough_keys != 0;

  if (e.state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    // The modifier coming up is swallowed either way — letting it through fires the
    // user's own SUPER binding and tears the state down before the run settles.
    if (m_altTabbing && modBitForKeycode(e.keycode) == altTabModBit()) {
      m_altTabbing = false;
      // A hold commits here; too early to apply means arming it, as a tap does.
      if (cfg::keys.alt_tab_commit_on_release != 0 && !m_commitPending) {
        if (entrySettled())
          activateSelection();
        else
          m_commitPending = true;
      }
      cancel = true;
      return;
    }
    cancel = !passthrough;
    return;
  }

  const int k = e.keycode;
  // Held mods minus the pressed key's own bit, or a bare modifier name never matches.
  uint32_t mods = g_pInputManager ? g_pInputManager->getModsFromAllKBs() : 0;
  mods &= ~modBitForKeycode(k);
  bool handled = true;
  if (keyMatches(k, mods, cfg::keys.close.get()))
    close();
  else if (keyMatches(k, mods, cfg::keys.next_workspace.get()))
    stepWorkspace(
        1);
  else if (keyMatches(k, mods, cfg::keys.prev_workspace.get()))
    stepWorkspace(-1);
  else if (keyMatches(k, mods, cfg::keys.activate.get()))
    activateSelection();
  else if (keyMatches(k, mods, cfg::keys.close_window.get()))
    closeTileWindow(m_scene.focused());
  else if (keyMatches(k, mods, cfg::keys.left.get()))
    moveSelection(-1, 0);
  else if (keyMatches(k, mods, cfg::keys.right.get()))
    moveSelection(1, 0);
  else if (keyMatches(k, mods, cfg::keys.up.get()))
    moveSelection(0, -1);
  else if (keyMatches(k, mods, cfg::keys.down.get()))
    moveSelection(0, 1);
  else if (keyMatches(k, mods, cfg::keys.desktop.get()))
    setDesktopMode(!m_view.desktop());
  else if (keyMatches(k, mods, cfg::keys.all_workspaces.get()))
    toggleAllWorkspaces();
  else if (keyMatches(k, mods, cfg::keys.carousel.get()))
    toggleCarousel();
  // Ctrl masked out: bare digit and Ctrl+digit both land here.
  else if (const int idx =
               keyIndex(k, mods & ~HL_MODIFIER_CTRL, cfg::keys.workspace.get());
           idx >= 0) {
    const int id = idx + 1;
    const auto m = m_monitor.lock();
    auto ws = State::workspaceState()->query().id(id).run();
    const bool justCreated = !ws && m;
    if (justCreated)
      ws = State::workspaceState()->create(id, m->m_id, "", false);
    if (ws) {
      if (justCreated) {
        // Persistent, or a new empty workspace is reaped within a frame.
        ws->setPersistent(true);
        m_fx.addNewWorkspace(ws);
        m_newCardId =
            id;
        m_place.clocks.newCard.begin();
  debug::trace::animStart("new_card", "card", "", m_place.recipes.entry.ms, m_place.recipes.entry.curve.c_str());
        m_newCardAnim = true;
      }
      model::StripItem it;
      it.ws = ws;
      const bool jumpMode = cfg::behavior.workspace_key_mode.get() == "jump";
      const bool ctrlHeld = (mods & HL_MODIFIER_CTRL) != 0;
      const bool doJump   = jumpMode && !ctrlHeld;
      const bool instant  = doJump && anim::jumpStyle() == "instant";
      switchToWorkspace(it, instant);
      if (m) {
        if (const auto target = m_workspace.lock();
            target && target != m->m_activeWorkspace) {
          // internal=true is not the lever (it stopped a fullscreen window on
          // the target from rendering); keep it false and warp the slide away below.
          const auto oldWs = m->m_activeWorkspace; // capture BEFORE the switch
          m->changeWorkspace(target, false, true, false);
          if (oldWs && oldWs != target) {
            oldWs->m_alpha->setValueAndWarp(oldWs->m_alpha->goal());
            oldWs->m_renderOffset->setValueAndWarp(oldWs->m_renderOffset->goal());
          }
          target->m_alpha->setValueAndWarp(target->m_alpha->goal());
          target->m_renderOffset->setValueAndWarp(target->m_renderOffset->goal());
          m_liveWsAtOpen = m->m_activeWorkspace;
        }
      }
      // "switch" stays open; "jump" closes now, or when the ws-slide settles.
      if (doJump) {
        if (instant)
          close();
        else
          m_life.setJumpClosing();
      }
    }
  } else
    handled = false;
  cancel = handled || !passthrough;
}

// The bits key_combo.hpp declares for itself must be the bits Hyprland puts in the event.
// A move here would make every combo using that modifier stop matching, and it would look
// like a config error.
static_assert(gloview::input::kModShift == HL_MODIFIER_SHIFT);
static_assert(gloview::input::kModCtrl == HL_MODIFIER_CTRL);
static_assert(gloview::input::kModAlt == HL_MODIFIER_ALT);
static_assert(gloview::input::kModMeta == HL_MODIFIER_META);


bool Overview::keyMatches(int keycode, uint32_t mods,
                          const std::string &combo) const {
  return input::keyMatches(keycode, mods, combo);
}

int Overview::keyIndex(int keycode, uint32_t mods,
                       const std::string &combo) const {
  return input::keyIndex(keycode, mods, combo);
}

void Overview::moveSelection(int dx, int dy) {
  if (m_scene.tiles().empty())
    return;
  if (!m_scene.hasSelection()) {
    m_scene.select((m_scene.hoveredTile() >= 0) ? m_scene.hoveredTile() : 0);
    damage();
    return;
  }
  // Carousel: dx is a circular index step, dy is unused; relayoutCarousel()
  // reacts to m_scene.selected().
  if (carouselMode()) {
    if (dx != 0) {
      m_scene.stepSelection(dx);
      damage();
    }
    return;
  }
  const LRect cur = currentBox(m_scene.tiles()[m_scene.selected()], m_scene.selected());
  const double cx = cur.cx(), cy = cur.cy();
  int best = -1;
  double bestScore = 1e18;
  for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
    if (static_cast<int>(i) == m_scene.selected())
      continue;
    const LRect b = currentBox(m_scene.tiles()[i], static_cast<int>(i));
    const double ddx = b.cx() - cx;
    const double ddy = b.cy() - cy;
    const double along =
        dx * ddx + dy * ddy;
    if (along <= 1.0)
      continue;
    const double perp = std::abs(dx * ddy - dy * ddx);
    const double score =
        along + perp * 2.0;
    if (score < bestScore) {
      bestScore = score;
      best = static_cast<int>(i);
    }
  }
  if (best >= 0) {
    m_scene.select(best);
    damage();
  }
}

// Steps through the order the user SEES. In the run that is the slot order, not the
// scene's array order: the cards are laid out most-recently-used first, so stepping
// the array index would move the selection to a window that is not the next card on
// screen. A grid has no slot order and keeps the array order it is drawn in.
void Overview::stepAltTab(int dir) {
  const int n = static_cast<int>(m_scene.size());
  if (n == 0)
    return;
  const auto &slot = m_view.slot();
  if (carouselMode() && m_scene.hasSelection() &&
      slot.size() == static_cast<std::size_t>(n)) {
    const int cur = m_view.slotOf(static_cast<std::size_t>(m_scene.selected()));
    if (cur >= 0) {
      const int nxt = ((cur + dir) % n + n) % n;
      m_scene.select(static_cast<int>(slot[static_cast<std::size_t>(nxt)]));
      damage();
      return;
    }
  }
  m_scene.stepSelection(dir);
  damage();
}

void Overview::activateSelection() {
  PHLWINDOW w;
  // focused(), not selected(): the card the user can SEE centred is the one Enter must
  // commit to. Asking the raw index got -1 whenever the deck was rebuilt onto a selection
  // the overview never had -- an empty workspace switched to all-workspaces -- so Enter
  // activated nothing and the exit became a bare close.
  if (const int f = m_scene.focused(); f >= 0)
    w = m_scene.tiles()[static_cast<size_t>(f)].win.lock();
  focusAndClose(w, Desktop::FOCUS_REASON_KEYBIND);
}


} // namespace gloview

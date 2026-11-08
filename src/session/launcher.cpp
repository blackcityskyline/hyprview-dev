#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../input/key_combo.hpp"
#include "../overview.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <linux/input-event-codes.h>

#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

namespace hyprview {

namespace {

char launcherChar(int k, bool shift) {
  if (k == KEY_SPACE)
    return ' ';
  if (k >= KEY_1 && k <= KEY_0) {
    static constexpr char kPlain[] = "1234567890";
    static constexpr char kShift[] = "!@#$%^&*()";
    const int i = k - KEY_1;
    if (i >= 0 && i < 10)
      return shift ? kShift[i] : kPlain[i];
  }
  switch (k) {
  case KEY_MINUS:      return shift ? '_' : '-';
  case KEY_EQUAL:      return shift ? '+' : '=';
  case KEY_LEFTBRACE:  return shift ? '{' : '[';
  case KEY_RIGHTBRACE: return shift ? '}' : ']';
  case KEY_SEMICOLON:  return shift ? ':' : ';';
  case KEY_APOSTROPHE: return shift ? '"' : '\'';
  case KEY_COMMA:      return shift ? '<' : ',';
  case KEY_DOT:        return shift ? '>' : '.';
  case KEY_SLASH:      return shift ? '?' : '/';
  case KEY_GRAVE:      return shift ? '~' : '`';
  case KEY_BACKSLASH:  return shift ? '|' : '\\';
  default:
    break;
  }
  for (char c = 'a'; c <= 'z'; ++c) {
    const auto codes = input::keyNameToCodes(std::string(1, c));
    if (std::find(codes.begin(), codes.end(), k) != codes.end())
      return shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
  }
  return '\0';
}

}

void Overview::toggleLauncher() {
  if (m_launcher.active())
    closeLauncher();
  else
    openLauncher();
}

void Overview::openLauncher() {
  if (m_launcher.active())
    return;

  if (!openedOrParked()) {
    m_launcherOpenedOverview = true;
    m_launcherPrevAll = m_view.allOverride();
    m_launcherForcedAll = !showAllWorkspaces();
    m_view.setAllOverride(1);
    open();
    if (!openedOrParked()) {
      m_view.setAllOverride(m_launcherPrevAll);
      m_launcherForcedAll = false;
      m_launcherOpenedOverview = false;
      return;
    }
  } else if (!showAllWorkspaces()) {
    m_launcherPrevAll = m_view.allOverride();
    m_launcherForcedAll = true;
    setAllWorkspaces(true);
  }

  const bool push = cfg::launcher.mode.get() == "push";
  m_launcher.open(push ? model::LauncherMode::Push : model::LauncherMode::Overlay);
  buildLauncherItems();
  syncLauncherSelection();

  if (push) {
    auto oldBoxes = captureCurrentBoxes();
    layoutTiles();
    startTileGlide(oldBoxes);
  }

  debug::dbg("LAUNCH|open|mode=" + std::string(push ? "push" : "overlay") +
             "|items=" + std::to_string(m_launcher.items().size()));
  damage();
}

void Overview::closeLauncher() {
  if (!m_launcher.active())
    return;
  const bool closeOverview = m_launcherOpenedOverview;
  const bool push = m_launcher.mode() == model::LauncherMode::Push;
  m_launcher.close();
  m_launcherOpenedOverview = false;

  if (m_launcherForcedAll) {
    m_launcherForcedAll = false;
    if (openedOrParked() && showAllWorkspaces())
      setAllWorkspaces(false);
    m_view.setAllOverride(m_launcherPrevAll);
  }

  if (closeOverview) {
    close();
    return;
  }
  if (push && openedOrParked()) {
    auto oldBoxes = captureCurrentBoxes();
    layoutTiles();
    startTileGlide(oldBoxes);
  }
  debug::dbg("LAUNCH|close");
  damage();
}

void Overview::buildLauncherItems() {
  std::vector<model::LauncherItem> items;
  items.reserve(m_scene.size() + 5);

  for (const auto &t : m_scene.tiles()) {
    model::LauncherItem item;
    item.subj = t.subj;
    if (cardOf(t).isTab()) {
      item.kind = model::LauncherKind::Tab;
      item.tab = static_cast<int>(t.subj.id);
      item.title = m_tabs.label(t.subj);
      if (item.title.empty())
        item.title = t.labelText;
      if (const auto host = ownerWindow(t))
        item.detail = host->m_class;
    } else {
      const auto w = tileWindow(t);
      if (!w)
        continue;
      item.kind = model::LauncherKind::Window;
      item.title = w->m_title.empty() ? w->m_class : w->m_title;
      item.detail = w->m_class;
    }
    if (item.title.empty())
      item.title = "untitled";
    items.push_back(std::move(item));
  }

  constexpr model::LauncherVerb kViews[] = {
      model::LauncherVerb::ViewAll,   model::LauncherVerb::ViewOne,
      model::LauncherVerb::ViewCarousel, model::LauncherVerb::ViewBrowser,
      model::LauncherVerb::ViewDesktop};
  for (const auto verb : kViews) {
    model::LauncherItem action;
    action.kind = model::LauncherKind::Action;
    action.verb = verb;
    action.title = model::verbLabel(verb);
    action.detail = "view";
    items.push_back(std::move(action));
  }

  m_launcher.setItems(std::move(items));
}

void Overview::syncLauncherSelection() {
  const auto *item = m_launcher.selectedItem();
  if (!item)
    return;
  const int idx = m_scene.tileIndexOf(item->subj);
  if (idx < 0)
    return;
  m_scene.select(idx);
  followGrid(idx);
  damage();
}

void Overview::launcherCommit() {
  if (m_launcher.menuOpen()) {
    runLauncherVerb(m_launcher.menuVerb());
    return;
  }
  const auto *item = m_launcher.selectedItem();
  if (!item)
    return;
  runLauncherVerb(item->kind == model::LauncherKind::Action
                      ? item->verb
                      : model::LauncherVerb::FocusWindow);
}

void Overview::runLauncherVerb(model::LauncherVerb verb) {
  const auto *item = m_launcher.selectedItem();
  if (!item || verb == model::LauncherVerb::Count)
    return;

  const auto subj = item->subj;
  const int tab = item->tab;
  const int idx = m_scene.tileIndexOf(subj);
  const model::Tile *tile = idx >= 0 ? &m_scene.tiles()[static_cast<std::size_t>(idx)] : nullptr;
  const auto win = tile ? ownerWindow(*tile) : PHLWINDOW{};

  debug::dbg("LAUNCH|verb|" + std::string(model::verbLabel(verb)));

  switch (verb) {
  case model::LauncherVerb::FocusWindow: {
    m_launcherOpenedOverview = false;
    if (idx >= 0) {
      m_scene.select(idx);
      followGrid(idx);
    }
    closeLauncher();
    return;
  }
  case model::LauncherVerb::FocusWorkspace: {
    m_launcherOpenedOverview = false;
    if (tile) {
      model::StripItem it;
      it.kind = model::StripItem::Kind::Ws;
      it.id = tile->workspace;
      it.ws = State::workspaceState()->query().id(it.id).run();
      if (it.ws)
        switchToWorkspace(it);
    }
    closeLauncher();
    return;
  }
  case model::LauncherVerb::CloseWindow:
    if (win)
      win->sendClose();
    break;
  case model::LauncherVerb::KillWindow:
    if (win) {
      if (const auto killed = Config::Actions::killWindow(win); !killed)
        debug::dbg("LAUNCH|verb|kill failed");
    }
    break;
  case model::LauncherVerb::CloseTab: {
    const auto group = m_tabs.groupOf(subj);
    if (group.id)
      writeTabClose(group.id, tab);
    break;
  }
  case model::LauncherVerb::UnloadTab: {
    const auto group = m_tabs.groupOf(subj);
    if (group.id)
      writeTabUnload(group.id, tab);
    break;
  }
  case model::LauncherVerb::ViewAll:
  case model::LauncherVerb::ViewOne:
  case model::LauncherVerb::ViewCarousel:
  case model::LauncherVerb::ViewBrowser:
  case model::LauncherVerb::ViewDesktop: {
    m_launcherOpenedOverview = false;
    m_launcherForcedAll = false;
    if (verb == model::LauncherVerb::ViewAll)
      setAllWorkspaces(true);
    else if (verb == model::LauncherVerb::ViewOne)
      setAllWorkspaces(false);
    else if (verb == model::LauncherVerb::ViewCarousel)
      toggleCarousel();
    else if (verb == model::LauncherVerb::ViewBrowser)
      toggleBrowserView();
    else
      setDesktopMode(!m_view.desktop());
    closeLauncher();
    return;
  }
  default:
    return;
  }

  const int sel = m_launcher.selection();
  buildLauncherItems();
  m_launcher.setSelection(sel);
  damage();
}

bool Overview::launcherKey(const IKeyboard::SKeyEvent &e, int k, uint32_t mods) {
  if (!m_launcher.active())
    return false;

  if (keyMatches(k, mods, cfg::keys.close.get())) {
    closeLauncher();
    return true;
  }
  if (keyMatches(k, mods, cfg::keys.launcher_menu.get())) {
    m_launcher.toggleMenu();
    damage();
    return true;
  }

  if ((mods & (HL_MODIFIER_CTRL | HL_MODIFIER_ALT | HL_MODIFIER_META)) != 0)
    return true;

  if (k == KEY_ENTER || k == KEY_KPENTER) {
    launcherCommit();
    return true;
  }
  if (keyMatches(k, mods, cfg::keys.up.get())) {
    m_launcher.stepAny(-1);
    syncLauncherSelection();
    return true;
  }
  if (keyMatches(k, mods, cfg::keys.down.get())) {
    m_launcher.stepAny(1);
    syncLauncherSelection();
    return true;
  }
  if (k == KEY_BACKSPACE) {
    m_launcher.backspace();
    syncLauncherSelection();
    return true;
  }
  if (k == KEY_DELETE) {
    m_launcher.setQuery({});
    syncLauncherSelection();
    return true;
  }
  if (const char c = launcherChar(k, (mods & HL_MODIFIER_SHIFT) != 0)) {
    m_launcher.appendChar(c);
    syncLauncherSelection();
    return true;
  }
  return true;
}

bool Overview::launcherMouse(const IPointer::SButtonEvent &e) {
  if (!m_launcher.active())
    return false;
  if (e.state != WL_POINTER_BUTTON_STATE_PRESSED)
    return true;

  const auto m = m_monitor.lock();
  if (!m)
    return true;
  const auto mc = g_pInputManager->getMouseCoordsInternal();
  const double lx = mc.x - m->m_position.x;
  const double ly = mc.y - m->m_position.y;
  const auto l = launcherLayoutNow();
  if (!l.fit)
    return true;

  const auto hitAt = [&](int visible) {
    return render::launcherScroll(m_launcher.selection(),
                                  static_cast<int>(m_launcher.hits().size()),
                                  static_cast<int>(l.rows.size())) +
           visible;
  };

  if (m_launcher.menuOpen()) {
    const auto &acts = m_launcher.menuActions();
    const auto menu = render::launcherMenuAt(l, m_launcher.selection(),
                                             static_cast<int>(acts.size()),
                                             cfg::launcher.width.get() / 3.0,
                                             cfg::launcher.row_height.get());
    for (std::size_t i = 0; i < menu.rows.size(); ++i)
      if (menu.rows[i].contains(lx, ly)) {
        m_launcher.setMenuSelection(static_cast<int>(i));
        runLauncherVerb(m_launcher.menuVerb());
        return true;
      }
    if (!menu.panel.contains(lx, ly))
      m_launcher.closeMenu();
    damage();
    return true;
  }

  const int visible = render::launcherRowAt(l, lx, ly);
  if (visible >= 0) {
    const int hit = hitAt(visible);
    if (e.button == BTN_RIGHT) {
      m_launcher.setSelection(hit);
      syncLauncherSelection();
      m_launcher.toggleMenu();
    } else if (hit == m_launcher.selection()) {
      launcherCommit();
    } else {
      m_launcher.setSelection(hit);
      syncLauncherSelection();
    }
    damage();
    return true;
  }

  if (!l.panel.contains(lx, ly))
    closeLauncher();
  return true;
}

render::LauncherMetrics Overview::launcherMetrics() const {
  render::LauncherMetrics mt;
  mt.width = cfg::launcher.width.get();
  mt.rows = cfg::launcher.rows.get();
  mt.rowH = cfg::launcher.row_height.get();
  return mt;
}

render::LauncherLayout Overview::launcherLayoutNow() const {
  return render::launcherLayout(gridArea(), launcherMetrics());
}

}

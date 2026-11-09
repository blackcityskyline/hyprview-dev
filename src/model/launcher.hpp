#pragma once

#include "fuzzy.hpp"
#include "subject.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hyprview::model {

enum class LauncherKind : int { Window, Tab, App, Action };
enum class LauncherMode : int { Overlay, Push };

enum class LauncherVerb : int {
  FocusWindow,
  CloseWindow,
  KillWindow,
  CloseTab,
  UnloadTab,
  LaunchApp,
  FocusWorkspace,
  ViewAll,
  ViewOne,
  ViewCarousel,
  ViewBrowser,
  ViewDesktop,
  Count
};

inline constexpr std::size_t kLauncherVerbCount = static_cast<std::size_t>(LauncherVerb::Count);

inline constexpr const char *const kLauncherVerbLabels[kLauncherVerbCount] = {
    "focus window", "close window", "kill window", "close tab", "unload tab",
    "launch app",   "focus workspace", "all workspaces", "one workspace", "carousel",
    "browser tabs", "desktop"};

[[nodiscard]] inline const char *verbLabel(LauncherVerb v) {
  const auto i = static_cast<std::size_t>(v);
  return i < kLauncherVerbCount ? kLauncherVerbLabels[i] : "";
}

struct LauncherItem {
  LauncherKind kind = LauncherKind::Window;
  SubjectRef   subj;
  std::string  title;
  std::string  detail;
  std::string  exec;
  std::string  icon;
  LauncherVerb verb = LauncherVerb::FocusWindow;
  int          tab = -1;
};

struct LauncherHit {
  std::size_t item = 0;
  int         score = 0;
};

[[nodiscard]] inline std::vector<LauncherHit> rank(const std::vector<LauncherItem> &items,
                                                   std::string_view query) {
  std::vector<LauncherHit> out;
  out.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    const std::string_view fields[] = {items[i].title, items[i].detail};
    const int s = fuzzyScoreAny(query, fields, 2);
    if (s >= 0)
      out.push_back(LauncherHit{i, s});
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const LauncherHit &a, const LauncherHit &b) { return a.score > b.score; });
  return out;
}

[[nodiscard]] inline std::vector<LauncherVerb> verbsFor(LauncherKind kind) {
  switch (kind) {
    case LauncherKind::Tab:
      return {LauncherVerb::CloseTab, LauncherVerb::UnloadTab, LauncherVerb::CloseWindow,
              LauncherVerb::KillWindow, LauncherVerb::FocusWorkspace,
              LauncherVerb::FocusWindow};
    case LauncherKind::Action:
    case LauncherKind::App:
      return {};
    case LauncherKind::Window:
    default:
      return {LauncherVerb::CloseWindow, LauncherVerb::KillWindow,
              LauncherVerb::FocusWorkspace, LauncherVerb::FocusWindow};
  }
}

class Launcher {
  public:
    void open(LauncherMode mode) {
      m_open = true;
      m_mode = mode;
      m_query.clear();
      m_selection = 0;
      m_menuOpen = false;
      m_menuAt = 0;
      rebuild();
    }
    void close() {
      m_open = false;
      m_menuOpen = false;
    }
    [[nodiscard]] bool active() const { return m_open; }
    [[nodiscard]] LauncherMode mode() const { return m_mode; }
    void setMode(LauncherMode m) { m_mode = m; }

    void setItems(std::vector<LauncherItem> items) {
      m_items = std::move(items);
      rebuild();
    }
    [[nodiscard]] const std::vector<LauncherItem> &items() const { return m_items; }

    [[nodiscard]] const std::string &query() const { return m_query; }
    void setQuery(std::string q) {
      if (q == m_query)
        return;
      m_query = std::move(q);
      rebuild();
    }
    void appendChar(char c) { setQuery(m_query + c); }
    void backspace() {
      if (!m_query.empty())
        setQuery(m_query.substr(0, m_query.size() - 1));
    }

    [[nodiscard]] const std::vector<LauncherHit> &hits() const { return m_hits; }
    [[nodiscard]] const LauncherItem *itemAt(std::size_t hit) const {
      if (hit >= m_hits.size() || m_hits[hit].item >= m_items.size())
        return nullptr;
      return &m_items[m_hits[hit].item];
    }
    [[nodiscard]] const LauncherItem *selectedItem() const {
      return m_hits.empty() ? nullptr : itemAt(static_cast<std::size_t>(m_selection));
    }

    [[nodiscard]] int selection() const { return m_selection; }
    void setSelection(int i) {
      const int n = static_cast<int>(m_hits.size());
      m_selection = n == 0 ? 0 : std::clamp(i, 0, n - 1);
      closeMenu();
    }
    void step(int dir) {
      const int n = static_cast<int>(m_hits.size());
      if (n == 0)
        return;
      m_selection = ((m_selection + dir) % n + n) % n;
      closeMenu();
    }

    [[nodiscard]] bool menuOpen() const { return m_menuOpen && selectedItem(); }
    void toggleMenu() {
      const auto *item = selectedItem();
      if (!item || verbsFor(item->kind).empty())
        return;
      m_menuOpen = !m_menuOpen;
      m_menuAt = 0;
    }
    void closeMenu() { m_menuOpen = false; }
    [[nodiscard]] int menuSelection() const { return m_menuAt; }
    void setMenuSelection(int i) {
      const int n = static_cast<int>(menuActions().size());
      m_menuAt = n == 0 ? 0 : std::clamp(i, 0, n - 1);
    }
    [[nodiscard]] const std::vector<LauncherVerb> &menuActions() const {
      static const std::vector<LauncherVerb> NONE;
      const auto *item = selectedItem();
      if (!item)
        return NONE;
      m_actions = verbsFor(item->kind);
      return m_actions;
    }
    // The open menu owns the arrows; with it closed the rows do.
    void stepAny(int dir) {
      if (m_menuOpen)
        stepMenu(dir);
      else
        step(dir);
    }

    void stepMenu(int dir) {
      const auto &acts = menuActions();
      const int n = static_cast<int>(acts.size());
      if (n == 0)
        return;
      m_menuAt = ((m_menuAt + dir) % n + n) % n;
    }
    [[nodiscard]] LauncherVerb menuVerb() const {
      const auto &acts = menuActions();
      const auto i = static_cast<std::size_t>(m_menuAt);
      return i < acts.size() ? acts[i] : LauncherVerb::Count;
    }

  private:
    void rebuild() {
      m_hits = rank(m_items, m_query);
      m_selection = 0;
      m_menuOpen = false;
      m_menuAt = 0;
    }

    std::vector<LauncherItem> m_items;
    std::vector<LauncherHit>  m_hits;
    std::string               m_query;
    int                       m_selection = 0;
    bool                      m_open = false;
    bool                      m_menuOpen = false;
    int                       m_menuAt = 0;
    LauncherMode              m_mode = LauncherMode::Overlay;
    mutable std::vector<LauncherVerb> m_actions;
};

}

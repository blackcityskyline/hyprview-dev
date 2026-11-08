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

// What a row points at. The launcher searches these three domains, and an action row is the
// plugin's own verb rather than a window: one list, one selection, one Enter.
enum class LauncherKind : int { Window, Tab, Action };

// How the launcher meets the grid: displacing it or painting over it.
enum class LauncherMode : int { Overlay, Push };

// Every verb the launcher can run. Layout comes from the caller; a verb here is only a name.
enum class LauncherVerb : int {
  FocusWindow,
  CloseWindow,
  KillWindow,
  CloseTab,
  UnloadTab,
  FocusWorkspace,
  ViewAll,
  ViewOne,
  ViewCarousel,
  ViewBrowser,
  ViewDesktop,
  Count
};

inline constexpr std::size_t kLauncherVerbCount = static_cast<std::size_t>(LauncherVerb::Count);

// Keyed by verb, sized by the enum: a verb added without a row does not compile.
inline constexpr const char *const kLauncherVerbLabels[kLauncherVerbCount] = {
    "focus window", "close window", "kill window", "close tab", "unload tab",
    "focus workspace", "all workspaces", "one workspace", "carousel",
    "browser tabs", "desktop"};

[[nodiscard]] inline const char *verbLabel(LauncherVerb v) {
  const auto i = static_cast<std::size_t>(v);
  return i < kLauncherVerbCount ? kLauncherVerbLabels[i] : "";
}

struct LauncherItem {
  LauncherKind kind = LauncherKind::Window;
  SubjectRef   subj;
  std::string  title;
  std::string  detail; // class, browser, or a verb's hint: searched, shown dimmer
  LauncherVerb verb = LauncherVerb::FocusWindow;
  int          tab = -1; // browser tab id for Kind::Tab, -1 otherwise
};

// A ranked row: the item's index in the caller's list, and why it scored.
struct LauncherHit {
  std::size_t item = 0;
  int         score = 0;
};

// Score desc, then the caller's order. The list arrives already ordered; a stable tie keeps
// that order, because recency is not the launcher's to know.
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

// The verbs a row offers. A tab has no window of its own, so its close/kill pair names the
// host window; a window has no tab, so the tab pair is absent, not disabled.
[[nodiscard]] inline std::vector<LauncherVerb> verbsFor(LauncherKind kind) {
  switch (kind) {
    case LauncherKind::Tab:
      return {LauncherVerb::CloseTab, LauncherVerb::UnloadTab, LauncherVerb::CloseWindow,
              LauncherVerb::KillWindow, LauncherVerb::FocusWorkspace,
              LauncherVerb::FocusWindow};
    case LauncherKind::Action:
      return {};
    case LauncherKind::Window:
    default:
      return {LauncherVerb::CloseWindow, LauncherVerb::KillWindow,
              LauncherVerb::FocusWorkspace, LauncherVerb::FocusWindow};
  }
}

// The panel's whole state. Search is recomputed on the two events that can change it -- new
// items, new query -- never per frame: ranking a few hundred rows per frame is waste.
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

    // Ranked hits, and the item a hit names. Both are empty/-1 when nothing matches.
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
    // Wraps: the last row's Down is the first row, so a short list stays reachable.
    void step(int dir) {
      const int n = static_cast<int>(m_hits.size());
      if (n == 0)
        return;
      m_selection = ((m_selection + dir) % n + n) % n;
      closeMenu();
    }

    // A menu on a row that is not there is a menu on nothing; a row with no verbs has no menu.
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
    [[nodiscard]] const std::vector<LauncherVerb> &menuActions() const {
      static const std::vector<LauncherVerb> NONE;
      const auto *item = selectedItem();
      if (!item)
        return NONE;
      m_actions = verbsFor(item->kind);
      return m_actions;
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
    mutable std::vector<LauncherVerb> m_actions; // painted per frame, not stored per row
};

} // namespace hyprview::model

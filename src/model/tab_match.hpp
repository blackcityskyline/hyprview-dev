#pragma once

// Which Hyprland window is which browser window. The two sides cannot name each other: the
// extension knows browser.windows ids and tab titles, the plugin knows CWindow and its title,
// and no id crosses in either direction. What both have is the active tab's title -- a browser
// window's title IS its active tab's title, in Gecko and Chromium alike.
//
// The rules are built around being wrong safely. EXACT, not fuzzy: a prefix match would be
// guessing, and a browser window guessed into the wrong window is a set of tabs filed under the
// wrong program. ONE-TO-ONE: two browser windows claiming the same Hyprland window is not a
// match, it is a conflict. AMBIGUOUS means unmatched: two windows with the same title, and
// neither is matched, because guessing there is the one failure a user cannot detect by
// looking. Unmatched is a supported outcome -- the window then contributes nothing in Tabs
// mode, exactly as a window nobody has heard from does (ReportedTabs::known).

#include "tab_feed.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gloview::model {

// A window the plugin can put a browser window into.
struct HostWindow {
    // Opaque identity of the compositor window; the caller resolves it.
    std::uint64_t id = 0;
    // The OS process it belongs to -- the match, where a title was not. A browser appends its
    // own name to a window's title, so the two sides hold different strings by construction.
    // See TabSnapshot::pids.
    std::uint64_t pid = 0;
};

// Browser window id -> index into `hosts`, for those that matched. `pids` is keyed by browser
// window id and a window absent from it has no process and matches nothing.
//
// Two windows of one browser share a process, so "is this host the right browser?" is a SET,
// not a single answer -- that is the whole shape of this function, and why matching by title
// had to go: two windows of one browser differ in title and agree in process, the opposite of
// what the old rule assumed.
[[nodiscard]] inline std::unordered_map<std::uint64_t, std::size_t> matchBrowserWindows(
    const std::vector<HostWindow> &hosts,
    const std::unordered_map<std::uint64_t, std::uint64_t> &pids) {

    // Host indices per process. Every index is kept, never just the first, because picking one
    // out of several would be a coin flip dressed up as a rule.
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> byPid;
    for (std::size_t i = 0; i < hosts.size(); ++i)
        if (hosts[i].pid != 0)
            byPid[hosts[i].pid].push_back(i);

    std::unordered_map<std::uint64_t, std::size_t> out;
    for (const auto &[browserWindow, pid] : pids) {
        const auto it = byPid.find(pid);
        if (it == byPid.end())
            continue; // that browser has no window on this run
        const auto &cands = it->second;
        if (cands.size() != 1)
            continue; // two windows of this browser are on the run and nothing says which
        out.emplace(browserWindow, cands.front());
    }
    return out;
}

// A map from a compositor window to the browser window whose tabs it shows. A type rather than
// a bare map because it has a lifetime: built when a run opens, dropped when it ends, and
// "dropped" is the part that has to be right -- holding it for the life of the plugin would
// let a process that matched during one run decide the next. Both directions, because
// answering one by scanning for the other is a second place that could disagree.
class BrowserLinks {
  public:
    void rebuild(const std::vector<HostWindow> &hosts,
                 const std::unordered_map<std::uint64_t, std::uint64_t> &pids) {
      // Replaced, never accumulated: the next run must not inherit the last run's answers.
      m_byHost.clear();
      m_byBrowser.clear();
      for (const auto &[browserWindow, host] :
           matchBrowserWindows(hosts, pids)) {
        m_byHost.emplace(hosts[host].id, browserWindow);
        m_byBrowser.emplace(browserWindow, hosts[host].id);
      }
    }

    // The browser window whose tabs this one carries, or 0 -- which is never a browser window
    // id, so "none" needs no separate spelling and no optional.
    [[nodiscard]] std::uint64_t browserOf(std::uint64_t host) const {
      const auto it = m_byHost.find(host);
      return it == m_byHost.end() ? 0 : it->second;
    }

    [[nodiscard]] std::uint64_t hostOf(std::uint64_t browserWindow) const {
      const auto it = m_byBrowser.find(browserWindow);
      return it == m_byBrowser.end() ? 0 : it->second;
    }

    [[nodiscard]] bool empty() const { return m_byHost.empty(); }
    void clear() {
      m_byHost.clear();
      m_byBrowser.clear();
    }

  private:
    std::unordered_map<std::uint64_t, std::uint64_t> m_byHost;
    std::unordered_map<std::uint64_t, std::uint64_t> m_byBrowser;
};

} // namespace gloview::model
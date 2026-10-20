#pragma once

// Which Hyprland window is which browser window: the two sides cannot name each other, so the
// match runs on the OS process a browser window belongs to (TabSnapshot::pids). ONE-TO-ONE --
// two candidates is not a match but a conflict, and a guess is the one failure a user cannot
// detect by looking. Unmatched is supported: that window then contributes nothing (AGENTS.md 2.19).

#include "tab_feed.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyprview::model {

// A window the plugin can put a browser window into.
struct HostWindow {
    std::uint64_t id = 0;
    // The match -- never a title: the browser appends its own name (AGENTS.md 2.26).
    std::uint64_t pid = 0;
};

// Browser window id -> index into `hosts`, for those that matched; absent from `pids` means no
// process and no match. Two windows of one browser share a process, so "which host is the right
// browser" is a SET, not a single answer.
[[nodiscard]] inline std::unordered_map<std::uint64_t, std::size_t> matchBrowserWindows(
    const std::vector<HostWindow> &hosts,
    const std::unordered_map<std::uint64_t, std::uint64_t> &pids) {

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> byPid;
    for (std::size_t i = 0; i < hosts.size(); ++i)
        if (hosts[i].pid != 0)
            byPid[hosts[i].pid].push_back(i);

    std::unordered_map<std::uint64_t, std::size_t> out;
    for (const auto &[browserWindow, pid] : pids) {
        const auto it = byPid.find(pid);
        if (it == byPid.end())
            continue;
        const auto &cands = it->second;
        if (cands.size() != 1)
            continue;
        out.emplace(browserWindow, cands.front());
    }
    return out;
}

// A map from a compositor window to the browser window whose tabs it shows. A type rather than
// a bare map because it has a lifetime: built when a run opens, dropped when it ends. Both
// directions, because answering one by scanning for the other is a second place to disagree.
class BrowserLinks {
  public:
    void rebuild(const std::vector<HostWindow> &hosts,
                 const std::unordered_map<std::uint64_t, std::uint64_t> &pids) {
      m_byHost.clear();
      m_byBrowser.clear();
      for (const auto &[browserWindow, host] :
           matchBrowserWindows(hosts, pids)) {
        m_byHost.emplace(hosts[host].id, browserWindow);
        m_byBrowser.emplace(browserWindow, hosts[host].id);
      }
    }

    // 0 is never a browser window id, so "none" needs no separate spelling and no optional.
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

} // namespace hyprview::model

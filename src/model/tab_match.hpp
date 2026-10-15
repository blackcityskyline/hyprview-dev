#pragma once

// Which Hyprland window is which browser window.
//
// The two sides cannot name each other. The extension knows `browser.windows` ids and tab
// titles; the plugin knows CWindow and its title; neither has the other's vocabulary and
// there is no id crossing between them in either direction.
//
// The one thing both have is the active tab's title: a browser window's title IS its active
// tab's title, and that is true in Gecko and Chromium alike. So the match is by title, and
// everything about it is built around being wrong safely:
//
//   - Exact, not fuzzy. A prefix or a substring match would be guessing, and a browser
//     window guessed into the wrong window is a set of tabs filed under the wrong program.
//     If the title does not match exactly, the match simply does not happen.
//   - One-to-one. A window hosts one browser window. Two browser windows claiming the same
//     Hyprland window is not a match, it is a conflict.
//   - Ambiguous means unmatched. Two Hyprland windows with the same title -- two inboxes,
//     two terminals -- and nothing says which is which, so NEITHER is matched. Guessing
//     here would file one browser's tabs under the other's window, which is the one failure
//     the user cannot detect by looking.
//
// Unmatched is a supported outcome, not a failure: the window then contributes nothing in
// Tabs mode, exactly as a window nobody has heard from does (see ReportedTabs::known). The whole
// point of that mode's fallback is that the ambiguous case degrades into the ordinary one.

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
    std::string title;
};

// Browser window id -> index into `hosts`, for those that matched.
//
// `activeTitles` is the same map keyed by browser window id: the title of that browser
// window's active tab, which is what a browser window's title is. A browser window absent
// from it is not matched either -- no active tab, no title, nothing to agree on.
[[nodiscard]] inline std::unordered_map<std::uint64_t, std::size_t> matchBrowserWindows(
    const std::vector<HostWindow> &hosts,
    const std::unordered_map<std::uint64_t, std::string> &activeTitles) {

    // Host indices by title. A title seen twice makes it unmatchable, which is why this keeps
    // every index rather than the first hit: keeping the first would be a coin flip dressed
    // up as a rule.
    std::unordered_map<std::string, std::vector<std::size_t>> byTitle;
    for (std::size_t i = 0; i < hosts.size(); ++i)
        byTitle[hosts[i].title].push_back(i);

    // How many browser windows claim each title, and the other half of the rule: a title must
    // be claimed exactly once. Anything else is a guess -- two windows with one title, or two
    // browser windows fighting over one.
    //
    // Counting the BROWSER side too is not redundancy. Counting only the hosts would leave
    // "two browser windows, one title" to be decided by whichever the hash map visited first:
    // a coin flip that lands differently between runs on identical input.
    //
    // Empty titles are excluded here, by never being counted, rather than by a second check
    // further down. That is why they can never satisfy "exactly once" below: an empty title
    // reads as UNKNOWN, not as a window that agrees to be empty, and matching it would hand
    // one browser's tabs to whatever window had no title yet. The test pins this by breaking
    // this line and nothing else -- a separate guard for it would pass the same test whether
    // or not this one was there.
    std::unordered_map<std::string, int> browserTitleUses;
    for (const auto &[browserWindow, title] : activeTitles)
        if (!title.empty())
            ++browserTitleUses[title];

    std::unordered_map<std::uint64_t, std::size_t> out;
    for (const auto &[browserWindow, title] : activeTitles) {
        if (browserTitleUses[title] != 1)
            continue; // unclaimed, shared, or empty: none of those is a match
        const auto it = byTitle.find(title);
        if (it == byTitle.end())
            continue;
        const auto &cands = it->second;
        if (cands.size() != 1)
            continue; // ambiguous: two windows, one title, no way to tell
        out.emplace(browserWindow, cands.front());
    }
    return out;
}

// The match, kept. A map from a compositor window to the browser window whose tabs it shows.
//
// This is a type rather than a bare map because it has a lifetime: it is built when a run
// opens and dropped when it ends, and "dropped" is the part that has to be right. Holding
// it for the life of the plugin would mean a title that matched during one run still
// deciding which browser a window belongs to during the next -- and titles are what the
// match is made of.
//
// Both directions, because both get asked and answering one by scanning for the other is a
// second place that could disagree with the first.
class BrowserLinks {
  public:
    void rebuild(const std::vector<HostWindow> &hosts,
                 const std::unordered_map<std::uint64_t, std::string> &activeTitles) {
      // Replaced, never accumulated: the next run must not inherit the last run's answers.
      m_byHost.clear();
      m_byBrowser.clear();
      for (const auto &[browserWindow, host] :
           matchBrowserWindows(hosts, activeTitles)) {
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
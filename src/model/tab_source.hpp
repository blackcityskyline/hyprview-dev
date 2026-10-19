#pragma once

// Pure: no Hyprland type appears here, so ctest reaches it without a compositor (AGENTS.md 2.20).
// activate/close/moveTo are messages to the browser and no channel exists: each answers false.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "content_source.hpp"
#include "subject.hpp"
#include "tab_feed.hpp"

namespace gloview::model {

class TabsContentSource final : public IContentSource {
  public:
    // Replaces wholesale, never merged; a rejected snapshot leaves the previous in place.
    bool adopt(TabSnapshot snap) {
      if (snap.generation == 0)
        return false; // generation 0 is window-domain by definition (AGENTS.md 2.17)
      windowsByTab_.clear();
      titles_.clear();
      for (const auto &[browserWindow, reported] : snap.windows)
          for (const auto &[tabId, info] : reported.tabs) {
            const std::uint64_t id = static_cast<std::uint64_t>(tabId);
            // Assigned, not inserted: LAST report of a repeated id wins; enumerate() still
            // reports both, so this is deliberately not a dedup.
            windowsByTab_[id] = browserWindow;
            titles_[id]       = std::string{info.title};
          }
      m_snap = std::move(snap);
      return true;
    }

    [[nodiscard]] std::uint32_t generation() const { return m_snap.generation; }

    [[nodiscard]] const TabSnapshot &snapshot() const { return m_snap; }
    [[nodiscard]] bool empty() const { return m_snap.windows.empty(); }

    // NOT const, same contract as WindowsContentSource::enumerate(): callers must not cache
    // it across a reconnect, where every id is being reused.
    [[nodiscard]] std::vector<SubjectRef> enumerate() override {
        std::vector<SubjectRef> out;
        std::size_t total = 0;
        for (const auto &[browserWindow, reported] : m_snap.windows)
            total += reported.tabs.size();
        out.reserve(total);
        for (const auto &[browserWindow, reported] : m_snap.windows)
            for (const auto &[tabId, info] : reported.tabs)
                // The epoch is the connection that issued the id: "tab 41" from last week's
                // browser and "tab 41" from this one are different subjects.
                out.push_back(
                    SubjectRef{static_cast<std::uint64_t>(tabId), m_snap.generation});
        return out;
    }

    // A tab's group is the BROWSER WINDOW, never a Hyprland workspace (AGENTS.md 2.20).
    [[nodiscard]] GroupRef groupOf(SubjectRef s) const override {
        if (s.epoch != m_snap.generation)
            return {};
        if (const auto w = browserWindowOf(s))
            return GroupRef{*w, m_snap.generation};
        return {};
    }

    [[nodiscard]] std::string label(SubjectRef s) const override {
        if (s.epoch != m_snap.generation)
            return {};
        const auto it = titles_.find(s.id);
        return it == titles_.end() ? std::string{} : it->second;
    }

    // Always true, but load-bearing: WindowsContentSource::isTab() is false by construction,
    // so the two differing is what lets a caller ask "which domain" once.
    [[nodiscard]] bool isTab(SubjectRef s) const override {
        return s.epoch != 0 && s.epoch == m_snap.generation;
    }

    // Titles stay here as strings, not string_views into the parsed text. "Heard from" is
    // not "has tabs" -- the fallback rule (entriesFor).
    [[nodiscard]] bool knownFor(std::uint64_t browserWindow) const {
        const auto it = m_snap.windows.find(browserWindow);
        return it != m_snap.windows.end() && it->second.known;
    }

    [[nodiscard]] std::vector<std::int64_t> tabIdsFor(std::uint64_t browserWindow) const {
        const auto it = m_snap.windows.find(browserWindow);
        if (it == m_snap.windows.end())
            return {};
        std::vector<std::int64_t> ids;
        ids.reserve(it->second.tabs.size());
        for (const auto &[tabId, info] : it->second.tabs)
            ids.push_back(tabId);
        return ids;
    }

    bool activate(SubjectRef) override { return false; }
    bool close(SubjectRef) override { return false; }
    bool moveTo(SubjectRef, GroupRef) override { return false; }

    void clear() {
        m_snap = {};
        windowsByTab_.clear();
        titles_.clear();
    }

  private:
    // Rebuilt on adopt() rather than searched per query, because groupOf() is asked per
    // tile per frame.
    [[nodiscard]] std::optional<std::uint64_t> browserWindowOf(SubjectRef s) const {
        const auto it = windowsByTab_.find(s.id);
        if (it == windowsByTab_.end())
            return std::nullopt;
        return it->second;
    }

    TabSnapshot m_snap;
    std::unordered_map<std::uint64_t, std::uint64_t> windowsByTab_;
    std::unordered_map<std::uint64_t, std::string> titles_;
};

} // namespace gloview::model

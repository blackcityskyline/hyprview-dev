#pragma once

// Tabs as subjects, the second implementation of IContentSource -- the one that made
// `virtual` mean something. It is PURE: not one Hyprland type appears here, because a tab has
// no window, no surface, no workspace, so ctest checks it without a compositor.
//
// What it cannot do yet it says rather than fakes: activate, close and moveTo are messages to
// the browser and no such channel exists. They answer false, the interface's word for "it did
// not happen", so the caller does not animate towards a subject that did not respond.

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
    // Replaces everything with one snapshot, wholesale, never merged. Returns whether it was
    // ACCEPTED: a rejected one leaves the previous in place rather than emptying the source,
    // because "the file was half-written" and "this browser has no tabs" must not reach the
    // screen as the same thing -- one flickers the whole grid.
    bool adopt(TabSnapshot snap) {
      // Rebuilt, never merged, exactly as parseTabs() replaces rather than merges.
      if (snap.generation == 0)
        return false; // generation 0 is window-domain by definition (AGENTS.md 2.17)
      windowsByTab_.clear();
      titles_.clear();
      for (const auto &[browserWindow, reported] : snap.windows)
          for (const auto &[tabId, info] : reported.tabs) {
            const std::uint64_t id = static_cast<std::uint64_t>(tabId);
            // Assigned, not inserted: a repeated tab id inside one connection is corrupt input
            // and parseTabs() does not refuse it, so the LAST report wins -- the first would
            // answer with a tab the browser already replaced. This does NOT deduplicate:
            // enumerate() reports both because that is what the file says.
            windowsByTab_[id] = browserWindow;
            titles_[id]       = std::string{info.title};
          }
      m_snap = std::move(snap);
      return true;
    }

    [[nodiscard]] std::uint32_t generation() const { return m_snap.generation; }

    // The snapshot as parsed, for the questions the other two do not answer -- today only
    // which tab of a browser window is active.
    [[nodiscard]] const TabSnapshot &snapshot() const { return m_snap; }
    [[nodiscard]] bool empty() const { return m_snap.windows.empty(); }

    // One subject per tab, in the order reported. NOT const, for the same reason as
    // WindowsContentSource::enumerate(): callers must not cache it across a reconnect, where
    // every id is being reused. There is no ownership to refresh here, but the contract is
    // one contract.
    [[nodiscard]] std::vector<SubjectRef> enumerate() override {
        std::vector<SubjectRef> out;
        std::size_t total = 0;
        for (const auto &[browserWindow, reported] : m_snap.windows)
            total += reported.tabs.size();
        out.reserve(total);
        for (const auto &[browserWindow, reported] : m_snap.windows)
            for (const auto &[tabId, info] : reported.tabs)
                // The tab id is the subject, unique browser-wide in both Gecko and Chromium;
                // the epoch is the connection that issued it, so "tab 41" from last week's
                // browser and "tab 41" from this one are different subjects.
                out.push_back(
                    SubjectRef{static_cast<std::uint64_t>(tabId), m_snap.generation});
        return out;
    }

    // A tab's group is the BROWSER WINDOW it lives in -- never a Hyprland workspace, which
    // the compositor knows nothing about and which would be a lie in every tab's case.
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

    // Always true, and not a formality: WindowsContentSource::isTab() answers false by
    // construction, so the two differing is what lets a caller ask "which domain" once.
    [[nodiscard]] bool isTab(SubjectRef s) const override {
        return s.epoch != 0 && s.epoch == m_snap.generation;
    }

    // Whether anything has been reported for this browser window, and its tab ids. Split from
    // ReportedTabs on purpose: its titles are string_views into the parsed text, which dies
    // with the call that produced it. "Heard from" is not "has tabs" -- a browser that has not
    // reported and one that reported none both show up here, distinguishably, which is the
    // fallback rule (entriesFor).
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

    // --- no transport yet --------------------------------------------------------------------
    // Each is a message to the browser and there is no channel to it yet. False is the
    // interface's word for "it did not happen".
    bool activate(SubjectRef) override { return false; }
    bool close(SubjectRef) override { return false; }
    bool moveTo(SubjectRef, GroupRef) override { return false; }

    // Forgets everything. Called when the run ends, so a stale tab cannot be rendered and so
    // the titles do not outlive the browser window they belonged to.
    void clear() {
        m_snap = {};
        windowsByTab_.clear();
        titles_.clear();
    }

  private:
    // The browser window a tab belongs to, or nothing. Rebuilt on adopt() rather than searched
    // per query, because groupOf() is asked per tile per frame.
    [[nodiscard]] std::optional<std::uint64_t> browserWindowOf(SubjectRef s) const {
        const auto it = windowsByTab_.find(s.id);
        if (it == windowsByTab_.end())
            return std::nullopt;
        return it->second;
    }

    TabSnapshot m_snap;
    // Flattened on adopt(): the two questions above are per-subject while the snapshot is
    // nested, and rebuilding per query would be a walk per tile per frame.
    std::unordered_map<std::uint64_t, std::uint64_t> windowsByTab_;
    std::unordered_map<std::uint64_t, std::string> titles_; // tab id -> title
};
} // namespace gloview::model

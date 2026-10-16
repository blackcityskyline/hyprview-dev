#pragma once

// Tabs as subjects. The second implementation of IContentSource, and the one that made the
// interface worth marking virtual: with only Windows, `virtual` was a promise about a second
// implementation that did not exist.
//
// It is PURE. Not one Hyprland type appears here, and none can: a tab has no window, no
// surface, no workspace. That is not an accident of the current stage -- it is what the
// boundary is for, and it means this file is checked by ctest without a compositor, exactly
// like the interface it implements.
//
// What it cannot do yet, and says so rather than faking: activate, close and moveTo are
// remote operations. For a window they are compositor calls; for a tab they are messages to
// the browser, and no such channel exists yet. They answer false, which the interface
// defines as "it did not happen", and the caller treats that as "do not animate towards
// something that is gone" -- the correct behaviour for a verb with no transport, and far
// better than a tab that appears to close and does not.

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
    // Replaces everything with one snapshot. Wholesale, never merged: see parseTabs().
    //
    // Returns whether the snapshot was ACCEPTED, which is the one thing a caller has to be
    // able to know. A rejected snapshot leaves the previous one in place rather than
    // emptying the source, because "the file was half-written" and "this browser has no
    // tabs" must not reach the screen as the same thing -- one flickers the whole grid, the
    // other is a fact about the browser.
    bool adopt(TabSnapshot snap) {
      // Rebuilt, never merged, exactly as parseTabs() replaces rather than merges.
      if (snap.generation == 0)
        return false; // generation 0 is window-domain by definition (AGENTS.md 2.17)
      windowsByTab_.clear();
      titles_.clear();
      for (const auto &[browserWindow, reported] : snap.windows)
          for (const auto &[tabId, info] : reported.tabs) {
            const std::uint64_t id = static_cast<std::uint64_t>(tabId);
            // Assigned, not inserted. A tab id repeated inside one connection is corrupt
            // input and parseTabs() does not refuse it, so this has to pick something; the
            // file lists tabs in order, which makes the LAST report the more recent one, and
            // picking the first would answer with a tab the browser has already replaced.
            // Note what this does NOT do: deduplicate. enumerate() still reports both, because
            // that is what the file says, and quietly collapsing them here would make the
            // grid disagree with the snapshot for no stated reason.
            windowsByTab_[id] = browserWindow;
            titles_[id]       = std::string{info.title};
          }
      m_snap = std::move(snap);
      return true;
    }

    [[nodiscard]] std::uint32_t generation() const { return m_snap.generation; }

    // The snapshot as parsed, for the questions knownFor()/tabIdsFor() do not answer. The
    // only one today is which tab of a browser window is active, and a second accessor's
    // existence would be a second place that could disagree about the same snapshot.
    [[nodiscard]] const TabSnapshot &snapshot() const { return m_snap; }
    [[nodiscard]] bool empty() const { return m_snap.windows.empty(); }

    // One subject per tab, in the order the browser reported them.
    //
    // NOT const, for the same reason as WindowsContentSource::enumerate(): it is a read of
    // the current world that callers must not be able to cache across a reconnect, where
    // every id is being reused. There is no ownership to refresh here, but the contract is
    // one contract and a source that quietly differed on the signature would make every
    // caller write a dynamic_cast.
    [[nodiscard]] std::vector<SubjectRef> enumerate() override {
        std::vector<SubjectRef> out;
        std::size_t total = 0;
        for (const auto &[browserWindow, reported] : m_snap.windows)
            total += reported.tabs.size();
        out.reserve(total);
        for (const auto &[browserWindow, reported] : m_snap.windows)
            for (const auto &[tabId, info] : reported.tabs)
                // The tab id is the subject. It is unique browser-wide in both Gecko and
                // Chromium, and the epoch is the connection that issued it -- so "tab 41"
                // from last week's browser and "tab 41" from this one are different subjects,
                // which is the whole reason an id alone would have been an ABA bug waiting
                // to happen.
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

    // Always true, and the answer is not a formality: WindowsContentSource::isTab() answers
    // false by construction, and the two differing is what lets a caller ask "which domain"
    // once instead of holding a flag and hoping it tracks.
    [[nodiscard]] bool isTab(SubjectRef s) const override {
        return s.epoch != 0 && s.epoch == m_snap.generation;
    }

    // Whether anything has been reported for this browser window, and its tab ids.
    //
    // Split from ReportedTabs on purpose: that type's titles are string_views into the parsed
    // text, which dies with the call that produced it, so handing it back to a caller would be
    // a dangling view dressed as a return value. The two facts a caller actually needs --
    // heard from or not, and which ids -- survive that, and nothing else is asked for here.
    //
    // "Heard from" is not "has tabs". A browser that has not reported yet and one that
    // reported none both show up here, distinguishable, and the difference is the whole
    // fallback rule (see entriesFor).
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

    // --- no transport yet ------------------------------------------------------------------------
    // Each of these is a message to the browser, and there is no channel to the browser yet.
    // False is the interface's own word for "it did not happen", so the caller already knows
    // what to do with this: do not animate towards a subject that did not respond.
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
    // Flattened on adopt(), because the two questions above are per-subject and the snapshot
    // is nested. Rebuilding them per query would be a walk per tile per frame to answer a
    // question that changed only when the file did.
    std::unordered_map<std::uint64_t, std::uint64_t> windowsByTab_;
    std::unordered_map<std::uint64_t, std::string> titles_; // tab id -> title
};
} // namespace gloview::model

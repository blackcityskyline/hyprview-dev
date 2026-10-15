#pragma once

#include "subject.hpp"
#include "tab_feed.hpp"

#include <cstddef>
#include <vector>

namespace gloview::model {

// What the overview is showing FOR ONE WINDOW.
//
// A window has always been one entry. A browser window has as many as it has tabs, so an
// entry is now either the window itself or a tab inside it. `tabId == -1` means the former,
// and that is the only thing that distinguishes them: the box is the same kind of box, the
// label is the same kind of label, and every existing rule about "a tile" applies to both.
//
// The window handle is carried as an opaque pointer here. An entry is an identity and an
// index; deciding whether a window belongs on the run at all is somebody else's question,
// and keeping Hyprland out of this header is what lets the rule below be checked without a
// compositor.
//
// `tabId == -1` means the window itself. It was once its own struct, TabSet, and it was a
// duplicate: same `known`, same list of ids, one place carrying the titles and the other
// throwing them away. Two spellings of one shape is how "which tabs does this window have"
// gets answered twice and the answers drift.
struct Entry {
    const void *win;
    int tabId = -1;
};

// The entries one window contributes.
//
// The fallback is the part that matters. In Tabs mode a window we have never heard about
// falls back to being shown as a window, because a browser that has not reported yet is
// indistinguishable from one with no tabs -- and silently dropping every window until the
// extension connects is the failure mode that reads as "the overview lost my windows".
//
// A window we HAVE heard from, and which reported none, contributes nothing. That is the
// mode working.
[[nodiscard]] inline std::vector<Entry> entriesFor(const void *win, Domain mode,
                                                   const ReportedTabs &tabs) {
    std::vector<Entry> out;
    const bool window = showsWindows(mode) || !tabs.known;
    if (window)
      out.push_back(Entry{win, -1});
    if (showsTabs(mode)) {
        out.reserve(out.size() + tabs.tabs.size());
        for (const auto &[id, info] : tabs.tabs)
            out.push_back(Entry{win, static_cast<int>(id)});
    }
    return out;
}

} // namespace gloview::model
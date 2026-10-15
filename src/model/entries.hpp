#pragma once

#include "subject.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
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
// `tabId == -1` means the window itself. The list of a window's tabs was once its own struct,
// TabSet, and it was a duplicate of ReportedTabs: same `known`, same list of ids, one place
// carrying the titles and the other throwing them away. That struct is gone, and so is the
// temptation to rebuild it here -- ReportedTabs cannot be handed back out of a source, because
// its titles are string_views into the parsed text and that text is not the caller's to keep.
struct Entry {
    const void *win;
    std::int64_t tabId = -1;
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
// Two plain facts rather than a type: this needs to know WHETHER the window was heard from
// and WHICH tabs, and a container assembled to carry that would be a third spelling of
// something ReportedTabs already describes. ReportedTabs cannot be handed back out of a
// source either -- its titles are string_views into the parsed text, which dies with the call
// that produced it -- so a caller asking this question cannot be given one.
[[nodiscard]] inline std::vector<Entry> entriesFor(const void *win, Domain mode,
                                                   bool tabsKnown,
                                                   std::span<const std::int64_t> tabIds) {
    std::vector<Entry> out;
    const bool window = showsWindows(mode) || !tabsKnown;
    if (window)
      out.push_back(Entry{win, -1});
    if (showsTabs(mode)) {
        out.reserve(out.size() + tabIds.size());
        for (const auto id : tabIds)
          out.push_back(Entry{win, id});
    }
    return out;
}

} // namespace gloview::model
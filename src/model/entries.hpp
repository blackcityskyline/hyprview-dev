#pragma once

#include "subject.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gloview::model {

// What the overview shows FOR ONE WINDOW: the window itself, or a tab inside it. The box and
// the label are the same kind either way, so every rule about "a tile" applies to both.
// `tabId == -1` is the window. The handle is an opaque pointer: an entry is an identity and
// an index, and which windows belong is somebody else's question -- keeping Hyprland out is
// what lets the rule below be checked without a compositor.
struct Entry {
    const void *win;
    std::int64_t tabId = -1;
};

// The entries one window contributes.
//
// A browser we have never heard from falls back to being shown as a window: a browser that
// has not reported yet is indistinguishable from one with no tabs, and dropping every browser
// until the extension connects reads as "the overview lost my windows". A window we HAVE
// heard from, reporting none, contributes nothing -- that is the mode working.
//
// Never-heard-from and not-a-browser are both an empty tab list, and reading them as one
// thing put the terminal in the browser's view. So the caller says which of the two it has.
// Two plain facts rather than a container: `isBrowser` answers whether it was heard from and
// the span answers which tabs, and ReportedTabs cannot be handed back out of a source anyway
// -- its titles are string_views into the parsed text, which dies with the call.
[[nodiscard]] inline std::vector<Entry> entriesFor(const void *win, Domain mode,
                                                   bool tabsKnown,
                                                   std::span<const std::int64_t> tabIds,
                                                   bool isBrowser) {
    std::vector<Entry> out;
    const bool window = showsWindows(mode) || (isBrowser && !tabsKnown);
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
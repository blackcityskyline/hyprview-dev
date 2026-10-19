#pragma once

#include "subject.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hyprview::model {

// What the overview shows FOR ONE WINDOW: the window or a tab inside it (`tabId == -1` = window).
struct Entry {
    const void *win;
    std::int64_t tabId = -1;
};

// Never-heard-from and not-a-browser are both an empty tab list, so the caller says which it
// has: `isBrowser` whether it was heard from, the span which tabs. A browser we have never
// heard from is shown as a window, while one reporting none contributes nothing.
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

} // namespace hyprview::model

#pragma once

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
struct Entry {
    const void *win;
    int tabId = -1;
};

// Which of the three the user asked for.
enum class TileMode {
    // One entry per window. What the plugin always did.
    Windows,
    // One entry per TAB, and a window with no tabs contributes nothing at all -- it is not
    // shown as a window, because the point of this mode is that only tabs are shown.
    Tabs,
    // The window itself plus each of its tabs.
    Both,
};

// What the extension has told us about one window's tabs.
struct TabSet {
    // False when nothing has been reported for this window yet, which is NOT the same as
    // "it has no tabs" and has to stay distinguishable: a window whose data has not arrived
    // must not vanish from the overview.
    bool known = false;
    std::vector<int> ids; // tab ids, in the extension's order
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
[[nodiscard]] inline std::vector<Entry> entriesFor(const void *win, TileMode mode,
                                                   const TabSet &tabs) {
    std::vector<Entry> out;
    const bool window = mode != TileMode::Tabs || !tabs.known;
    if (window)
      out.push_back(Entry{win, -1});
    if (mode != TileMode::Windows) {
        out.reserve(out.size() + tabs.ids.size());
        for (const auto id : tabs.ids)
            out.push_back(Entry{win, id});
    }
    return out;
}

// A mode parsed from config. Anything unrecognised is Windows: the mode that was always
// there, so a typo cannot turn the overview into something else.
[[nodiscard]] inline TileMode parseTileMode(const std::string &s) {
    if (s == "tabs")
        return TileMode::Tabs;
    if (s == "both")
        return TileMode::Both;
    return TileMode::Windows;
}

[[nodiscard]] inline const char *tileModeName(TileMode m) {
    switch (m) {
        case TileMode::Tabs:   return "tabs";
        case TileMode::Both:   return "both";
        case TileMode::Windows: break;
    }
    return "windows";
}

} // namespace gloview::model
#pragma once

// Parsing what the browser side told us about its tabs.
//
// Deliberately only THIS. Reading the file is three lines of std::ifstream and belongs with
// whoever does the reading; the rules that decide what a snapshot MEANS are the part worth
// testing, and none of them need a compositor, a socket or an extension.
//
// The format is line-oriented and has no quoting, because the plugin has no JSON dependency
// and is not going to acquire one (AGENTS.md 1: no curl/json/websocket in this process). A
// title is the rest of the line, which is all a caption ever wanted from it:
//
//   g <generation>
//   p <pid> <window>      the process a browser window belongs to
//   t <window> <tab> <active> <title...>
//
// <generation> is the connection's identity. It is what makes tab ids safe: ids are reused
// (close tab 41, tab 41 exists again a moment later on a different page), so an id is only
// meaningful under the generation that issued it -- the same ABA hazard as a window address,
// which this project already has a rule about (AGENTS.md 2.9). The browser side bumps it on
// every reconnect, and a snapshot carrying a generation the plugin has not seen means every
// id in it is being reused.
//
// A snapshot replaces the previous one wholesale. There is no diffing and no merging: the
// writer owns the truth, and a partial merge would have to invent a rule for "this tab was
// there a moment ago but is not in the snapshot now" that is indistinguishable from a tab
// that was closed.

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gloview::model {

// What the caption says about one tab. Empty is allowed and means exactly what it means for
// a window title: the caller decides what to show instead.
struct TabInfo {
    std::string_view title;
    bool active = false;
};

// One window's tabs, as reported. `known` is what separates "reported no tabs" from "never
// reported", and the difference is the whole fallback rule: a browser that has not spoken
// yet must still be shown as a window, or the overview looks like it lost every window until
// the extension connects.
struct ReportedTabs {
    bool known = false;
    std::vector<std::pair<std::int64_t, TabInfo>> tabs;
};

// Everything one connection said. `generation` is that connection's identity.
struct TabSnapshot {
    std::uint32_t generation = 0;
    // browser window id -> the OS process its window belongs to.
    //
    // This is what a browser window is matched to a compositor window BY, and it is a pid
    // rather than a title because a browser appends its own name to a window's title --
    // "(page) - YouTube -- Zen Browser" -- so the title a compositor holds and the title an
    // extension reports are different strings by construction. A pid is not a heuristic: two
    // windows of the same browser share it, which is exactly the grouping wanted, and a
    // window of a different browser never does.
    //
    // A window absent from this map has no pid and matches nothing.
    std::unordered_map<std::uint64_t, std::uint64_t> pids;
    // Window id -> its tabs, in the order they were reported. A window absent from this map
    // has NOT been heard from -- which is not the same as having no tabs, and must never be
    // collapsed into it.
    std::unordered_map<std::uint64_t, ReportedTabs> windows;
};

// A snapshot parsed out of a file, or nothing.
//
// Nothing is the right answer for: no file, an empty file, a file whose first line is not a
// generation header, and a file with no generation header followed by garbage. All four mean
// "nothing has been reported", and all four must be indistinguishable from one another to the
// caller -- a half-read snapshot is worse than none, because it looks like a browser with
// fewer tabs.
// One bare non-negative decimal integer at `pos`, advancing past trailing spaces.
//
// False for a sign, letters, an empty field, junk glued onto the number, and anything that
// would overflow. Every one of those means the writer and the reader disagree about the
// format, and picking the number that "looks right" is how a tab ends up filed under the
// wrong window -- or a corrupt generation wrapping onto one the plugin has already seen.
[[nodiscard]] inline bool number(std::string_view line, std::size_t &pos,
                                 std::uint64_t &out) {
    while (pos < line.size() && line[pos] == ' ')
        ++pos;
    const std::size_t start = pos;
    out                     = 0;
    while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        if (out > (UINT64_MAX - 9) / 10)
            return false;
        out = out * 10 + static_cast<std::uint64_t>(line[pos] - '0');
        ++pos;
    }
    if (pos == start)
        return false; // no digits
    return pos == line.size() || line[pos] == ' ';
}

[[nodiscard]] inline std::optional<TabSnapshot> parseTabs(std::string_view text) {
    TabSnapshot out;
    bool haveHeader = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto eol   = text.find('\n', pos);
        if (eol == std::string_view::npos)
            eol = text.size();
        auto line = text.substr(pos, eol - pos);
        pos       = eol + 1;

        if (line.empty() || line == "\r")
            continue;

        // Header: 'g' and the generation. It must come first, because a generation read
        // after some tabs have already been collected is a generation that could have
        // belonged to a different connection than the tabs below it.
        if (line.front() == 'g') {
            if (haveHeader || line.size() < 3 || line[1] != ' ')
                return std::nullopt;
            std::size_t p = 2;
            std::uint64_t gen = 0;
            if (!number(line, p, gen) || gen > 0xffffffffULL)
                return std::nullopt;
            out.generation = static_cast<std::uint32_t>(gen);
            haveHeader     = true;
            continue;
        }

        // Process: 'p' pid window. Declared per window rather than once per feed because a
        // feed can describe several browsers -- each mediator is a separate process on its own
        // port -- and their windows must not be attributed to one another.
        if (line.front() == 'p') {
            if (line.size() < 4 || line[1] != ' ')
                return std::nullopt;
            std::size_t pp = 2;
            std::uint64_t pid = 0, window = 0;
            if (!number(line, pp, pid) || !number(line, pp, window))
                return std::nullopt;
            out.pids[window] = pid;
            continue;
        }

        // Tab: 't' window tab active title...
        if (!haveHeader)
            return std::nullopt; // tabs before a generation: ids with nothing to be scoped to
        if (line.front() != 't' || line.size() < 2 || line[1] != ' ')
            return std::nullopt;

        std::size_t p    = 2;
        std::uint64_t window = 0, tab = 0;
        if (!number(line, p, window) || !number(line, p, tab))
            return std::nullopt;
        while (p < line.size() && line[p] == ' ')
            ++p;

        // active: exactly one character, and only 0 or 1. A bare 2 or a word here would mean
        // the writer and the reader disagree about the format, and guessing is how a tab
        // ends up silently not active.
        while (p < line.size() && line[p] == ' ')
            ++p;
        if (p >= line.size() || (line[p] != '0' && line[p] != '1'))
            return std::nullopt;
        const bool active = line[p] == '1';
        ++p;
        while (p < line.size() && line[p] == ' ')
            ++p;

        // Title: the rest of the line, spaces included. May be empty.
        auto title = line.substr(p);
        if (!title.empty() && title.front() == '\r')
            title.remove_suffix(1);

        auto &entry = out.windows[window];
        entry.known = true;
        entry.tabs.push_back({static_cast<std::int64_t>(tab), TabInfo{title, active}});
    }

    if (!haveHeader)
        return std::nullopt; // a file with tabs but no generation is not a snapshot
    return out;
}

} // namespace gloview::model
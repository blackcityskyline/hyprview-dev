#pragma once

// Parsing what the browser side told us about its tabs -- only this: what a snapshot MEANS
// is the part worth testing and needs no compositor, socket or extension.
//
// Line-oriented, no quoting, because the plugin has no JSON dependency and is not acquiring
// one (AGENTS.md 1). A title is the rest of the line, which is all a caption wants:
//
//   g <generation>
//   p <pid> <window>      the process a browser window belongs to
//   t <window> <tab> <active> <title...>
//
// <generation> is the connection's identity and is what makes tab ids safe: ids are reused
// (close tab 41, tab 41 exists again a moment later), so an id is only meaningful under the
// generation that issued it -- the same ABA hazard as a window address (AGENTS.md 2.9).
// A snapshot replaces the previous wholesale: no diffing, no merging.

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

// One window's tabs, as reported. `known` separates "reported no tabs" from "never reported",
// and that difference is the whole fallback rule: a browser that has not spoken yet must still
// be shown as a window, or the overview looks like it lost every window.
struct ReportedTabs {
    bool known = false;
    std::vector<std::pair<std::int64_t, TabInfo>> tabs;
};

// Everything one connection said. `generation` is that connection's identity.
struct TabSnapshot {
    std::uint32_t generation = 0;
    // browser window id -> the OS process its window belongs to: what a browser window is
    // matched to a compositor window BY. A pid rather than a title, because a browser appends
    // its own name to a window's title ("(page) - YouTube -- Zen Browser") so the two sides
    // hold different strings by construction. Absent from this map means no pid and no match.
    std::unordered_map<std::uint64_t, std::uint64_t> pids;
    // Window id -> its tabs, in report order. Absent means NOT heard from, which must never be
    // collapsed into "has no tabs".
    std::unordered_map<std::uint64_t, ReportedTabs> windows;
};

// A snapshot parsed out of a file, or nothing. Nothing covers no file, an empty file, a first
// line that is not a generation header, and a header followed by garbage: all four mean
// "nothing has been reported" and must be indistinguishable, because a half-read snapshot
// looks like a browser with fewer tabs.
// One bare non-negative decimal integer at `pos`. False for a sign, letters, an empty field,
// junk glued on and anything that would overflow -- each means writer and reader disagree
// about the format, and picking what "looks right" is how a tab ends up filed under the wrong
// window.
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

        // Must come first: a generation read after some tabs were collected could belong to a
        // different connection than the tabs below it.
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

        // Per window rather than once per feed: a feed can describe several browsers, one
        // mediator each, and their windows must not be attributed to one another.
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

        // Exactly one character, and only 0 or 1: a 2 or a word here is a format disagreement,
        // and guessing is how a tab ends up silently not active.
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
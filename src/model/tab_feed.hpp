#pragma once

// Feed lines from the browser side: no quoting, no JSON dependency (AGENTS.md 1).
//   g <generation> / p <pid> <window> / t <window> <tab> <flags> <title...>
// Tab ids are reused: an id means something only under the generation that issued it (2.9).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hyprview::model {

struct TabInfo {
    std::string_view title;
    bool active = false;
    // Open but not in memory; activating it is what reloads it.
    bool discarded = false;
};

// What the plugin asks the mediator for: one tab, in the browser process it names.
[[nodiscard]] inline std::string activateCommand(std::uint64_t pid, std::uint64_t window,
                                                 std::int64_t tab) {
    return "a " + std::to_string(pid) + " " + std::to_string(window) + " " +
           std::to_string(tab) + "\n";
}

// `known` separates "reported no tabs" from "never reported" -- the whole fallback rule: a browser that has not spoken yet must still be shown as a window (entriesFor).
struct ReportedTabs {
    bool known = false;
    std::vector<std::pair<std::int64_t, TabInfo>> tabs;
};

struct TabSnapshot {
    std::uint32_t generation = 0;
    // Browser window id -> OS pid: what a browser window is matched BY, never by title -- the browser appends its own name to the window title (AGENTS.md 2.19).
    std::unordered_map<std::uint64_t, std::uint64_t> pids;
    // Window id -> its tabs, in report order. Absent means NOT heard from, which must never collapse into "has no tabs".
    std::unordered_map<std::uint64_t, ReportedTabs> windows;
};

// Both parsers refuse rather than guess: no file, an empty file, a bad header and trailing
// garbage all mean "nothing has been reported". One bare non-negative decimal integer at `pos`.
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
        return false;
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

        if (!haveHeader)
            return std::nullopt;
        if (line.front() != 't' || line.size() < 2 || line[1] != ' ')
            return std::nullopt;

        std::size_t p    = 2;
        std::uint64_t window = 0, tab = 0, flags = 0;
        // Bit 0 active, bit 1 discarded; a word here is a disagreement, not a guess.
        if (!number(line, p, window) || !number(line, p, tab) || !number(line, p, flags))
            return std::nullopt;
        while (p < line.size() && line[p] == ' ')
            ++p;

        auto title = line.substr(p);
        if (!title.empty() && title.front() == '\r')
            title.remove_suffix(1);

        auto &entry = out.windows[window];
        entry.known = true;
        entry.tabs.push_back({static_cast<std::int64_t>(tab),
                              TabInfo{title, (flags & 1) != 0, (flags & 2) != 0}});
    }

    if (!haveHeader)
        return std::nullopt;
    return out;
}

} // namespace hyprview::model

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace gloview::model {

// Identity of one thing the overview shows: a window, or a tab inside one. Neither the epoch
// nor the width of id is decoration -- both exist because a bare integer is reused. A window
// is identified by its address (never key a map by a raw pointer, AGENTS.md 2.9) and a browser
// tab id is the same hazard from another source: close tab 41 and tab 41 exists again a moment
// later on another page. An id is only meaningful with the epoch it was issued under; the
// source bumps it on every fresh connection and resynchronisation, so a SubjectRef with an
// older epoch fails equality rather than silently matching whoever took the number.
// Default-constructed is Window0@0, the live compositor window.
struct SubjectRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const SubjectRef &) const = default;
};

// Where a subject lives: a workspace for a window, a browser window for a tab. A tab's group
// is not a workspace -- Hyprland knows nothing about tabs -- so it is the same value type
// rather than a handle that only means something in one of the two domains.
struct GroupRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const GroupRef &) const = default;
};

// Which of the three the user asked for. Resolved once per open or dispatch and read
// everywhere a subject is created or asked about, as view::State does for its Engine axis.
enum class Domain : int {
    // One subject per window. What the plugin always did.
    Windows,
    // One subject per browser tab, and a browser window with no tabs is not shown at all.
    Tabs,
    // The window plus each of its tabs.
    Both,
};

[[nodiscard]] constexpr bool showsWindows(Domain d) {
    return d != Domain::Tabs;
}

[[nodiscard]] constexpr bool showsTabs(Domain d) {
    return d != Domain::Windows;
}

// A mode parsed from config. Anything unrecognised is Windows, the mode that was always there,
// so a typo cannot quietly turn the overview into something else. It used to be a second enum
// with the same three values one file over -- two spellings of one question get answered two
// ways.
[[nodiscard]] constexpr Domain parseDomain(std::string_view s) {
    if (s == "tabs")
        return Domain::Tabs;
    if (s == "both")
        return Domain::Both;
    return Domain::Windows;
}

[[nodiscard]] constexpr const char *domainName(Domain d) {
    return d == Domain::Tabs ? "tabs" : d == Domain::Both ? "both" : "windows";
}

} // namespace gloview::model

// std::hash: SubjectRef's whole point is to be a map key and a defaulted operator== does not
// give one. Folding the epoch in is for spread, not correctness -- a hash using only id stays
// valid, and unordered_map resolves collisions with operator==.
template <>
struct std::hash<gloview::model::SubjectRef> {
    std::size_t operator()(const gloview::model::SubjectRef &r) const noexcept {
        const auto h = std::hash<std::uint64_t>{};
        return h(r.id) ^ (h(r.epoch) + 0x9e3779b97f4a7c15ULL + (h(r.id) << 6) + (h(r.id) >> 2));
    }
};

template <>
struct std::hash<gloview::model::GroupRef> {
    std::size_t operator()(const gloview::model::GroupRef &r) const noexcept {
        const auto h = std::hash<std::uint64_t>{};
        return h(r.id) ^ (h(r.epoch) + 0x9e3779b97f4a7c15ULL + (h(r.id) << 6) + (h(r.id) >> 2));
    }
};
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace hyprview::model {

// An id is only meaningful with the epoch it was issued under: a bare integer is reused (close
// tab 41 and tab 41 exists again), and a window is identified by its address, never by a raw
// pointer (AGENTS.md 2.9). A SubjectRef with an older epoch fails equality instead of matching
// whoever took the number.
struct SubjectRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const SubjectRef &) const = default;
};

// Where a subject lives: a workspace for a window, a browser window for a tab -- the same value
// type rather than a handle that only means something in one of the two domains.
struct GroupRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const GroupRef &) const = default;
};

enum class Domain : int {
    Windows,
    Tabs,
    Both,
};

[[nodiscard]] constexpr bool showsWindows(Domain d) {
    return d != Domain::Tabs;
}

[[nodiscard]] constexpr bool showsTabs(Domain d) {
    return d != Domain::Windows;
}

// Anything unrecognised is Windows, the mode that was always there: a typo cannot quietly turn
// the overview into something else.
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

} // namespace hyprview::model

// A defaulted operator== gives no hash. Folding the epoch in is for spread, not correctness:
// unordered_map resolves collisions with operator==.
template <>
struct std::hash<hyprview::model::SubjectRef> {
    std::size_t operator()(const hyprview::model::SubjectRef &r) const noexcept {
        const auto h = std::hash<std::uint64_t>{};
        return h(r.id) ^ (h(r.epoch) + 0x9e3779b97f4a7c15ULL + (h(r.id) << 6) + (h(r.id) >> 2));
    }
};

template <>
struct std::hash<hyprview::model::GroupRef> {
    std::size_t operator()(const hyprview::model::GroupRef &r) const noexcept {
        const auto h = std::hash<std::uint64_t>{};
        return h(r.id) ^ (h(r.epoch) + 0x9e3779b97f4a7c15ULL + (h(r.id) << 6) + (h(r.id) >> 2));
    }
};

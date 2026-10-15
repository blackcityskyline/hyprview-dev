#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gloview::model {

// Identity of one thing the overview shows: a window, or a tab inside one.
//
// The epoch is not decoration and neither is the width of id. Both exist because a bare
// integer is reused.
//
// A window is identified by its address, and this project already made the rule about that:
// never key a map by a raw pointer, because a dead window's address becomes the next one's
// (AGENTS.md 2.9). A tab id from the browser is the same hazard with a different source --
// close tab 41, and tab 41 exists again a moment later pointing at a different page.
//
// So an id is only meaningful together with the epoch it was issued under. The source bumps
// the epoch on every fresh connection and every resynchronisation; a SubjectRef carrying an
// older epoch names a subject that no longer exists, and it fails the equality rather than
// silently matching whatever took the number.
//
// Default-constructed is Window0@0, which is the live compositor window and the only
// subject that has no source of truth behind it.
struct SubjectRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const SubjectRef &) const = default;
};

// Where a subject lives: a workspace for a window, a browser window for a tab.
//
// Same shape and same reason. A tab's group is not a workspace -- Hyprland knows nothing
// about tabs -- so it is carried by the same value type rather than by a Hyprland handle
// that only means something in one of the two domains.
struct GroupRef {
    std::uint64_t id     = 0;
    std::uint32_t epoch = 0;

    bool operator==(const GroupRef &) const = default;
};

// Which of the three the user asked for. Resolved once per open or once per dispatch, read
// everywhere that creates or asks about a subject -- the same pattern view::State already
// uses for its Engine axis.
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

} // namespace gloview::model

// std::hash, because the whole point of SubjectRef is to be a map key and a defaulted
// operator== does not give you one.
//
// Folding the epoch in is for spread, not for correctness: a hash that used only id would
// still be a valid hash -- equal subjects would still hash equal, and unordered_map resolves
// the collision with operator==. Two subjects in the same bucket cost a comparison, nothing
// more.
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
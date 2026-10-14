#pragma once

// A layer namespace that must stay fully visible while the overlay is up: the configured
// list, comma-separated with an optional `*` suffix for a prefix match, plus the implicit
// aboveoverview entry every install carries.
//
// No Hyprland and no config reads, both on purpose. Taking the list as an argument is what
// lets this be compiled by ctest on its own (AGENTS.md 2.15, and hypr_types_check's rule
// that a header a test compiles must stay compilable), and the parse is the whole of the
// rule: a list written "bar, foo " has to yield the same two entries as "bar,foo", because a
// trailing space left on an entry is an entry that matches nothing.

#include <cctype>
#include <string>
#include <vector>

namespace gloview::session {

[[nodiscard]] inline bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

namespace detail {

// The comma-separated split, skipping runs of separators and surrounding whitespace.
[[nodiscard]] inline std::vector<std::string>
splitNamespaces(const std::string &list) {
    std::vector<std::string> out;
    std::size_t             i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ',' || isSpace(list[i])))
            ++i;
        const auto start = i;
        while (i < list.size() && list[i] != ',' && !isSpace(list[i]))
            ++i;
        if (i != start)
            out.push_back(list.substr(start, i - start));
    }
    return out;
}

} // namespace detail

[[nodiscard]] inline bool
isAboveLayer(const std::string &ns, const std::string &aboveNamespaces) {
    if (ns.find("aboveoverview") != std::string::npos)
        return true;
    for (const auto &entry : detail::splitNamespaces(aboveNamespaces)) {
        if (entry.back() == '*') {
            const auto prefix = entry.substr(0, entry.size() - 1);
            if (ns.compare(0, prefix.size(), prefix) == 0)
                return true;
        } else if (ns == entry) {
            return true;
        }
    }
    return false;
}

} // namespace gloview::session

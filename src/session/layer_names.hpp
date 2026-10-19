#pragma once

// No Hyprland and no config reads: the list comes in as an argument, so ctest compiles this
// alone (AGENTS.md 2.15). A trailing space is an entry that matches nothing.

#include <cctype>
#include <string>
#include <vector>

namespace gloview::session {

[[nodiscard]] inline bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

namespace detail {

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

}

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

}

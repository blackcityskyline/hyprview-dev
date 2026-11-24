#pragma once

#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/workspace/RegularWorkspace.hpp>

#include <string>

namespace hyprview::compat {

inline PHLWORKSPACE wsById(int id) {
    auto found = State::workspaceState()->filter(std::to_string(id));
    return found.empty() ? PHLWORKSPACE{} : found.front();
}

inline PHLWORKSPACE wsCreate(int id, const PHLMONITOR& monitor, bool empty = true) {
    return State::workspaceState()->createNumbered(
        Workspace::SWorkspaceNumberedID{static_cast<uint32_t>(id)}, monitor, {}, empty);
}

inline bool wsSpecial(const PHLWORKSPACE& ws) {
    return ws && ws->type() == Workspace::eWorkspaceType::SPECIAL;
}

inline int wsId(const PHLWORKSPACE& ws) {
    if (!ws)
        return -1;
    const auto numbered = ws->numberedID();
    return numbered ? static_cast<int>(*numbered) : -1;
}

inline void wsSetPersistent(const PHLWORKSPACE& ws, bool persistent) {
    if (const auto regular = Hyprutils::Memory::dynamicPointerCast<Workspace::CRegularWorkspace>(ws))
        regular->setPersistent(persistent);
}

}

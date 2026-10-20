#include "window_source.hpp"

#include "title_text.hpp"

#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/helpers/WLClasses.hpp>

namespace hyprview::model {

namespace {

std::string classBase(const std::string &cls) {
    const auto dot = cls.find('.');
    return dot == std::string::npos ? cls : cls.substr(0, dot);
}


} // namespace

PHLWINDOW WindowsContentSource::windowOf(SubjectRef s) const {
    // Epoch must be 0: a non-zero epoch means the browser issued this id, and treating it as
    // an address would read whatever the number happens to point at.
    if (s.epoch != 0)
        return {};
    for (const auto &w : m_alive)
        if (w.get() == reinterpret_cast<Desktop::View::CWindow *>(s.id))
            return w; // a copy: the refcount goes up, so the caller owns it as well
    return {};
}

std::vector<SubjectRef> WindowsContentSource::enumerate() {
    // Rebuilt, never appended: an identity from a previous enumeration must stop resolving
    // the moment the list moves on, or "gone" and "still there" become indistinguishable.
    m_alive.clear();
    std::vector<SubjectRef> out;
    if (!m_host.monitor())
        return out;
    for (const auto &w : Desktop::windowState()->windows()) {
        // EVERY window is held; only the belonging ones are returned as subjects. The run
        // draws the strip of EVERY workspace and a thumbnail resolves its window through
        // this list, so the belonging subset alone left those cards on bare wallpaper.
        m_alive.push_back(w);
        if (!m_host.belongs(w))
            continue;
        out.push_back(SubjectRef{reinterpret_cast<std::uintptr_t>(w.get()), 0});
    }
    return out;
}

GroupRef WindowsContentSource::groupOf(SubjectRef s) const {
    const auto w = windowOf(s);
    if (!w)
        return {};
    const auto ws = w->m_workspace;
    if (!ws)
        return {};
    return GroupRef{reinterpret_cast<std::uintptr_t>(ws.get()), 0};
}

std::string WindowsContentSource::label(SubjectRef s) const {
    const auto w = windowOf(s);
    if (!w)
        return {};
    auto text = w->m_title;
    if (text.empty())
        text = classBase(w->m_class);
    return truncateTitle(text);
}

bool WindowsContentSource::isTab(SubjectRef) const {
    return false; // by construction: windowOf refuses anything with an epoch
}

bool WindowsContentSource::activate(SubjectRef s) {
    // Deliberately does NOT focus: the compositor decides focus every frame, and mid-run focus
    // fights the run's hide-everything rule. Reports the subject the run may commit to on exit.
    return windowOf(s) != nullptr;
}

bool WindowsContentSource::close(SubjectRef s) {
    const auto w = windowOf(s);
    if (!w)
        return false;
    // No flight here: the card flies out BEFORE this is called and the caller draws it -- a
    // source that animated would have to know where the card is.
    w->sendClose();
    return true;
}

bool WindowsContentSource::moveTo(SubjectRef s, GroupRef target) {
    const auto w = windowOf(s);
    if (!w)
        return false;
    if (target.epoch != 0)
        return false; // a browser window's group is not a Hyprland workspace
    const auto ws = PHLWORKSPACE{reinterpret_cast<CWorkspace *>(target.id)};
    if (!ws)
        return false;
    Desktop::globalWindowController()->moveWindowToWorkspace(w, ws);
    return true;
}

} // namespace hyprview::model

#include "window_source.hpp"

#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/helpers/WLClasses.hpp>

namespace gloview::model {

namespace {

// A dotclass suffix is not part of the name a human would use.
std::string classBase(const std::string &cls) {
    const auto dot = cls.find('.');
    return dot == std::string::npos ? cls : cls.substr(0, dot);
}

constexpr std::size_t kMaxLabel = 80;

} // namespace

PHLWINDOW WindowsContentSource::windowOf(SubjectRef s) const {
    // Epoch must be 0. A non-zero epoch means this id was issued by a source that owns
    // epochs -- the browser -- and treating that as an address would be reading whatever the
    // number happens to point at.
    if (s.epoch != 0)
        return {};
    for (const auto &w : m_alive)
        if (w.get() == reinterpret_cast<Desktop::View::CWindow *>(s.id))
            return w; // a copy: the refcount goes up, so the caller owns it as well
    // Not in the last enumeration. Stale, never enumerated, or gone -- null either way.
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
        // EVERY window is held, and only the belonging ones are returned as subjects. Those
        // are different lists for a reason: the run draws the strip of EVERY workspace, and a
        // thumbnail there resolves its window through this same list. Filling it with the
        // belonging subset alone left every card but the current workspace's showing the bare
        // wallpaper, because the window it wanted had never been held by anyone.
        //
        // Holding a reference is not the same as showing it, so this costs nothing but memory
        // and grants a window no place on the run.
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
    if (text.size() > kMaxLabel)
        text = text.substr(0, kMaxLabel - 1) + "…";
    return text;
}

bool WindowsContentSource::isTab(SubjectRef) const {
    return false; // by construction: windowOf refuses anything with an epoch
}

bool WindowsContentSource::activate(SubjectRef s) {
    // Deliberately does NOT focus.
    //
    // A tab CAN be activated from here -- the extension switches to it -- but a window
    // cannot: the overlay is still up, the compositor decides focus every frame, and
    // focusing mid-run fights the run's own hide-everything rule. So this only reports that
    // the subject is one the run may commit to on exit, and the run records that intent.
    //
    // This asymmetry is the domain talking, not an oversight: "make this active" is a
    // remote operation for a tab and a deferred one for a window.
    return windowOf(s) != nullptr;
}

bool WindowsContentSource::close(SubjectRef s) {
    const auto w = windowOf(s);
    if (!w)
        return false;
    // No flight here either. The card flies out BEFORE this is called and the caller draws
    // it -- a source that animated would have to know where the card is, which is the run's
    // business, and a tab has no such card to fly.
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

} // namespace gloview::model
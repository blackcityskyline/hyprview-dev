#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

#include "content_source.hpp"
#include "window_host.hpp"

namespace gloview::model {

// Windows as subjects, the first of the two implementations. A window IS its address and the
// epoch is always 0, which is why a tabs SubjectRef passed to this source cannot be mistaken
// for a window: it would need an id that happens to be a valid pointer AND an epoch of 0.
// Holds nothing but what a window is and what you can do to one; layout, drag and exit ask.
class WindowsContentSource final : public IContentSource {
  public:
    explicit WindowsContentSource(const IWindowHost &host) : m_host(host) {}

    // All or none. Refreshes what this source holds alive; see IContentSource::enumerate.
    [[nodiscard]] std::vector<SubjectRef> enumerate() override;

    // The window behind an identity from the LAST enumeration, or null. Never rebuilt from
    // the id: that is a use-after-free wearing working code's clothes and it took the
    // compositor down once (CSharedPointer<CWindow>::_delete -> ~CSharedPointer ->
    // Overview::buildTiles). Wrong epoch and stale identity are both null.
    [[nodiscard]] PHLWINDOW windowOf(SubjectRef s) const;

    // At run end: holding a window alive past its own run keeps it out of the compositor's
    // teardown, a leak the plugin cannot explain.
    void release() { m_alive.clear(); }

    [[nodiscard]] GroupRef groupOf(SubjectRef s) const override;

    // Title, falling back to class. Trailing dots in the class ("kitty.kdots") are a
    // convention, not part of what a human calls the window.
    [[nodiscard]] std::string label(SubjectRef s) const override;

    [[nodiscard]] bool isTab(SubjectRef s) const override;

    bool activate(SubjectRef s) override;
    bool close(SubjectRef s) override;
    bool moveTo(SubjectRef s, GroupRef target) override;

  private:
    const IWindowHost &m_host;
    // Strong references taken at the last enumerate(); windowOf hands out copies. The entire
    // ownership story for a window identity.
    std::vector<SP<Desktop::View::CWindow>> m_alive;
};

} // namespace gloview::model
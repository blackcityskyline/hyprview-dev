#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

#include "content_source.hpp"
#include "window_host.hpp"

namespace gloview::model {

// Windows as subjects. The first of the two implementations.
//
// Identity: a window IS its address, and the epoch is always 0 because the compositor never
// issues one. That encoding is the whole reason a tabs SubjectRef can be handed to this
// source without being mistaken for a window -- it would need an id that happens to be a
// valid pointer and an epoch of 0, and the extension has no reason to produce those.
//
// Note what this does NOT hold: the tile layout, the drag state, the exit machinery. The
// source knows what a window is and what you can do to one. Everything about the run asks it.
class WindowsContentSource final : public IContentSource {
  public:
    explicit WindowsContentSource(const IWindowHost &host) : m_host(host) {}

    // All of them, or none of them. Refreshes what this source holds alive -- see the note
    // on IContentSource::enumerate for why that is not a const operation.
    [[nodiscard]] std::vector<SubjectRef> enumerate() override;

    // The window behind an identity FROM THE LAST ENUMERATION, or null. Never a pointer
    // rebuilt from the id: that is a use-after-free wearing working code's clothes, and it
    // took the compositor down once --
    //   CSharedPointer<CWindow>::_delete -> ~CSharedPointer -> Overview::buildTiles
    // A SubjectRef whose epoch is not 0 is not a window at all, and a stale identity is not
    // one either. Both are null, which is the only honest answer for "I do not have it".
    [[nodiscard]] PHLWINDOW windowOf(SubjectRef s) const;

    // Forgets everything. Called when the run ends: holding a window alive past its own run
    // would keep it out of the compositor's teardown, which is a leak the plugin does not get
    // to explain.
    void release() { m_alive.clear(); }

    [[nodiscard]] GroupRef groupOf(SubjectRef s) const override;

    // The window's title, falling back to its class, which is what the caption has always
    // shown. Trailing dots in the class ("kitty.kdots") are a window class convention, not
    // part of what a human calls the window.
    [[nodiscard]] std::string label(SubjectRef s) const override;

    [[nodiscard]] bool isTab(SubjectRef s) const override;

    bool activate(SubjectRef s) override;
    bool close(SubjectRef s) override;
    bool moveTo(SubjectRef s, GroupRef target) override;

  private:
    const IWindowHost &m_host;
    // Strong references taken at the last enumerate(). windowOf hands out COPIES, so the
    // caller owns what it got too. This is the entire ownership story for a window identity.
    std::vector<SP<Desktop::View::CWindow>> m_alive;
};

} // namespace gloview::model
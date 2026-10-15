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

    // All of them, or none of them.
    //
    // A SubjectRef whose epoch is not 0 is not a window, whatever its id, and windowOf
    // refuses it rather than casting a tab id to a pointer. Nothing in the plugin produces
    // such a ref today; that is the point of checking.
    [[nodiscard]] static PHLWINDOW windowOf(SubjectRef s);

    [[nodiscard]] std::vector<SubjectRef> enumerate() const override;

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
};

} // namespace gloview::model
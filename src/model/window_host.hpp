#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace gloview::model {

// What the window source needs to ask the run in order to enumerate.
//
// This is the inbound half of the same boundary IContentSource is the outbound half of, and
// it is a separate interface because the two callers are not the same shape. A tabs source
// needs NOTHING from the run: it owns the tab list the extension sent. A windows source
// cannot know which windows belong -- that rule is the run's, it reads the monitor, the
// displayed workspace and the all-workspaces mode, and every one of those changes mid-run.
//
// One interface carrying both directions would have forced the tabs source to implement a
// question it cannot answer, and the usual result of that is returning a default that
// silently means "yes" -- which is the failure this whole boundary exists to prevent.
//
// Narrow on purpose: the verbs are NOT here. A source can close a window and move a window
// with Hyprland calls alone (verified by compiling an implementation that does), so asking
// the run "how do I close" would be handing back a job that never needed doing.
class IWindowHost {
  public:
    virtual ~IWindowHost() = default;

    // Does this window get a tile? The one rule, shared by buildTiles() and syncTiles() --
    // they MUST agree or a window that is on screen has no card to click.
    [[nodiscard]] virtual bool belongs(const PHLWINDOW &w) const = 0;

    // The monitor the run is laid out in. Enumeration is scoped to it.
    [[nodiscard]] virtual PHLMONITOR monitor() const = 0;
};

} // namespace gloview::model
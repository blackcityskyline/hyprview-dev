#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace gloview::model {

// What the window source asks the run to enumerate. The inbound half of IContentSource's
// outbound half, and separate because the two callers differ: a tabs source owns the tab list
// the extension sent and needs nothing from the run; which windows belong is the run's rule and
// it changes mid-run. Shared, the tabs source would return a default meaning "yes" -- the
// failure this boundary exists to prevent. Narrow: the verbs are not here, an implementation
// closes and moves windows with Hyprland calls alone.
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
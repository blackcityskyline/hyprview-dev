#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace gloview::model {

// Which windows belong is the run's rule and changes mid-run: a default here would mean "yes".
class IWindowHost {
  public:
    virtual ~IWindowHost() = default;

    // One rule, shared by buildTiles() and syncTiles(): they MUST agree or a window that is on
    // screen has no card to click.
    [[nodiscard]] virtual bool belongs(const PHLWINDOW &w) const = 0;

    // The monitor the run is laid out in. Enumeration is scoped to it.
    [[nodiscard]] virtual PHLMONITOR monitor() const = 0;
};

} // namespace gloview::model

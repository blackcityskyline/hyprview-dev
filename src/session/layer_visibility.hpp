#pragma once

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "layer_names.hpp"

namespace gloview::session {

// Fading the bars and popups to 0 so they do not bleed through the translucent backdrop,
// and putting them back on the way out.
//
// The pair is the invariant, not the convenience: hide() without restore() leaves the
// user's taskbar invisible for the rest of the session, and nothing else in the plugin
// would notice. The saved alphas live here so the two cannot be called apart by accident --
// the destructor restores.
//
// The wallpaper is NOT chrome and is never in this list. It is the one thing that keeps the
// desktop from being an empty black buffer on the first frame: Hyprland renders it at alpha
// 0 into the monitor framebuffer and the opaque entry base goes down on top of nothing,
// which is the entry blackout this exclusion prevents.
class LayerVisibility {
  public:
    LayerVisibility() = default;
    ~LayerVisibility();

    LayerVisibility(const LayerVisibility &)            = delete;
    LayerVisibility &operator=(const LayerVisibility &) = delete;

    // Read the config at the call site, every frame (2.16), so an edit applies to the next
    // open rather than to a captured copy.
    void hide(const PHLMONITOR &m, bool hideTop, bool hideOverlay,
              const std::string &aboveNamespaces);

    void restore();

    [[nodiscard]] bool anyHidden() const { return !m_hidden.empty(); }
    [[nodiscard]] std::size_t hiddenCount() const { return m_hidden.size(); }

  private:
    std::vector<std::pair<PHLLSREF, float>> m_hidden; // + pre-hide alpha
};

} // namespace gloview::session
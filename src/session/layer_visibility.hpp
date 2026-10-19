#pragma once

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "layer_names.hpp"

namespace hyprview::session {

// hide() and restore() are a PAIR: hide alone leaves the taskbar invisible for the rest of the session,
// and only the saved alphas can put it back -- hence the destructor restores.
class LayerVisibility {
  public:
    LayerVisibility() = default;
    ~LayerVisibility();

    LayerVisibility(const LayerVisibility &)            = delete;
    LayerVisibility &operator=(const LayerVisibility &) = delete;

    // Config read at the call site, every frame (AGENTS.md 2.16).
    void hide(const PHLMONITOR &m, bool hideTop, bool hideOverlay,
              const std::string &aboveNamespaces);

    void restore();

    [[nodiscard]] bool anyHidden() const { return !m_hidden.empty(); }
    [[nodiscard]] std::size_t hiddenCount() const { return m_hidden.size(); }

  private:
    std::vector<std::pair<PHLLSREF, float>> m_hidden; // + pre-hide alpha
};

}

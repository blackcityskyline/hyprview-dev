#pragma once

#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <vector>

#include "../model/strip.hpp"

namespace hyprview::session {

// Makes windows on other workspaces drawable into a preview: the compositor reads three things
// (ws alpha, fullscreen alpha, surface visibleRegion), and all three say "no" off the active ws.
class PreviewVisibility {
  public:
    // `winFor` resolves a tile to its window; null (a tab) is the RIGHT answer -- nothing in Hyprland to hide behind.
    template <class WinFor>
    void force(const std::vector<model::Tile> &tiles,
               const std::vector<model::StripItem> &strip, WinFor &&winFor);
    void restore();

    [[nodiscard]] bool active() const {
      return !m_ws.empty() || !m_wins.empty() || !m_regions.empty();
    }

  private:
    struct ForcedWinVis {
      PHLWINDOWREF w;
      float alphaVal;
    };
    struct ForcedRegion {
      SP<Desktop::View::CWLSurface> hl;
      CRegion region;
    };

    std::vector<PHLWORKSPACEREF> m_ws;
    std::vector<ForcedWinVis>  m_wins;
    std::vector<ForcedRegion>  m_regions;
};

}

#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <vector>

#include "../model/strip.hpp"

namespace gloview::session {

// Makes windows on other workspaces drawable into a preview, and puts every touched
// value back.
//
// The compositor decides what to draw from three pieces of state, and for a window that is
// not on the ACTIVE workspace all three say "no": the workspace is faded out, the window's
// fullscreen alpha is 0, and the window's surfaces carry a visibleRegion left over from
// whenever they were last on screen. The live preview then reads a resource with no
// committed buffer and a geometry that was never given the layout.
//
// ScrollOverview clears all three before drawing a preview and puts them back afterwards.
// Same three, same calls -- forceWorkspaceAlphaVisible / forceWindowVisible /
// forceWindowSurfaceVisibility.
//
// The saved values are what make force/restore a PAIR: calling either alone leaves the
// desktop wrong for the rest of the session, and nothing else in the plugin notices.
class PreviewVisibility {
  public:
    void force(const std::vector<model::Tile>  &tiles,
               const std::vector<model::StripItem> &strip);
    void restore();

    [[nodiscard]] bool active() const {
      return !m_ws.empty() || !m_wins.empty() || !m_regions.empty();
    }

  private:
    struct ForcedWsAlpha {
      PHLWORKSPACEREF ws;
      float val;
    };
    struct ForcedWinVis {
      PHLWINDOWREF w;
      float alphaVal;
    };
    struct ForcedRegion {
      SP<Desktop::View::CWLSurface> hl;
      CRegion region;
    };

    std::vector<ForcedWsAlpha> m_ws;
    std::vector<ForcedWinVis>  m_wins;
    std::vector<ForcedRegion>  m_regions;
};

} // namespace gloview::session
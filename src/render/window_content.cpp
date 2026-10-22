#include "window_content.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>

#include "../overview.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "gl_util.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {

// Real opacity: active/inactive rule x FULLSCREEN/LAYOUT fade; the ws-switch and FADE alphas
// are excluded because they compound with the caller's fade.
float windowRealAlpha(const PHLWINDOW &w, const PHLMONITOR &mon) {
  if (!w)
    return 1.0F;
  float active = 1.0F;
  if (w->m_ruleApplicator) {
    static auto PACTIVEALPHA =
        CConfigValue<Config::FLOAT>("decoration:active_opacity");
    static auto PINACTIVEALPHA =
        CConfigValue<Config::FLOAT>("decoration:inactive_opacity");
    static auto PFULLSCREENALPHA =
        CConfigValue<Config::FLOAT>("decoration:fullscreen_opacity");
    if (Fullscreen::controller()->isFullscreen(w,
                                               Fullscreen::FSMODE_FULLSCREEN)) {
      active =
          w->m_ruleApplicator->alphaFullscreen().valueOrDefault().applyAlpha(
              *PFULLSCREENALPHA);
    } else {
      const bool reallyFocused = mon &&
                                 w->m_workspace == mon->m_activeWorkspace &&
                                 w == Desktop::focusState()->window();
      active = reallyFocused
                   ? w->m_ruleApplicator->alpha().valueOrDefault().applyAlpha(
                         *PACTIVEALPHA)
                   : w->m_ruleApplicator->alphaInactive()
                         .valueOrDefault()
                         .applyAlpha(*PINACTIVEALPHA);
    }
    if (w->m_ruleApplicator->opaque().valueOrDefault())
      active = 1.0F;
  }
  const float fade = w->alphaValue(Desktop::View::WINDOW_ALPHA_FULLSCREEN) *
                     w->alphaValue(Desktop::View::WINDOW_ALPHA_LAYOUT);
  return std::clamp(active * fade, 0.0F, 1.0F);
}

}

// roundPx is the CONTENT radius in monitor px; destPx must carry the window's own aspect.
void renderWindowLive(const PHLWINDOW &w, const PHLMONITOR &mon,
                      const CBox &destPx, const CBox &clipPx, float alpha,
                      const Time::steady_tp &when, int roundPx,
                      float roundingPower, bool settled, bool contain) {
  if (!w || !mon || !w->m_isMapped || !w->wlSurface() ||
      !w->wlSurface()->resource())
    return;
  if (!(destPx.w > 0 && destPx.h > 0))
    return;

  w->wlSurface()->m_fillIgnoreSmall = true;

  // goal(), not value(): value() mid-resize scales only part of destPx.
  const auto pos  = w->positionAnimation()->goal();
  const auto size = w->sizeAnimation()->goal();
  const Vector2D logicalTL = pos + w->m_floatingOffset;

  const render::WindowFitInput fitIn{
      destPx.x, destPx.y, destPx.w, destPx.h,
      logicalTL.x, logicalTL.y, size.x, size.y,
      mon->m_position.x, mon->m_position.y, mon->m_scale,
      settled, contain, kSeamPadPx};
  const auto fit = render::windowFit(fitIn);
  if (!fit.valid)
    return;
  const auto box = render::previewBox(fitIn, fit);
  if (!(box.w > 0 && box.h > 0))
    return;

  CSurfacePassElement::SRenderData data{};
  data.pMonitor = mon;
  data.when     = when;
  data.pos      = Vector2D{box.x, box.y};
  data.w        = box.w;
  data.h        = box.h;
  data.surface  = w->wlSurface()->resource();
  data.dontRound      = false;
  // The window's own rule opacity; the surface's alphaModifier and overallOpacity still apply on top.
  data.fadeAlpha = windowRealAlpha(w, mon);
  data.alpha          = std::clamp(alpha, 0.F, 1.F);
  data.decorate       = false;
  data.rounding       = roundPx;
  data.roundingPower  = roundingPower;
  data.blur           = false;
  // No pWindow: calculateUVForSurface crops to RATIO = box / reported size
  // (ElementRenderer.cpp:98) instead of stretching.
  data.pWindow        = nullptr;
  data.clipBox        = clipPx;
  data.squishOversized = true;
  data.surfaceCounter = 0;

  if (g_overview && g_overview->snapshotMode()) {
    if (const auto snap = g_overview->snapshot(w); snap && snap->ok()) {
      auto sdata      = data;
      sdata.texture   = snap;
      sdata.localPos  = Vector2D{};
      sdata.mainSurface = true;
      g_pHyprRenderer->draw(sdata, g_pHyprRenderer->m_renderData.damage);
      return;
    }
  }

  w->wlSurface()->resource()->breadthfirst(
      [&data, &w, &fit](SP<CWLSurfaceResource> s, const Vector2D &offset,
                                 void *) {
        if (!s || !s->m_current.texture || s->m_current.size.x < 1 ||
            s->m_current.size.y < 1)
          return;
        // ponytail: the offset is fitted, the box Hyprland derives for a subsurface from its resource size is not -- a popup previews at real size.
        data.localPos    = offset * fit.scale;
        data.texture     = s->m_current.texture;
        data.surface     = s;
        data.mainSurface = s == w->wlSurface()->resource();
        g_pHyprRenderer->draw(data, g_pHyprRenderer->m_renderData.damage);
        data.surfaceCounter++;
      },
      nullptr);
}

}

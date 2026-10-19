#include "layer_visibility.hpp"

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace gloview::session {

LayerVisibility::~LayerVisibility() { restore(); }

void LayerVisibility::hide(const PHLMONITOR &m, bool hideTop, bool hideOverlay,
                           const std::string &aboveNamespaces) {
  if (!m || (!hideTop && !hideOverlay))
    return;
  const auto fade = [this, &m, &aboveNamespaces](
                        const std::vector<PHLLSREF> &layer) {
    for (const auto &ref : layer) {
      const auto ls = ref.lock();
      if (!ls)
        continue;
      if (ls->wlSurface() && ls->wlSurface()->resource() &&
          ls->wlSurface()->resource()->m_current.texture.get() ==
              m->m_background.get())
        continue;
      auto &a = ls->alpha()[Desktop::View::LS_ALPHA_FADE];
      if (!a)
        continue;
      if (isAboveLayer(ls->m_namespace, aboveNamespaces))
        continue;
      m_hidden.emplace_back(ref, a->goal());
      *a = 0.F;
    }
  };
  if (hideTop)
    fade(m->m_layerSurfaceLayers[2]); // ZWLR_LAYER_SHELL_V1_LAYER_TOP
  if (hideOverlay)
    fade(m->m_layerSurfaceLayers[3]); // ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
}

void LayerVisibility::restore() {
  for (auto &[ref, alpha] : m_hidden)
    if (const auto ls = ref.lock(); ls) {
      auto &a = ls->alpha()[Desktop::View::LS_ALPHA_FADE];
      if (a)
        *a = alpha;
    }
  m_hidden.clear();
}

}

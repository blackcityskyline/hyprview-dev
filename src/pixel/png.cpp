#include "png.hpp"

#include <hyprland/src/render/Renderer.hpp>

namespace gloview::pixel {

SP<Render::ITexture> loadPng(const std::string &path) {
    if (!g_pHyprRenderer)
        return nullptr;
    auto *surf = readPngSurface(path);
    if (!surf)
        return nullptr;
    // createTexture copies what it needs from the surface, so the surface dies here. That is
    // assumed, not verified: nothing in the headers says otherwise, and a cairo upload that
    // aliased the surface would leave this reading freed memory. Worth a check if a preview
    // ever comes out as noise.
    auto tex = g_pHyprRenderer->createTexture(surf);
    cairo_surface_destroy(surf);
    return tex;
}

} // namespace gloview::pixel

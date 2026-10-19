#include "png.hpp"

#include <hyprland/src/render/Renderer.hpp>

namespace gloview::pixel {

SP<Render::ITexture> loadPng(const std::string &path) {
    if (!g_pHyprRenderer)
        return nullptr;
    auto *surf = readPngSurface(path);
    if (!surf)
        return nullptr;
    // createTexture copies from the surface, so it dies here -- assumed, not verified by the headers.
    auto tex = g_pHyprRenderer->createTexture(surf);
    cairo_surface_destroy(surf);
    return tex;
}

} // namespace gloview::pixel

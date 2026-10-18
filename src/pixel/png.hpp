#pragma once

// A PNG on disk becomes a texture. NOT g_pHyprRenderer->loadAsset(): that resolves against
// Hyprland's asset dirs (`<assetdir>/hypr/` + name, Renderer.cpp:1477), so a runtime path can
// never go through it. cairo reads the file and the renderer takes the surface -- no new
// dependency, cairo is already linked in. This is the one line that needs a GPU; the read
// lives in png_read.hpp so a check can exercise the file cases with no compositor.

#include "png_read.hpp"

#include <hyprland/src/render/Texture.hpp>

#include <string>

namespace gloview::pixel {

// The image as a texture, or nullptr on an unreadable file and on a missing renderer: a
// plugin outliving its GL context must not chase a null pointer.
[[nodiscard]] SP<Render::ITexture> loadPng(const std::string &path);

} // namespace gloview::pixel

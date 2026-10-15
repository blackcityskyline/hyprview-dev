#pragma once

// A PNG on disk becomes a texture.
//
// NOT g_pHyprRenderer->loadAsset(). That resolves its argument against Hyprland's own asset
// directories (`<assetdir>/hypr/` + the name, Renderer.cpp:1477), so a runtime path can never
// be loaded through it. cairo reads the file and Hyprland's renderer takes the surface from
// there -- which is why this needs no new dependency: cairo is already linked into this
// plugin (ldd build/gloview.so).
//
// Split in two: readPngSurface() in png_read.hpp touches nothing but the filesystem, so a
// check can exercise it with no compositor; this is the one line that needs a GPU. One
// function would make the interesting half -- what happens when the file is missing,
// truncated, or not a PNG -- untestable, and those are the cases that actually happen.

#include "png_read.hpp"

#include <hyprland/src/render/Texture.hpp>

#include <string>

namespace gloview::pixel {

// The image as a texture, or nullptr. Null when the file could not be read AND when there is
// no renderer: a plugin that outlives its GL context must not reach through a null pointer on
// its way to a texture.
[[nodiscard]] SP<Render::ITexture> loadPng(const std::string &path);

} // namespace gloview::pixel

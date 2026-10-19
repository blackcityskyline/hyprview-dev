#pragma once

// PNG -> texture via cairo (AGENTS.md 2.21); NEVER loadAsset(), which resolves <assetdir>/hypr/ (Renderer.cpp:1477).

#include "png_read.hpp"

#include <hyprland/src/render/Texture.hpp>

#include <string>

namespace hyprview::pixel {

[[nodiscard]] SP<Render::ITexture> loadPng(const std::string &path);

} // namespace hyprview::pixel

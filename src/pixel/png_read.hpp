#pragma once

// Reading a PNG off disk: cairo and the filesystem only, which makes this half checkable (2.8).

#include <cairo.h>

#include <string>

namespace gloview::pixel {

// nullptr for no file, not-a-PNG, truncated or unreadable alike; caller destroys with cairo_surface_destroy().
[[nodiscard]] cairo_surface_t *readPngSurface(const std::string &path);

} // namespace gloview::pixel

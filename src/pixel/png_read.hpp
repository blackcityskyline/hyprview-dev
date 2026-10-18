#pragma once

// Reading a PNG off disk: cairo and the filesystem only, which is what makes this half
// checkable (AGENTS.md 2.8: a file ctest compiles without hyprland/ never gets it included).

#include <cairo.h>

#include <string>

namespace gloview::pixel {

// The decoded image, or nullptr. One null for no file, not-a-PNG, truncated and unreadable:
// the caller has one correct response for all four, and per-failure behaviour would be a
// distinction it cannot act on. Caller destroys with cairo_surface_destroy().
[[nodiscard]] cairo_surface_t *readPngSurface(const std::string &path);

} // namespace gloview::pixel

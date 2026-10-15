#pragma once

// Reading a PNG off disk. No compositor, no renderer, no GPU -- just cairo and the
// filesystem, which is what makes this half of the job checkable (AGENTS.md 2.8: a file
// ctest compiles without hyprland/ never gets hyprland/ included).

#include <cairo.h>

#include <string>

namespace gloview::pixel {

// The decoded image, or nullptr. Null covers every failure there is, deliberately and without
// distinguishing them: no file, not a PNG, truncated, or a PNG cairo could not read. The
// caller has one correct response for all four -- draw the next thing down -- and choosing a
// different behaviour per failure would be inventing distinctions it cannot act on.
//
// The caller owns the surface and destroys it with cairo_surface_destroy().
[[nodiscard]] cairo_surface_t *readPngSurface(const std::string &path);

} // namespace gloview::pixel

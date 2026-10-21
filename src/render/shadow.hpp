#pragma once

#include <cstdint>

struct _cairo_surface;

namespace hyprview::render {

// A drop shadow is the silhouette, blurred: no shader and no global render knob, so its size and
// its softness are arguments. Hyprland's renderRoundedShadow takes its falloff from
// decoration:shadow:render_power, which is one number for every window on the desktop.
struct ShadowSpec {
  double dy = 0.0;      // logical px, down is positive: a light from above
  double blur = 6.0;    // logical px, the falloff reaches about 1.5x this on every side
  double spread = 0.0;  // logical px, the silhouette grows before it is blurred
  double r = 0.0;       // colour is baked into the texture: renderTexture has no tint
  double g = 0.0;
  double b = 0.0;
};

// The icon's shadow: its units are shares of the icon's side, so it grows with the icon.
[[nodiscard]] inline ShadowSpec iconShadowSpec(double side, double offset, double blur) {
  return ShadowSpec{.dy = side * offset, .blur = side * blur};
}

// Padding, device px: the drawn box is this much larger on every side. Same inputs, same answer
// as the mask, or the shadow would be drawn against a texture that ends before it fades out.
[[nodiscard]] int shadowPad(const ShadowSpec &spec, double scale);

// A blurred rounded rect of w x h device px with corner radius round at full alpha: the caller
// applies intensity when drawing. Caller owns the surface.
[[nodiscard]] _cairo_surface *shadowMask(const ShadowSpec &spec, int w, int h, int round,
                                         double scale);

} // namespace hyprview::render

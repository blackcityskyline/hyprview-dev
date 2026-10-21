// The shadow is the silhouette blurred. Two things can go wrong silently: the blur is skipped (a
// hard ring, which is what the badge had) and the colour lands in the wrong channel (blue icons
// with an orange shadow). Both are visible in the mask itself, so no GL is needed.

#include <cstdio>
#include <string>
#include <vector>

#include <cairo.h>

#include "../src/render/shadow.hpp"

using hyprview::render::ShadowSpec;
using hyprview::render::shadowMask;
using hyprview::render::shadowPad;

static int failures = 0;

static void want(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

int main() {
  const ShadowSpec spec{.dy = 0.0, .blur = 6.0, .spread = 0.0, .r = 0.2, .g = 0.4, .b = 0.8};
  const int pad = shadowPad(spec, 1.0);
  const int W = 40 + 2 * pad;
  cairo_surface_t *mask = shadowMask(spec, 40, 40, 10, 1.0);
  if (!mask || cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
    std::fprintf(stderr, "FAIL: no mask\n");
    return 1;
  }
  want(cairo_image_surface_get_width(mask) == W, "mask spans the box plus the pad");

  const int stride = cairo_image_surface_get_stride(mask);
  const unsigned char *data = cairo_image_surface_get_data(mask);
  const auto px = [&](int x, int y) { return y * stride + x * 4; };
  const int midY = W / 2;
  const auto alphaAt = [&](int x) { return static_cast<int>(data[px(x, midY) + 3]); };

  want(alphaAt(0) <= 2, "the pad is fully transparent");
  want(alphaAt(W / 2) >= 253, "the silhouette centre is opaque");

  int steps = 0;
  int previous = alphaAt(0);
  for (int x = 0; x < W / 2; ++x) {
    const int a = alphaAt(x);
    if (a > 2 && a < 253)
      ++steps;
    want(a + 1 >= previous,
         "the falloff rises towards the silhouette at x=" + std::to_string(x));
    previous = a;
  }
  want(steps >= 8, "the falloff is a gradient, not an edge: " + std::to_string(steps) + " steps");

  const int c = px(W / 2, midY);
  want(data[c + 0] >= 200, "blue is the first byte of ARGB32");
  want(data[c + 1] >= 95 && data[c + 1] <= 110, "green is the second byte");
  want(data[c + 2] >= 45 && data[c + 2] <= 58, "red is the third byte");

  const ShadowSpec wide{.dy = 0.0, .blur = 12.0, .spread = 0.0, .r = 0.2, .g = 0.4, .b = 0.8};
  want(shadowPad(wide, 1.0) > pad, "a wider blur asks for more padding");
  want(shadowPad(wide, 2.0) > shadowPad(wide, 1.0), "a scale multiplies the device padding");

  cairo_surface_destroy(mask);
  if (failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("shadow_check: %d px pad, %d gradient steps\n", pad, steps);
  return 0;
}

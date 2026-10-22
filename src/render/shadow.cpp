#include "shadow.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <cairo.h>

namespace hyprview::render {

namespace {

// Three box passes through one buffer: the profile that comes out is a Gaussian to the eye, and
// the silhouette keeps its rounded corners because the blur runs on the alpha plane only.
void blurAlpha(std::vector<float> &a, int w, int h, int r) {
  if (r < 1 || w <= 0 || h <= 0)
    return;
  std::vector<float> tmp(a.size());
  const float norm = 1.0F / static_cast<float>(2 * r + 1);
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < h; ++y) {
      const int base = y * w;
      float sum = 0.0F;
      for (int x = -r; x <= r; ++x)
        sum += a[base + std::clamp(x, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        tmp[base + x] = sum * norm;
        sum -= a[base + std::clamp(x - r, 0, w - 1)];
        sum += a[base + std::clamp(x + r + 1, 0, w - 1)];
      }
    }
    for (int x = 0; x < w; ++x) {
      float sum = 0.0F;
      for (int y = -r; y <= r; ++y)
        sum += tmp[std::clamp(y, 0, h - 1) * w + x];
      for (int y = 0; y < h; ++y) {
        a[y * w + x] = sum * norm;
        sum -= tmp[std::clamp(y - r, 0, h - 1) * w + x];
        sum += tmp[std::clamp(y + r + 1, 0, h - 1) * w + x];
      }
    }
  }
}

void roundedRect(cairo_t *cr, double x, double y, double w, double h, double r) {
  r = std::clamp(r, 0.0, std::min(w, h) * 0.5);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0.0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
  cairo_close_path(cr);
}

int blurRadius(double scale, const ShadowSpec &spec) {
  return std::max(1, static_cast<int>(std::lround(spec.blur * scale * 0.5)));
}

}

int shadowPad(const ShadowSpec &spec, double scale) {
  const double reach = 3.0 * blurRadius(scale, spec) + spec.spread * scale +
                       std::abs(spec.dy) * scale;
  return static_cast<int>(std::ceil(reach)) + 1;
}

cairo_surface_t *shadowMask(const ShadowSpec &spec, int w, int h, int round, double scale) {
  w = std::max(1, w);
  h = std::max(1, h);
  const int pad = shadowPad(spec, scale);
  const int W = w + 2 * pad;
  const int H = h + 2 * pad;

  cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
  if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(mask);
    return nullptr;
  }
  cairo_t *cr = cairo_create(mask);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
  const double spread = spec.spread * scale;
  roundedRect(cr, pad - spread, pad - spread + spec.dy * scale, w + 2.0 * spread,
              h + 2.0 * spread, round + spread);
  cairo_fill(cr);
  cairo_destroy(cr);
  cairo_surface_flush(mask);

  const int stride = cairo_image_surface_get_stride(mask);
  const unsigned char *src = cairo_image_surface_get_data(mask);
  std::vector<float> alpha(static_cast<size_t>(W) * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      alpha[static_cast<size_t>(y) * W + x] = src[y * stride + x] / 255.0F;
  cairo_surface_destroy(mask);

  blurAlpha(alpha, W, H, blurRadius(scale, spec));

  cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(out);
    return nullptr;
  }
  unsigned char *dst = cairo_image_surface_get_data(out);
  const int outStride = cairo_image_surface_get_stride(out);
  for (int y = 0; y < H; ++y) {
    unsigned char *row = dst + y * outStride;
    for (int x = 0; x < W; ++x) {
      // ARGB32 is premultiplied, so the colour is scaled here: one texture serves every intensity.
      const float a = std::clamp(alpha[static_cast<size_t>(y) * W + x], 0.0F, 1.0F);
      row[x * 4 + 0] = static_cast<unsigned char>(std::lround(spec.b * a * 255.0));
      row[x * 4 + 1] = static_cast<unsigned char>(std::lround(spec.g * a * 255.0));
      row[x * 4 + 2] = static_cast<unsigned char>(std::lround(spec.r * a * 255.0));
      row[x * 4 + 3] = static_cast<unsigned char>(std::lround(a * 255.0));
    }
  }
  cairo_surface_mark_dirty(out);
  return out;
}

} // namespace hyprview::render

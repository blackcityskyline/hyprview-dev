#include "cache.hpp"

#include <algorithm>
#include <cmath>

#include "app_icon.hpp"
#include "png.hpp"

#include <hyprland/src/render/Renderer.hpp>

#ifdef HYPRVIEW_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

namespace hyprview::pixel {

namespace {

// U+2026, added by hand because renderText clips instead of ellipsizing.
constexpr char kEllipsis[] = "\xE2\x80\xA6";

} // namespace

SP<Render::ITexture> PixelCache::render(const std::string &s,
                                        const CHyprColor &col, int size) {
  if (!g_pHyprRenderer)
    return nullptr;
  return g_pHyprRenderer->renderText(s, col, size, false, "", 0,
                                     size >= 15 ? 700 : 600);
}

std::string PixelCache::truncate(const std::string &text,
                                 const CHyprColor &col, int size, int maxW) {
  if (maxW <= 0 || text.size() <= 4)
    return text;
  const auto probe = render(text, col, size);
  if (!probe || probe->m_size.x <= maxW)
    return text;
  // Binary search on the character count, not the width: a candidate costs a render.
  int lo = 1, hi = static_cast<int>(text.size()) - 1, best = 0;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const auto t =
        render(text.substr(0, static_cast<size_t>(mid)) + kEllipsis, col, size);
    if (t && t->m_size.x <= maxW) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best <= 0)
    return text;
  return text.substr(0, static_cast<size_t>(best)) + kEllipsis;
}

SP<Render::ITexture> PixelCache::windowLabel(const PHLWINDOW &win,
                                            const std::string &text,
                                            const CHyprColor &col, int size,
                                            int maxW) {
  return labelIn(m_winLabels, PHLWINDOWREF{win}, text, col, size, maxW);
}

namespace {
std::int64_t fileStamp(const std::string &path) {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(path, ec);
  return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}
} // namespace

SP<Render::ITexture> PixelCache::putPicture(const model::SubjectRef &s,
                                           const std::string &screenshotPath,
                                           const std::string &iconPath) {
  const auto stamp = fileStamp(screenshotPath);
  if (const auto *hit = m_subjectArt.find(s)) {
    const auto it = m_artStamp.find(s);
    if (it != m_artStamp.end() && it->second == stamp)
      return *hit;
  }
  auto tex = loadPng(screenshotPath);
  if (!tex && !iconPath.empty())
    tex = loadPng(iconPath);
  if (tex) {
    m_subjectArt.put(s, tex);
    m_artStamp[s] = stamp;
  }
  return tex;
}

SP<Render::ITexture> PixelCache::putIcon(const model::SubjectRef &s, const std::string &path) {
  const auto stamp = fileStamp(path);
  if (stamp == 0) {
    m_iconStamp[s] = 0;
    m_subjectIcon.erase(s);
    return nullptr;
  }
  const auto it = m_iconStamp.find(s);
  if (it != m_iconStamp.end() && it->second == stamp) {
    const auto hit = m_subjectIcon.find(s);
    if (hit != m_subjectIcon.end())
      return hit->second;
  }
  auto tex = loadPng(path);
  m_iconStamp[s] = stamp;
  if (tex)
    m_subjectIcon[s] = tex;
  else
    m_subjectIcon.erase(s);
  return tex;
}

namespace {

// A badge is at most 60 logical pixels; a rasteriser asked for more than 128 stores pixels no
// card can show.
constexpr int kAppIconPx = 128;

SP<Render::ITexture> loadSvg(const std::string &path, int side) {
#ifdef HYPRVIEW_HAVE_RSVG
  if (!g_pHyprRenderer)
    return nullptr;
  GError *err = nullptr;
  RsvgHandle *handle = rsvg_handle_new_from_file(path.c_str(), &err);
  if (!handle) {
    if (err)
      g_error_free(err);
    return nullptr;
  }
  double w = side;
  double h = side;
  if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &w, &h) || w <= 0.0 || h <= 0.0) {
    w = side;
    h = side;
  }
  const double k = side / std::max(w, h);
  const int tw = std::max(1, static_cast<int>(std::ceil(w * k)));
  const int th = std::max(1, static_cast<int>(std::ceil(h * k)));
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
  cairo_t *cr = cairo_create(surface);
  const RsvgRectangle viewport{0.0, 0.0, static_cast<double>(tw), static_cast<double>(th)};
  const bool drawn = rsvg_handle_render_document(handle, cr, &viewport, &err);
  cairo_destroy(cr);
  g_object_unref(handle);
  SP<Render::ITexture> tex;
  if (drawn) {
    cairo_surface_flush(surface);
    tex = g_pHyprRenderer->createTexture(surface);
  } else if (err)
    g_error_free(err);
  cairo_surface_destroy(surface);
  return tex;
#else
  (void)path;
  (void)side;
  return nullptr; // no rasteriser: an SVG-only app keeps no corner icon
#endif
}

} // namespace

SP<Render::ITexture> PixelCache::appIcon(const std::string &windowClass) {
  if (windowClass.empty())
    return nullptr;
  const auto hit = m_appIcons.find(windowClass);
  if (hit != m_appIcons.end())
    return hit->second;
  const auto path = app_icon::appIconFile(windowClass);
  auto tex = path.empty()                       ? SP<Render::ITexture>{}
             : path.ends_with(".svg")           ? loadSvg(path, kAppIconPx)
                                                : loadPng(path);
  m_appIcons[windowClass] = tex; // a miss is cached too: one lookup per class, not per frame
  return tex;
}

SP<Render::ITexture> PixelCache::picture(const model::SubjectRef &s) const {
  const auto *hit = m_subjectArt.find(s);
  return hit ? *hit : nullptr;
}

SP<Render::ITexture> PixelCache::subjectLabel(const model::SubjectRef &s,
                                             const std::string &text,
                                             const CHyprColor &col, int size,
                                             int maxW) {
  auto &e = *m_subjectLabels.slot(s);
  if (e.matches(text, maxW, size, col))
    return e.tex;
  e = LabelEntry{truncate(text, col, size, maxW), maxW, size, col, nullptr};
  e.tex = render(e.text, col, size);
  return e.tex;
}

SP<Render::ITexture> PixelCache::workspaceLabel(const PHLWORKSPACE &ws,
                                                 const std::string &text,
                                                 const CHyprColor &col, int size,
                                                 int maxW) {
  return labelIn(m_wsLabels, PHLWORKSPACEREF{ws}, text, col, size, maxW);
}

SP<Render::ITexture> PixelCache::virtualLabel(WORKSPACEID id,
                                               const std::string &text,
                                               const CHyprColor &col, int size,
                                               int maxW) {
  auto &e = m_virtualLabels[id];
  // Hit first: nothing to measure before the lookup.
  if (e.matches(text, maxW, size, col))
    return e.tex;
  e = LabelEntry{truncate(text, col, size, maxW), maxW, size, col, nullptr};
  e.tex = render(e.text, col, size);
  return e.tex;
}

SP<Render::ITexture> PixelCache::fixedLabel(Slot slot, const std::string &text,
                                             const CHyprColor &col, int size,
                                             int maxW) {
  auto &e = m_slotLabels[slot];
  if (e.matches(text, maxW, size, col))
    return e.tex;
  e = LabelEntry{truncate(text, col, size, maxW), maxW, size, col, nullptr};
  e.tex = render(e.text, col, size);
  return e.tex;
}

SP<Render::ITexture> PixelCache::glyph(const std::string &icon,
                                        const CHyprColor &col) {
  if (!m_glyph || m_glyphIcon != icon) {
    m_glyph = g_pHyprRenderer
                  ? g_pHyprRenderer->renderText(icon, col, 16, false, "", 0, 800)
                  : nullptr;
    m_glyphIcon = icon;
  }
  return m_glyph;
}

SP<Render::ITexture> PixelCache::closeGlyph() const { return m_glyph; }

void PixelCache::putSnapshot(const PHLWINDOW &win,
                             const SP<Render::ITexture> &tex) {
  const PHLWINDOWREF ref = win;
  const auto         key = win.get();
  if (!key)
    return;
  auto &slot = m_snapshots[key];
  if (slot.owner.expired())
    slot = {}; // the address has a new tenant
  slot.owner = ref;
  slot.tex = tex;
}

SP<Render::ITexture> PixelCache::snapshot(const PHLWINDOW &win) const {
  const auto key = win.get();
  if (!key)
    return nullptr;
  const auto it = m_snapshots.find(key);
  if (it == m_snapshots.end() || it->second.owner.expired())
    return nullptr;
  return it->second.tex;
}

} // namespace hyprview
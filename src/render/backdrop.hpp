#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "../blur.hpp"
#include "../config/config.hpp"

namespace gloview::render {

struct BlurCache {
  SP<Render::IFramebuffer> fb;
  const void *srcId = nullptr;
  // recipe is part of the key, checked every frame
  int passes = 0, sizePx = 0, resolution = 0;
  float strength = -1.0F;
  bool valid = false;
  [[nodiscard]] bool matches(const void *id, int p, int s, int r,
                             float st) const {
    return valid && srcId == id && passes == p && sizePx == s &&
           resolution == r && strength == st;
  }
  void invalidate() { valid = false; }
  void drop() {
    invalidate();
    fb.reset();
  }
};

// What the backdrop needs from the frame it is drawn into, with the blur recipe already
// clamped by the owner. Built fresh every frame, so a config edit applies in flight.
struct BackdropFrame {
  PHLMONITOR mon;
  double   eased = 0.0; // chrome opacity / backdrop progress
  double   dimK  = 0.0; // backdrop strength on the open/close clock
  CHyprColor baseCol{};
  bool  opening = false;
  bool  blurEnabled = false;
  int   passes = 0, sizePx = 0, resolution = 0;
  float strength = 0.0F;
};

// Wallpaper + BACKGROUND/BOTTOM layer surfaces, blurred, under the whole overlay.
// Owns every framebuffer, the blur cache and the source-identity tracking behind them.
class Backdrop {
  public:
    // Never samples currentFB: a fullscreen window on the active workspace renders
    // through the "solitary client" fast path, and an idle workspace leaves it stale.
    // Caller sets RPT_EXPORT and fbSize around it.
    [[nodiscard]] SP<Render::ITexture> source(const PHLMONITOR &m, int W,
                                              int H) const;

    // The adopted composite, or null while none has been accepted.
    [[nodiscard]] SP<Render::ITexture> composite() const {
      return m_srcFB && m_srcFB->isAllocated() ? m_srcFB->getTexture() : nullptr;
    }

    // Readable after the cache goes stale: `valid` gates RECOMPUTATION, the framebuffer
    // still holds the last good blur.
    [[nodiscard]] SP<Render::ITexture> blurTexture() const {
      return (m_blur.fb && m_blur.fb->isAllocated()) ? m_blur.fb->getTexture()
                                                     : nullptr;
    }
    [[nodiscard]] bool blurStale() const { return !m_blur.valid; }
    [[nodiscard]] bool drawn() const { return m_drawn; }

    // Same as source(), but owns the projection/fbSize save-restore that every caller
    // otherwise has to spell out identically.
    [[nodiscard]] SP<Render::ITexture> frameSource(const BackdropFrame &f) const;

    void render(const BackdropFrame &f) const;

  private:

    mutable BlurCache m_blur;
    mutable SP<Render::IFramebuffer> m_srcFB, m_scratchFB, m_probeFB;
    mutable bool m_drawn = false;
    // The layer texture the committed composite was made from. Compared on every open, so
    // the composite can be kept indefinitely instead of expiring on a timer: a TTL cannot
    // tell "the desktop changed" from "the wallpaper texture is mid-recreation right now".
    mutable const void *m_srcTex = nullptr;
    // A video wallpaper replaces its source texture every frame. Detected from consecutive
    // changes, so a one-off compositor re-creation of a static wallpaper is not mistaken
    // for it. Such a source is composited once and then held: one frozen frame of the
    // video, shown for the whole open.
    mutable const void *m_lastObservedTex = nullptr;
    mutable int m_srcAnimStreak = 0;
    mutable bool m_srcAnimated = false;
    mutable CBlurFilter m_blurFilter; // independent of decoration:blur:*
};

} // namespace gloview::render
#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "../blur.hpp"
#include "../config/config.hpp"

namespace hyprview::render {

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

// What the backdrop needs from the frame it is drawn into, blur recipe already clamped. Built
// fresh every frame, so a config edit applies in flight.
struct BackdropFrame {
  PHLMONITOR mon;
  double   eased = 0.0;
  double   dimK  = 0.0; // backdrop strength on the open/close clock
  CHyprColor baseCol{};
  bool  opening = false;
  bool  blurEnabled = false;
  int   passes = 0, sizePx = 0, resolution = 0;
  float strength = 0.0F;
};

class Backdrop {
  public:
    // Never samples currentFB (AGENTS.md 2.12): a solitary fullscreen window bypasses
    // shouldRenderWindow. Caller sets RPT_EXPORT and fbSize around it.
    [[nodiscard]] SP<Render::ITexture> source(const PHLMONITOR &m, int W,
                                              int H) const;

    [[nodiscard]] SP<Render::ITexture> composite() const {
      return m_srcFB && m_srcFB->isAllocated() ? m_srcFB->getTexture() : nullptr;
    }

    // Readable after the cache goes stale: `valid` gates RECOMPUTATION, the FB keeps the blur.
    [[nodiscard]] SP<Render::ITexture> blurTexture() const {
      return (m_blur.fb && m_blur.fb->isAllocated()) ? m_blur.fb->getTexture()
                                                     : nullptr;
    }
    [[nodiscard]] bool blurStale() const { return !m_blur.valid; }
    [[nodiscard]] bool drawn() const { return m_drawn; }

    [[nodiscard]] SP<Render::ITexture> frameSource(const BackdropFrame &f) const;

    // The blur, computed if the recipe or the source moved (AGENTS.md 2.28).
    [[nodiscard]] bool ensureBlur(const BackdropFrame &f) const;

    void render(const BackdropFrame &f) const;

  private:
    // The blur into the cache, when the key moved. False when there is no blur to
    // have: no source, or the filter said no.
    [[nodiscard]] bool computeBlur(const BackdropFrame &f, const SP<Render::ITexture> &src) const;

    mutable BlurCache m_blur;
    mutable SP<Render::IFramebuffer> m_srcFB, m_scratchFB;
    mutable bool m_drawn = false;
    // The blur has been rendered into its FB at least once: the base may be that blur.
    mutable bool m_blurDrawn = false;
    // The layer texture the committed composite was made from, compared on every open.
    mutable const void *m_srcTex = nullptr;
    // Video wallpaper: the source texture changes every frame, so it is composited once and held.
    mutable const void *m_lastObservedTex = nullptr;
    mutable int m_srcAnimStreak = 0;
    mutable bool m_srcAnimated = false;
    mutable CBlurFilter m_blurFilter; // independent of decoration:blur:*
};

} // namespace hyprview::render
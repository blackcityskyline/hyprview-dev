#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include "gl_util.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"
#include "backdrop.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace render {

// Never sample currentFB: solitary fullscreen bypasses shouldRenderWindow (AGENTS.md 2.12).

SP<Render::ITexture> Backdrop::source(const PHLMONITOR &m, int W,
                                     int H) const {
  if (!m || !g_pHyprOpenGL || !g_pHyprRenderer)
    return nullptr;
  const auto size = Vector2D(W, H);
  const auto fit = [size](const SP<Render::IFramebuffer> &fb) {
    return fb && fb->isAllocated() && fb->m_size == size;
  };
  if (!m_srcFB)
    m_srcFB = g_pHyprRenderer->createFB("hyprview backdrop");
  if (!fit(m_srcFB)) {
    m_srcFB->alloc(W, H);
    m_drawn = false;
  }
  if (!fit(m_srcFB))
    return nullptr;

  const void *tex = nullptr;
  for (const int li : {0, 1})
    for (const auto &lr : m->m_layerSurfaceLayers[li]) {
      const auto layer = lr.lock();
      if (!layer || !layer->m_mapped || !layer->visible() ||
          !layer->wlSurface() || !layer->wlSurface()->resource())
        continue;
      const auto surf = layer->wlSurface()->resource();
      if (!surf->m_current.texture || !surf->m_current.texture->ok())
        continue;
      tex = surf->m_current.texture.get();
      break;
    }
  if (tex != m_lastObservedTex) {
    m_srcAnimStreak   = m_lastObservedTex ? m_srcAnimStreak + 1 : 0;
    m_lastObservedTex = tex;
  } else {
    m_srcAnimStreak = 0;
  }
  m_srcAnimated = m_srcAnimStreak >= render::kSourceAnimatedStreak;

  if (m_drawn && (m_srcAnimated || (tex && tex == m_srcTex)))
    return m_srcFB->getTexture();

  // The source changed: rebuild into SCRATCH and let its CONTENT decide -- mid-recreation reads black.
  if (!m_scratchFB)
    m_scratchFB = g_pHyprRenderer->createFB("hyprview backdrop scratch");
  if (!fit(m_scratchFB))
    m_scratchFB->alloc(W, H);
  if (!fit(m_scratchFB))
    return m_drawn ? m_srcFB->getTexture() : nullptr;

  const auto oldFB = g_pHyprRenderer->m_renderData.currentFB;
  GLint oldVp[4];
  glGetIntegerv(GL_VIEWPORT, oldVp);
  const auto oldTransformDmg = g_pHyprRenderer->m_renderData.transformDamage;
  const auto oldModif        = g_pHyprRenderer->m_renderData.renderModif;
  g_pHyprRenderer->m_renderData.transformDamage = false;
  g_pHyprRenderer->m_renderData.renderModif     = Render::SRenderModifData{};
  m_scratchFB->bind();
  g_pHyprRenderer->m_renderData.currentFB = m_scratchFB;
  g_pHyprOpenGL->setViewport(0, 0, W, H);
  Hyprutils::Utils::CScopeGuard guard([&] {
    g_pHyprRenderer->m_renderData.transformDamage = oldTransformDmg;
    g_pHyprRenderer->m_renderData.renderModif     = oldModif;
    g_pHyprRenderer->m_renderData.currentFB = oldFB;
    if (oldFB)
      oldFB->bind();
    g_pHyprOpenGL->setViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
  });

  // Always filled: the scratch holds a previous attempt, and blending onto it would smear.
  static auto PBG = CConfigValue<Config::INTEGER>("misc:background_color");
  safeRenderRect("backdrop", CBox(0, 0, W, H), argb(*PBG, 1.0), {});

  if (m->m_background && m->m_background->ok()) {
    const auto fit = render::coverFit(W, H, m->m_background->m_size.x,
                                       m->m_background->m_size.y);
    g_pHyprOpenGL->renderTexture(m->m_background,
                                 CBox{fit.x, fit.y,
                                      m->m_background->m_size.x * fit.scale,
                                      m->m_background->m_size.y * fit.scale},
                                 {.a = 1.0F});
  }

  const double s = m->m_scale;
  int drew = 0;
  for (const int li : {0, 1}) { // LAYER_BACKGROUND, LAYER_BOTTOM
    for (const auto &lr : m->m_layerSurfaceLayers[li]) {
      const auto layer = lr.lock();
      if (!layer || !layer->m_mapped || !layer->visible() ||
          !layer->wlSurface() || !layer->wlSurface()->resource())
        continue;
      const auto surf = layer->wlSurface()->resource();
      if (!surf->m_current.texture || !surf->m_current.texture->ok())
        continue;
      const auto pos =
          layer->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
      const auto sz = layer->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
      if (sz.x < 1.0 || sz.y < 1.0)
        continue;
      const float a = layer->alpha()[Desktop::View::LS_ALPHA_FADE]->value();
      g_pHyprOpenGL->renderTexture(
          surf->m_current.texture,
          pxb(CBox{pos.x - m->m_position.x, pos.y - m->m_position.y, sz.x, sz.y}, s),
          {.a = a});
      drew++;
    }
  }

  unsigned char q[4] = {0, 0, 0, 0};
  glReadPixels(W / 4, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q);
  // Black check skipped for a live source: a dark frame of a video wallpaper IS the wallpaper.
  const bool haveContent = drew || m->m_background;
  const bool good = haveContent && (m_srcAnimated || !render::looksBlack(q[0], q[1], q[2]));
  if (good) {
    std::swap(m_srcFB, m_scratchFB);
    m_drawn = true;
    m_srcTex = tex;
    // Content changed under the same FB identity, so the srcId-keyed blur cache cannot see it.
    m_blur.valid = false;
  }
  return m_drawn ? m_srcFB->getTexture() : nullptr;
}

// RPT_EXPORT + fbSize save/restore (AGENTS.md 2.3); callers of source() would repeat it.
SP<Render::ITexture> Backdrop::frameSource(const BackdropFrame &f) const {
  if (!f.mon || !g_pHyprRenderer || !g_pHyprOpenGL)
    return nullptr;
  const int W = f.mon->m_pixelSize.x, H = f.mon->m_pixelSize.y;
  const auto oldProj = g_pHyprRenderer->m_renderData.projectionType;
  const auto oldFbSz = g_pHyprRenderer->m_renderData.fbSize;
  g_pHyprRenderer->m_renderData.fbSize = Vector2D(W, H);
  g_pHyprRenderer->setProjectionType(Render::RPT_EXPORT);
  const auto tex = source(f.mon, W, H);
  g_pHyprRenderer->m_renderData.fbSize = oldFbSz;
  g_pHyprRenderer->setProjectionType(oldProj);
  return tex;
}

void Backdrop::render(const BackdropFrame &f) const {
  const auto &m = f.mon;
  if (!m || !g_pHyprRenderer || !g_pHyprOpenGL)
    return;
  const double s      = m->m_scale;
  const auto fullPx   = pxb(CBox(0, 0, m->m_size.x, m->m_size.y), s);
  const int W         = m->m_pixelSize.x;
  const int H         = m->m_pixelSize.y;
  const double e      = f.eased;
  const auto baseCol  = f.baseCol;

  const float k = static_cast<float>(e);
  const double dimK = f.dimK;
  debug::dbg(std::string("backdrop e=") + std::to_string(e).substr(0, 5) +
      " opening=" + std::to_string(f.opening) +
      " k=" + std::to_string(k).substr(0, 5));

  // Opaque base from the backdrop SOURCES: at low k the blend target is currentFB, stale and dark.
  const float baseA = f.opening ? 1.0F : static_cast<float>(f.dimK);
  bool baseIsBlur = false;
  if (baseA > 0.0F) {
    // NOT gated on m_blur.valid: valid drives RECOMPUTATION; the FB keeps the last good blur.
    const bool blurCacheReady = m_blur.fb && m_blur.fb->isAllocated() &&
        m_blur.fb->m_size == Vector2D(W, H);
    const auto cachedTex = blurCacheReady ? m_blur.fb->getTexture() : nullptr;
    // Reading m_blur.fb directly returns multisample garbage: probe through a 1x1 FB instead.
    bool cachedUsable = false;
    unsigned char q[4] = {0, 0, 0, 0};
    if (!m_probeFB)
      m_probeFB = g_pHyprRenderer->createFB("hyprview cache probe");
    if (cachedTex && cachedTex->ok() && m_probeFB) {
      if (!m_probeFB->isAllocated())
        m_probeFB->alloc(1, 1);
      GLint bnd = 0;
      GLint vp[4] = {0, 0, 0, 0};
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bnd);
      glGetIntegerv(GL_VIEWPORT, vp);
      m_probeFB->bind();
      g_pHyprOpenGL->setViewport(0, 0, 1, 1);
      // Three samples: a single one can land in a legitimately dark corner.
      const CBox spots[3] = {CBox(0, 0, 1, 1), CBox(0.25F, 0.25F, 1, 1),
                             CBox(0.75F, 0.75F, 1, 1)};
      int lit = 0;
      // Restore through the API, never raw GL: raw glEnable desyncs the cap cache and the
      // next scissor(nullptr) early-returns (OpenGL.cpp:2620), clipping the rest of the frame.
      const bool scissorOn = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
      const CBox  dmgExtents = g_pHyprRenderer->m_renderData.damage.getExtents();
      g_pHyprOpenGL->scissor(nullptr);
      for (const auto &spot : spots) {
        g_pHyprOpenGL->renderTexture(cachedTex, spot, {.a = 1.0F});
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, q);
        if (!render::looksBlack(q[0], q[1], q[2]))
          lit++;
      }
      if (scissorOn)
        g_pHyprOpenGL->scissor(dmgExtents);
      glBindFramebuffer(GL_FRAMEBUFFER, bnd);
      glViewport(vp[0], vp[1], vp[2], vp[3]);
      cachedUsable = lit > 0;
    }
    if (cachedUsable) {
      baseIsBlur = true;
      g_pHyprOpenGL->renderTexture(cachedTex, fullPx, {.a = baseA});
    } else {
      const auto comp = frameSource(f);

      if (comp && comp->ok()) {
        g_pHyprOpenGL->renderTexture(comp, fullPx, {.a = baseA});
} else if (m_blur.fb && m_blur.fb->isAllocated()) {
        // The stale blur FB holds the previous, correct backdrop; an outdated frame beats a black
        // one. Painting a probe called black at baseA==1 buries the FB (the entry blackout).
        const auto stale = m_blur.fb->getTexture();
        if (stale && stale->ok() && cachedUsable) {
          baseIsBlur = true;
          g_pHyprOpenGL->renderTexture(stale, fullPx, {.a = baseA});
        }
      }
    }
  }


  if (!f.blurEnabled) {
    const auto col = argb(baseCol, dimK);
    if (col.a <= 0.0)
      return;
    safeRenderRect("backdrop", fullPx, col, {});
    return;
  }

  SP<Render::ITexture> src;
  {
    src = frameSource(f);
  }

  if (!src || k <= 0.0F)
    return;

  const void *srcId = src ? src.get() : nullptr;
  const int   cPasses  = f.passes;
  const int   cSize    = f.sizePx;
  const int   cRes     = f.resolution;
  const float cStrength = f.strength;
  if (!m_blur.matches(srcId, cPasses, cSize, cRes, cStrength))
    m_blur.invalidate();
  m_blur.srcId     = srcId;
  m_blur.passes    = cPasses;
  m_blur.sizePx    = cSize;
  m_blur.resolution = cRes;
  m_blur.strength  = cStrength;

  if (!m_blur.valid || !m_blur.fb || !m_blur.fb->isAllocated() ||
      m_blur.fb->m_size != Vector2D(W, H)) {
    if (!m_blur.fb)
      m_blur.fb = g_pHyprRenderer->createFB("hyprview blur");
    if (!m_blur.fb->isAllocated() || m_blur.fb->m_size != Vector2D(W, H))
      m_blur.fb->alloc(W, H);

    const auto oldProjType = g_pHyprRenderer->m_renderData.projectionType;
    const auto oldFbSize   = g_pHyprRenderer->m_renderData.fbSize;
    g_pHyprRenderer->m_renderData.fbSize = Vector2D(W, H);
    g_pHyprRenderer->setProjectionType(Render::RPT_EXPORT);

    m_blurFilter.prepare(cPasses, static_cast<float>(cSize), cRes, cStrength);
    const bool ok = src && m_blurFilter.render(src, m_blur.fb, W, H);

    g_pHyprRenderer->m_renderData.fbSize = oldFbSize;
    g_pHyprRenderer->setProjectionType(oldProjType);
    const auto PMON = g_pHyprRenderer->m_renderData.pMonitor;
    g_pHyprOpenGL->setViewport(0, 0, PMON ? (int)PMON->m_pixelSize.x : W,
                               PMON ? (int)PMON->m_pixelSize.y : H);

    if (!ok) {
      // Never leave currentFB exposed: it can hold a solitary fullscreen window.
      m_blur.valid = false;
      debug::dbg("backdrop FALLBACK: blur unavailable / no source");
      safeRenderRect("backdrop", fullPx, argb(baseCol, dimK), {});
      return;
    }
    m_blur.valid = true;
  }

  const auto tex = m_blur.fb ? m_blur.fb->getTexture() : nullptr;
  const float kk = static_cast<float>(f.dimK);
  if (tex && tex->ok() && !baseIsBlur) {
    // Through g_pHyprOpenGL->blend so m_capStatus follows: a raw glEnable makes the pass's own
    // blend(false) early-return (OpenGL.cpp:2620), leaving GL on with the cache saying off.
    const bool blendBefore = glIsEnabled(GL_BLEND) == GL_TRUE;
    GLint      srcRGB = GL_ONE, dstRGB = GL_ZERO, srcA = GL_ONE, dstA = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    g_pHyprOpenGL->blend(true);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    g_pHyprOpenGL->renderTexture(tex, fullPx, {.a = kk});
    glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
    if (!blendBefore)
      g_pHyprOpenGL->blend(false);
  } else if (!baseIsBlur) {
    m_blur.valid = false;
  }


  const auto dimCol = argb(baseCol, dimK);
  if (dimCol.a > 0.0)
    safeRenderRect("backdrop", fullPx, dimCol, {});


}
} // namespace render

// The owner reads the config here, every frame, and hands the backdrop a plain recipe:
// nothing below reaches back out for it (AGENTS.md 2.15, 2.16).
void Overview::renderBackdrop() const {
  m_backdrop.render({m_monitor.lock(), eased(), blurAlpha(),
                     cfg::blur.backdrop.get(), opening(), blurEnabled(),
                     blurPasses(), blurSize(), blurResolution(), blurStrength()});
}

// Parked-open frames: promote the moment a non-black composite exists (two frames is the budget).
void Overview::primeBackdrop() {
  const auto m = m_monitor.lock();
  const auto promote = [this] {
    if (!m_life.promote())
      return;
    debug::trace::life("promote", m_life.describe());
    // LIFE|open: the entry animation's inputs on its first painted frame, once per open.
    {
      static const model::Tile kEmpty{};
      const auto &t0 = m_scene.tiles().empty() ? kEmpty : m_scene.tiles()[0];
      debug::trace::life(
          "open",
          "dir=" + std::to_string(m_view.wsSlideDir()) + "|style=" +
              moveName(m_place.recipes.enterStyle) + "|landed0=" +
              std::to_string(t0.landed) + "|tiles=" +
              std::to_string(m_scene.tiles().size()) + "|allws=" +
              std::to_string(showAllWorkspaces() ? 1 : 0) + "|run=" +
              std::to_string(carouselMode() ? 1 : 0) + "|rebuildRaw=" +
              std::to_string(m_place.clocks.rebuild.raw(m_place.recipes.entry.ms)) + "|natural=" +
              std::to_string(t0.natural.x) + "," + std::to_string(t0.natural.y) + "," +
              std::to_string(t0.natural.w) + "," + std::to_string(t0.natural.h) +
              "|target=" + std::to_string(t0.target.x) + "," +
              std::to_string(t0.target.y) + "," + std::to_string(t0.target.w) + "," +
              std::to_string(t0.target.h));
    }
    hideLayers(); // deferred from open(): fades only once there is something drawn over it
  };

  if (!m || !g_pHyprRenderer || !g_pHyprOpenGL) {
    promote();
    damage();
    return;
  }
  const auto comp = m_backdrop.frameSource({m});

  const bool ready = comp && comp->ok();
  if (!m_life.parkTick(ready)) {
    damage();
    return;
  }

  debug::dbg("prime: promote ticks=" + std::to_string(m_life.parkTicks()) +
             " backdrop=" + std::to_string(ready ? 1 : 0));
  promote();
  damage();
}
} // namespace hyprview

#include <algorithm>
#include "../anim.hpp"
#include <cmath>
#include <ctime>
#include <string>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "geometry.hpp"
#include "gl_util.hpp"
#include "tile_look.hpp"
#include "../config/config.hpp"
#include "../overview.hpp"
#include "../debug/log.hpp"
#include "../clock/curves.hpp"
#include "window_content.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {


} // namespace

bool Overview::closeButtonsAlwaysOn() const {
  return cfg::look.close_button_visibility.get() == "always";
}

bool Overview::closeOnDoubleClick() const {
  return cfg::look.close_trigger.get() == "doubleclick";
}

// One rule for every layout, read by the hit-test too: the ✕ never appears where a click won't close.
bool Overview::tileCloseButtons() const {
  return !closeOnDoubleClick() && (m_view.desktop() || closeButtonsAlwaysOn());
}

LRect Overview::closeButtonRect(const LRect &lb) const {
  const std::string p = cfg::look.close_button_position.get();
  const render::Corner corner =
      p == "top-left"     ? render::Corner::TopLeft
    : p == "bottom-right" ? render::Corner::BottomRight
    : p == "bottom-left"  ? render::Corner::BottomLeft
                          : render::Corner::TopRight;
  return render::closeButtonRect(lb, cfg::look.close_button_size, corner);
}

double Overview::entryFade(size_t i) const {
  if (i >= m_scene.tiles().size())
    return 1.0;
  const auto &t = m_scene.tiles()[i];
  return m_place.entryFade(t, i, frame());
}

// A preview always wears the window's own aspect, so the content's arcs meet the ring's.
LRect Overview::tileBox(size_t i, const LRect &slot) const {
  if (m_view.desktop())
    return slot;
  double aspect = slot.w / std::max(1.0, slot.h);
  if (i < m_scene.tiles().size()) {
    if (const auto w = tileWindow(m_scene.tiles()[i])) {
      const auto s = w->sizeAnimation()->goal();
      if (s.x > 0 && s.y > 0)
        aspect = s.x / s.y;
    }
  }
  return render::fitInside(slot, aspect);
}

LRect Overview::tileRect(size_t i) const {
  if (i >= m_scene.tiles().size())
    return {};
  return tileBox(i, currentBox(m_scene.tiles()[i], static_cast<int>(i)));
}

// THE tile: shadow, ring, focus ring, frost, content, pill, ✕ — each under the next.
void Overview::drawTile(size_t i, const TileLook &look, bool withCaption) const {
  const auto m = m_monitor.lock();
  if (!m || i >= m_scene.tiles().size())
    return;
  const auto &t = m_scene.tiles()[i];
  // Chrome -- frost, ring, shadow, pill -- is geometry-only and applies to a tab too; only CONTENT needs a window.
  const auto w = tileWindow(t);
  const auto card = cardOf(t);
  const bool tab = card.isTab();
  if (!tab && (!w || !w->m_isMapped || w->isHidden()))
    return;

  const LRect box = look.box;
  if (box.w < 1.0 || box.h < 1.0)
    return;

  const double s        = m->m_scale;
  const double e        = eased();
  const float  f        = static_cast<float>(entryFade(i));
  const int    round    = look.round >= 0.0
                              ? static_cast<int>(look.round)
                              : cfg::look.preview_round;
  const float  roundPow = cfg::look.preview_round_power;
  const auto shadowCol  = cfg::colors.shadow.get(1.0);

  const bool framed   = static_cast<int>(i) == m_scene.hoveredTile() || look.lift;
  const bool selected = static_cast<int>(i) == m_scene.focused() && !look.lift;
  const bool focused  = cfg::look.show_focus_border != 0 && (framed || selected);
  // Whichever ring ends up on top is the one the content has to tuck under.
  const int ringW = render::focusRingWidth(cfg::look.show_focus_border != 0, framed,
                                           selected, cfg::look.hover_border_size,
                                           cfg::look.select_border_size,
                                           cfg::look.border_size);

  dropShadow(box, s, shadowCol,
             look.lift ? cfg::look.shadow_radius_lift : cfg::look.shadow_radius,
             look.lift ? anim::param(anim::Param::LiftShadow)
                       : anim::param(anim::Param::ShadowOffset),
             static_cast<float>(e * cfg::look.shadow_alpha * f), round, roundPow);

  if (look.frame) {
    // show_border holds for windows; a tab's card is the frosted desktop and needs its outline.
    if (cfg::look.show_border != 0 || tab) {
      strokeRing(box, s, cfg::colors.border.get(e * f), cfg::look.border_size,
                 round, roundPow);
      debug::trace::ring("tile_border", box, round, static_cast<double>(e * f));
    }
    if (focused)
      strokeRing(box, s,
                 framed ? cfg::colors.hover_border.get(e * f)
                        : cfg::colors.select_border.get(e * f),
                 ringW, round, roundPow);
  }

  // Frost: the cached blur at CONSTANT alpha (fading it leaks C*e*(1-e) of the sharp desktop),
  // plus the dim at the global e. A tab needs it ALWAYS: its card is this frost or nothing.
  const double apNow = entryNow(t, static_cast<int>(i));
  if (tab || (opening() && (e < 0.999 || apNow < 0.999))) {
    if (const auto btex = backdropBlurTexture(); btex && btex->ok()) {
      const double pxW = static_cast<double>(m->m_pixelSize.x);
      const double pxH = static_cast<double>(m->m_pixelSize.y);
      const CBox  boxPx = pxb(box, s);
      const auto  uv = render::frostUV(boxPx.x, boxPx.y, boxPx.width, boxPx.height, pxW, pxH);
      CTexPassElement::SRenderData td{};
      td.tex           = btex;
      td.box           = boxPx;
      td.a             = 1.0F;
      td.round         = pxr(round, s);
      td.roundingPower = roundPow;
      td.allowCustomUV = true;
      auto &uvTL = g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft;
      auto &uvBR = g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight;
      uvTL = Vector2D{uv.u0, uv.v0};
      uvBR = Vector2D{uv.u1, uv.v1};
      g_pHyprRenderer->draw(td, g_pHyprRenderer->m_renderData.damage);
      uvTL = Vector2D{-1.0, -1.0};
      uvBR = Vector2D{-1.0, -1.0};
      safeRenderRect("tile", boxPx, cfg::blur.backdrop.get(static_cast<float>(e)),
                                {.round = pxr(round, s), .roundingPower = roundPow});
    }
  }
  // The card is the frame; the content fits in it and the rest is the plate under it.
  const LRect content = w ? render::contentInside(box, w->sizeAnimation()->goal().x,
                                                   w->sizeAnimation()->goal().y)
                           : box;
  if (look.surface && !tab)
    safeRenderRect("tile", pxb(box, s), cfg::colors.surface.get(static_cast<float>(e * f)),
                              {.round = pxr(round, s), .roundingPower = roundPow});
  if (tab) {
    if (const auto tex = tabArt(t))
      g_pHyprOpenGL->renderTexture(tex, pxb(box, s),
                                   {.a = f, .round = pxr(round, s), .roundingPower = roundPow});
  } else {
    renderWindowLive(w, m, pxb(content, s), pxb(content, s), f, Time::steadyNow(),
                     render::contentRadiusFor(round, content, ringW, s, !look.contain),
                     roundPow, m_place.clocks.tiles.done(glideDur()), look.contain);
  }

  if (look.tone > 0.0)
    safeRenderRect("tile", pxb(box, s), argb(shadowCol, look.tone * e * f),
                              {.round = pxr(round, s), .roundingPower = roundPow});

  if (withCaption)
    drawTileCaption(i, look);

  const auto closeGlyph = m_pixel.closeGlyph();
    if (look.button && !look.lift && closeGlyph && closeGlyph->m_size.x > 0) {
    const LRect br = closeButtonRect(box);
    safeRenderRect("tile", pxb(br, s), cfg::colors.close_button.get(e),
                              {.round = pxr(br.h / 2.0, s)});
    const auto glyph = render::glyphRect(br, closeGlyph->m_size.x, closeGlyph->m_size.y,
                                         cfg::look.close_glyph_fill);
    g_pHyprOpenGL->renderTexture(closeGlyph, pxb(glyph, s), {.a = static_cast<float>(e)});
  }
}

// The caption: the title pill and the counter, in a pass of its own.
void Overview::drawTileCaption(int i, const TileLook &look) const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const auto &t = m_scene.tiles()[i];
  const bool tab = cardOf(t).isTab();
  const bool framed = static_cast<int>(i) == m_scene.hoveredTile() || look.lift;
  const bool selected = static_cast<int>(i) == m_scene.focused() && !look.lift;
  const double s = m->m_scale;
  const double e = eased();
  const float f = static_cast<float>(entryFade(i));
  const float roundPow = cfg::look.preview_round_power;
  const LRect box = look.box;
  // A tab's title IS the card: an uncaptioned tab card is invisible.
  if ((framed || selected || tab) && !look.lift) {
    const bool hasLabel = t.label && t.label->m_size.x > 0;
    const bool wantCounter = look.counter && m_scene.tiles().size() > 1;
    auto countTex = wantCounter ? t.countLabel : SP<Render::ITexture>{};
    const bool hasCounter = countTex && countTex->m_size.x > 0;
    const render::CaptionSizes sizes{
        hasLabel ? t.label->m_size.x : 0.0, hasLabel ? t.label->m_size.y : 0.0,
        hasCounter ? countTex->m_size.x : 0.0, hasCounter ? countTex->m_size.y : 0.0,
        hasLabel, hasCounter};
    const bool titleBelow = cfg::look.title_position.get() != "top";
    const std::string cpos = cfg::look.counter_position.get();
    const render::CaptionStyle style{
        cfg::look.title_pad_x, cfg::look.title_pad_y,
        cfg::look.counter_pad_x, cfg::look.counter_pad_y,
        cfg::look.title_gap, cfg::look.counter_gap,
        cfg::look.title_margin, cfg::look.counter_margin,
        m_view.cardRefW(), std::max<double>(cfg::grid.gap, 1.0),
        titleBelow, cpos.empty() ? titleBelow : cpos != "top"};
    const auto lay = render::captionLayout(box, stripBand(), m->m_size.x, m->m_size.y, sizes,
                                           style);
    if (lay.pill.w > 0.0) {
      const LRect pill = lay.pill;
      const Vector2D push = look.push ? pushOut(pill) : Vector2D{0.0, 0.0};
      const double pop =
          look.push ? render::captionRemains(std::hypot(push.x, push.y), pill.w, pill.h,
                                             exitT(), opening())
                    : 1.0;
      const double cf = e * f;
      LRect drawn = render::scaledAboutCentre(pill, pop);
      drawn.x += push.x;
      drawn.y += push.y;
      Vector2D cPush{};
      LRect cAt{};
      if (lay.counter.h > 0.0) {
        cPush = pushOut(lay.counter);
        cAt = render::scaledAboutCentre(lay.counter, pop);
        cAt.x += cPush.x;
        cAt.y += cPush.y;
      }
      debug::trace::ring("title_pill", pill,
                         pxr(render::pillRadius(cfg::look.title_round, pill.w, pill.h), s), cf);
      debug::trace::ring("counter_pill", lay.counter, 0, cf);
      debug::trace::ring("caption_push", LRect{push.x, push.y, cPush.x, cPush.y}, 0, cf);
      debug::trace::ring("title_pill_drawn", drawn,
                         pxr(render::pillRadius(cfg::look.title_round, drawn.w, drawn.h), s), cf);
      if (cAt.h > 0.0) {
        safeRenderRect("tile", pxb(CBox(cAt.x, cAt.y, cAt.w, cAt.h), s),
                               cfg::colors.counter_pill.get(cf),
                               {.round = pxr(render::pillRadius(cfg::look.counter_round, cAt.w, cAt.h), s),
                                .roundingPower = roundPow});
        g_pHyprOpenGL->renderTexture(
            countTex,
            pxb(LRect{cAt.x + (cAt.w - sizes.countW * pop) / 2.0,
                      cAt.y + (cAt.h - sizes.countH * pop) / 2.0,
                      sizes.countW * pop, sizes.countH * pop}, s),
            {.a = static_cast<float>(cf)});
      }
      safeRenderRect("tile", pxb(CBox(drawn.x, drawn.y, drawn.w, drawn.h), s),
                               cfg::colors.title_pill.get(cf),
                               {.round = pxr(render::pillRadius(cfg::look.title_round, drawn.w,
                                                                 drawn.h), s),
                                .roundingPower = roundPow});
      if (hasLabel)
        g_pHyprOpenGL->renderTexture(
            t.label,
            pxb(CBox(drawn.x + style.padX * lay.kFit * pop,
                     drawn.y + style.padY * lay.kFit * pop,
                     sizes.textW * lay.kFit * pop, sizes.textH * lay.kFit * pop), s),
            {.a = static_cast<float>(cf)});
      else
        g_pHyprOpenGL->renderTexture(
            countTex,
            pxb(LRect{pill.x + push.x + style.cPadX * lay.kFit,
                      pill.y + push.y + style.cPadY * lay.kFit,
                      sizes.countW * lay.kFit, sizes.countH * lay.kFit}, s),
            {.a = static_cast<float>(cf)});
    }
  }
}

void Overview::renderTiles() const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  // GL_BLEND left off by a previous pass writes the ✕ glyph's padding through as opaque black.
  const bool wantButtons = tileCloseButtons();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  const bool blendBefore = glIsEnabled(GL_BLEND) == GL_TRUE;
  GLint srcRGB = GL_ONE, dstRGB = GL_ZERO, srcA = GL_ONE, dstA = GL_ZERO;
  glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
  glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const int dragIdx = draggedTile();
  for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
    if (static_cast<int>(i) == dragIdx)
      continue; // the dragged tile floats over the strip; drawn later
    const auto &t = m_scene.tiles()[i];
    // A tab has no window: `!w` must not skip it, or a tabs run draws nothing.
    if (cardOf(t).isTab()) {
      if (swapfxActiveSubject(t.subj, false))
        continue;
    } else {
      const auto w = tileWindow(t);
      if (!w || !w->m_isMapped || w->isHidden() || swapfxActive(w, false))
        continue; // flying via a landing: its slot is painted by renderSwapFX
    }
    drawTile(i, gridLook(tileRect(i), cfg::look.preview_round, wantButtons));
  }

  glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
  if (!blendBefore)
    glDisable(GL_BLEND);
}

void Overview::renderGhosts() const {
  if (m_fx.ghosts().empty() || m_place.clocks.rebuild.done(m_place.recipes.ghost.ms))
    return;
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const double scale = m->m_scale;
  const auto when    = Time::steadyNow();
  const float roundPow = cfg::look.preview_round_power;
  const double p    = std::min(1.0, m_place.clocks.rebuild.raw(m_place.recipes.ghost.ms));
  const double eOut = curves::eval(m_place.recipes.ghost.curve, p);

  for (const auto &g : m_fx.ghosts()) {
    const auto w = m_windows.windowOf(g.subj);
    if (!w || !w->m_isMapped || w->isHidden())
      continue;
    const double k  = 1.0 - 0.3 * eOut;
    const LRect box = render::scaledAboutCentre(g.box, k);
    LRect slidBox = box;
    float gAlpha = static_cast<float>((1.0 - eOut) * 0.85);
    // Same three branches as the entry: branch on style fields, never by equality.
  if (m_place.recipes.exitStyle.travel == anim::Travel::Slide && m_view.wsSlideDir() != 0) {
    if (m_place.recipes.exitStyle.axis == anim::Axis::Horizontal)
      slidBox.x -= static_cast<double>(m_view.wsSlideDir()) * m->m_size.x * eOut;
    else
      slidBox.y -= static_cast<double>(m_view.wsSlideDir()) * m->m_size.y * eOut;
  }
    const CBox px = pxb(slidBox, scale);
    renderWindowLive(w, m, px, px, gAlpha, when,
                     contentRadiusPx(cfg::look.preview_round,
                                     cfg::look.border_size, scale),
                     roundPow, true);
  }
}

} // namespace hyprview

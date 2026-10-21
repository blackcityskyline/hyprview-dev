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


}

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

  // Frost: the cached blur at CONSTANT alpha, plus the dim at the global e. A tab ALWAYS needs it.
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
  const auto tabTex = tab ? tabArt(t) : SP<Render::ITexture>{};
  const auto what = render::cardContent(tab ? render::CardKind::Tab : render::CardKind::Window,
                                         tabTex != nullptr);
  if (look.surface && !tab)
    safeRenderRect("tile", pxb(box, s), cfg::colors.surface.get(static_cast<float>(e * f)),
                              {.round = pxr(round, s), .roundingPower = roundPow});
  switch (what) {
    case render::CardContent::Picture:
      g_pHyprOpenGL->renderTexture(tabTex, pxb(box, s),
                                   {.a = f, .round = pxr(round, s), .roundingPower = roundPow});
      break;
    case render::CardContent::PlateAndTitle: {
      safeRenderRect("tile", pxb(box, s), cfg::colors.surface.get(static_cast<float>(e * f)),
                              {.round = pxr(round, s), .roundingPower = roundPow});
      if (t.label && t.label->m_size.x > 0) {
        // No picture: the title, centred and dim, is all the card can say about the page.
        const LRect room{box.x + box.w * 0.1, box.y + box.h * 0.35, box.w * 0.8, box.h * 0.3};
        g_pHyprOpenGL->renderTexture(
            t.label, pxb(render::contentInside(room, t.label->m_size.x, t.label->m_size.y), s),
            {.a = 0.45F * f});
      }
      break;
    }
    case render::CardContent::Window:
      renderWindowLive(w, m, pxb(content, s), pxb(content, s), f, Time::steadyNow(),
                       render::contentRadiusFor(round, content, ringW, s, !look.contain),
                       roundPow, m_place.clocks.tiles.done(glideDur()), look.contain);
      break;
  }
  // The card's own icon in the corner the caption does not reach: the browser for a tab, the app for a window.
  const double iconSide = render::badgeSide(box.h);
  const int badgePx = render::badgePx(iconSide, s);
  const auto badgeIcon = tab ? browserIcon(t, badgePx) : windowIcon(w, badgePx);
  // The badge pops a fifth and shrinks away; the open plays the same schedule backwards.
  constexpr double kBadgeSpan = 0.28;
  const double badgeK = render::badgeK(openT(), exitT(), opening(), kBadgeSpan);
  const auto pose = render::badgePose(badgeK);
  const double iconPop = pose.pop;
  const double iconFade = pose.alpha;
  const float badgeA = static_cast<float>(f * iconFade);
  const double badgeSide = badgePx / s; // the icon's own quantum, so the shadow caches with it
  if (badgeIcon && badgeA > 0.0F &&
      (tab ? cfg::look.show_tab_icons : cfg::look.show_window_icons) != 0) {
    const LRect badge = render::badgeBox(box, iconSide, iconPop);
    auto spec = render::iconShadowSpec(badgeSide, cfg::look.icon_shadow_offset,
                                        cfg::look.icon_shadow_blur);
    spec.r = shadowCol.r;
    spec.g = shadowCol.g;
    spec.b = shadowCol.b;
    if (const auto sh = m_pixel.shadow(spec, badgeSide, badgeSide, badgeSide * 0.24, s); sh.tex)
      g_pHyprOpenGL->renderTexture(
          sh.tex, pxb(LRect{badge.x - sh.pad, badge.y - sh.pad, badge.w + 2 * sh.pad,
                            badge.h + 2 * sh.pad}, s),
          {.a = static_cast<float>(cfg::look.icon_shadow_alpha * badgeA)});
    g_pHyprOpenGL->renderTexture(badgeIcon, pxb(badge, s), {.a = badgeA});
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
    // The site's icon belongs beside the title; a tab with no title has nothing to sit next to.
    const auto favicon = tab && cfg::look.show_tab_icons != 0 ? tabIcon(t)
                                                              : SP<Render::ITexture>{};
    const bool hasIcon = hasLabel && favicon && favicon->m_size.x > 0;
    const double iconSide = hasIcon ? t.label->m_size.y * 1.5 : 0.0;
    const render::CaptionSizes sizes{
        hasLabel ? t.label->m_size.x : 0.0, hasLabel ? t.label->m_size.y : 0.0,
        hasCounter ? countTex->m_size.x : 0.0, hasCounter ? countTex->m_size.y : 0.0,
        hasLabel, hasCounter, iconSide, hasIcon};
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
      // One owner for the caption; the view only says whether it may slide aside.
      const auto covers = coveringCards();
      const auto title = render::placeCaption(
          {pill, covers, look.push, exitT(), opening()});
      const auto count = lay.counter.h > 0.0
                             ? render::placeCaption({lay.counter, covers, look.push, exitT(),
                                                     opening()})
                             : render::Caption{};
      const double pop = title.scale;
      const double cf = e * f;
      LRect drawn = render::scaledAboutCentre(title.box, pop);
      Vector2D cPush{};
      LRect cAt{};
      if (lay.counter.h > 0.0) {
        cPush = Vector2D{count.shove.x, count.shove.y};
        cAt = render::scaledAboutCentre(count.box, count.scale);
      }
      debug::trace::ring("title_pill", pill,
                         pxr(render::pillRadius(cfg::look.title_round, pill.w, pill.h), s), cf);
      debug::trace::ring("counter_pill", lay.counter, 0, cf);
      debug::trace::ring("caption_push", LRect{title.shove.x, title.shove.y, cPush.x, cPush.y},
                         0, cf);
      debug::trace::ring("title_pill_drawn", drawn,
                         pxr(render::pillRadius(cfg::look.title_round, drawn.w, drawn.h), s), cf);
      if (cAt.h > 0.0) {
        safeRenderRect("tile", pxb(CBox(cAt.x, cAt.y, cAt.w, cAt.h), s),
                               cfg::colors.counter_pill.get(cf),
                               {.round = pxr(render::pillRadius(cfg::look.counter_round, cAt.w, cAt.h), s),
                                .roundingPower = roundPow});
        const double cPop = count.scale;
        g_pHyprOpenGL->renderTexture(
            countTex,
            pxb(LRect{cAt.x + (cAt.w - sizes.countW * cPop) / 2.0,
                      cAt.y + (cAt.h - sizes.countH * cPop) / 2.0,
                      sizes.countW * cPop, sizes.countH * cPop}, s),
            {.a = static_cast<float>(cf)});
      }
      safeRenderRect("tile", pxb(CBox(drawn.x, drawn.y, drawn.w, drawn.h), s),
                               cfg::colors.title_pill.get(cf),
                               {.round = pxr(render::pillRadius(cfg::look.title_round, drawn.w,
                                                                 drawn.h), s),
                                .roundingPower = roundPow});
      if (hasIcon) {
        const LRect at{drawn.x + style.padX * lay.kFit * pop,
                       drawn.y + (drawn.h - iconSide * lay.kFit * pop) / 2.0,
                       iconSide * lay.kFit * pop, iconSide * lay.kFit * pop};
        // The icon's shadow is the plugin's own: Hyprland's shader divides by the shape.
        auto spec = render::iconShadowSpec(iconSide, cfg::look.icon_shadow_offset,
                                            cfg::look.icon_shadow_blur);
        const auto shadowCol = cfg::colors.shadow.get(1.0);
        spec.r = shadowCol.r;
        spec.g = shadowCol.g;
        spec.b = shadowCol.b;
        if (const auto sh = m_pixel.shadow(spec, at.w, at.h, at.w * 0.2, s); sh.tex)
          g_pHyprOpenGL->renderTexture(
              sh.tex,
              pxb(LRect{at.x - sh.pad, at.y - sh.pad, at.w + 2 * sh.pad, at.h + 2 * sh.pad}, s),
              {.a = static_cast<float>(cfg::look.icon_shadow_alpha * cf)});
        g_pHyprOpenGL->renderTexture(favicon, pxb(at, s),
                                     {.a = static_cast<float>(cf)});
      }
      if (hasLabel)
        g_pHyprOpenGL->renderTexture(
            t.label,
            pxb(CBox(drawn.x + (style.padX + (hasIcon ? iconSide + style.iconGap : 0.0)) *
                                    lay.kFit * pop,
                     drawn.y + (drawn.h - sizes.textH * lay.kFit * pop) / 2.0,
                     sizes.textW * lay.kFit * pop, sizes.textH * lay.kFit * pop), s),
            {.a = static_cast<float>(cf)});
      else
        // The counter rides INSIDE the pill, so it pops with it.
        g_pHyprOpenGL->renderTexture(
            countTex,
            pxb(LRect{drawn.x + style.cPadX * lay.kFit * pop,
                      drawn.y + (drawn.h - sizes.countH * lay.kFit * pop) / 2.0,
                      sizes.countW * lay.kFit * pop, sizes.countH * lay.kFit * pop}, s),
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

}

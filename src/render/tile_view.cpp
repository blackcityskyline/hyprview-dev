#include <algorithm>
#include "../anim.hpp"
#include "../model/title_text.hpp"
#include <cmath>
#include <ctime>
#include <string>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
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

bool Overview::closeButtonsOn() const {
  return cfg::look.close_button != 0;
}

bool Overview::closeButtonsHoverOnly() const {
  return cfg::look.close_button_hover_only != 0;
}

bool Overview::closeButtonsEverywhere() const {
  return cfg::look.close_button_visibility.get() == "everywhere";
}

bool Overview::closeOnDoubleClick() const {
  return cfg::look.close_trigger.get() == "doubleclick";
}

// One rule for every layout, read by the hit-test too: the ✕ never appears where a click won't close.
bool Overview::tileCloseButtons() const {
  return closeButtonsOn() && !closeOnDoubleClick() &&
         (m_view.desktop() || closeButtonsEverywhere());
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

void Overview::drawTabContent(const model::SubjectRef &subj, const LRect &box,
                               SP<Render::ITexture> label, float contentAlpha,
                               float plateAlpha) const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const double s = m->m_scale;
  const int round = cfg::look.preview_round;
  const float roundPow = cfg::look.preview_round_power;
  const auto tex = m_pixel.picture(subj);
  if (tex) {
    const LRect pic = render::contentInside(box, tex->m_size.x, tex->m_size.y);
    g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), tex, pxb(pic, s),
                                 {.a = contentAlpha, .round = pxr(round, s),
                                  .roundingPower = roundPow});
    return;
  }
  safeRenderRect("tile", pxb(box, s), cfg::colors.surface.get(plateAlpha),
                            {.round = pxr(round, s), .roundingPower = roundPow});
  if (label && label->m_size.x > 0) {
    const LRect room{box.x + box.w * 0.1, box.y + box.h * 0.35, box.w * 0.8, box.h * 0.3};
    g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
        label, pxb(render::contentInside(room, label->m_size.x, label->m_size.y), s),
        {.a = 0.45F * contentAlpha});
  }
}

// THE tile: shadow, ring, focus ring, frost, content, plate, ✕ — each under the next.
void Overview::drawTile(size_t i, const TileLook &look, bool withCaption) const {
  const auto m = m_monitor.lock();
  if (!m || i >= m_scene.tiles().size())
    return;
  const auto &t = m_scene.tiles()[i];
  // Chrome -- frost, ring, shadow, pill -- is geometry-only and applies to a tab too; only CONTENT needs a window.
  const auto w = tileWindow(t);
  const auto card = cardOf(t);
  const bool tab = card.isTab();
  if (!tab && (!w || !w->mapped() || w->isHidden()))
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

  // Frost: the same two layers as the backdrop -- sharp source, then glass -- so a plate and the desktop under it never disagree.
  const double apNow = entryNow(t, static_cast<int>(i));
  if (tab || (opening() && (e < 0.999 || apNow < 0.999))) {
    const auto src  = backdropSourceTexture();
    const auto btex = blurEnabled() ? backdropBlurTexture() : SP<Render::ITexture>{};
    if (src && src->ok()) {
      const double pxW = static_cast<double>(m->m_pixelSize.x);
      const double pxH = static_cast<double>(m->m_pixelSize.y);
      const CBox  boxPx = pxb(box, s);
      const auto  uv = render::frostUV(boxPx.x, boxPx.y, boxPx.width, boxPx.height, pxW, pxH,
                                       render::backdropZoom(e));
      const auto blit = [&](const SP<Render::ITexture> &tex, float a) {
        CTexPassElement::SRenderData td{};
        td.tex           = tex;
        td.box           = boxPx;
        td.a             = a;
        td.round         = pxr(round, s);
        td.roundingPower = roundPow;
        td.allowCustomUV = true;
        g_pHyprRenderer->draw(g_pHyprRenderer->context(), td, g_pHyprRenderer->context().m_data.damage);
      };
      auto &uvTL = g_pHyprRenderer->context().m_data.primarySurfaceUVTopLeft;
      auto &uvBR = g_pHyprRenderer->context().m_data.primarySurfaceUVBottomRight;
      uvTL = Vector2D{uv.u0, uv.v0};
      uvBR = Vector2D{uv.u1, uv.v1};
      const bool   blurOk = btex && btex->ok();
      const double glass  = glassAlpha();
      if (!blurOk || glass < 1.0)
        blit(src, 1.0F);
      if (blurOk)
        blit(btex, static_cast<float>(glass));
      uvTL = Vector2D{-1.0, -1.0};
      uvBR = Vector2D{-1.0, -1.0};
      const auto dim = cfg::blur.backdrop.get(static_cast<float>(e));
      if (dim.a > 0.0)
        safeRenderRect("tile", boxPx, dim, {.round = pxr(round, s), .roundingPower = roundPow});
    }
  }
  // The card is the frame; the content fits in it and the rest is the plate under it.
  const LRect content = w ? render::contentInside(box, w->sizeAnimation()->goal().x,
                                                   w->sizeAnimation()->goal().y)
                           : box;
  if (tab) {
    // Loads the tab's picture into the pixel cache; drawTabContent reads it back out.
    (void)tabArt(t);
    // The title is the plate's now: a card without a picture is a bare plate, not a dim
    // duplicate of the same title in the middle of it.
    drawTabContent(t.subj, box, nullptr, f, static_cast<float>(e * f));
  } else {
    if (look.surface)
      safeRenderRect("tile", pxb(box, s), cfg::colors.surface.get(static_cast<float>(e * f)),
                                {.round = pxr(round, s), .roundingPower = roundPow});
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
    const bool btnHovered = !closeButtonsHoverOnly() ||
                            static_cast<int>(i) == m_scene.hoveredTile();
    if (look.button && !look.lift && btnHovered && closeGlyph && closeGlyph->m_size.x > 0) {
    const LRect br = closeButtonRect(box);
    safeRenderRect("tile", pxb(br, s), cfg::colors.close_button.get(e),
                              {.round = pxr(br.h / 2.0, s)});
    const auto glyph = render::glyphRect(br, closeGlyph->m_size.x, closeGlyph->m_size.y,
                                         cfg::look.close_glyph_fill);
    g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), closeGlyph, pxb(glyph, s), {.a = static_cast<float>(e)});
  }
}

// THE CAPTION, two owners: the card's name is ON the card -- a plate inside its bottom edge,
// with the icon -- and the run's counter is the outside pill it has always been.
void Overview::drawTileCaption(int i, const TileLook &look) const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const auto &t = m_scene.tiles()[i];
  const bool tab = cardOf(t).isTab();
  const double s = m->m_scale;
  const double e = eased();
  const float f = static_cast<float>(entryFade(i));
  const float roundPow = cfg::look.preview_round_power;
  const LRect box = look.box;

  const bool hasLabel = t.label && t.label->m_size.x > 0;
  const bool wantCounter = look.counter && m_scene.tiles().size() > 1;
  const auto countTex = wantCounter ? t.countLabel : SP<Render::ITexture>{};
  const bool hasCounter = countTex && countTex->m_size.x > 0;
  if (!hasLabel && !hasCounter)
    return;

  // The plate's icon: the page's favicon for a tab, the app's own for a window.
  const double iconSide = hasLabel ? t.label->m_size.y * 1.5 : 0.0;
  const auto w = tileWindow(t);
  const bool showIcon =
      hasLabel && (tab ? cfg::look.show_tab_icons : cfg::look.show_window_icons) != 0;
  const auto icon = !showIcon ? SP<Render::ITexture>{}
                              : tab ? tabIcon(t) : windowIcon(w, render::iconPx(iconSide, s));
  const bool hasIcon = icon && icon->m_size.x > 0;

  const render::CaptionSizes sizes{
      hasLabel ? t.label->m_size.x : 0.0, hasLabel ? t.label->m_size.y : 0.0,
      hasCounter ? countTex->m_size.x : 0.0, hasCounter ? countTex->m_size.y : 0.0,
      hasLabel, hasCounter, iconSide, hasIcon};
  const std::string cpos = cfg::look.counter_position.get();
  const render::CaptionStyle style{
      cfg::look.title_pad_x, cfg::look.title_pad_y,
      cfg::look.counter_pad_x, cfg::look.counter_pad_y,
      cfg::look.counter_gap, cfg::look.counter_margin, m_view.cardRefW(),
      cpos.empty() ? cfg::look.title_position.get() != "top" : cpos != "top", 6.0};
  const auto lay = render::captionLayout(box, stripBand(), m->m_size.x, m->m_size.y, sizes,
                                         style);
  const float a = static_cast<float>(e * f);

  const LRect plate = lay.plate;
  if (plate.w > 0.0 && drawable(plate, s)) {
    const double k = lay.kFit;
    const int round =
        look.round >= 0.0 ? static_cast<int>(look.round) : cfg::look.preview_round;
    debug::trace::ring("title_pill", plate, pxr(round, s), a);
    // The bar IS the card's edge: the card's own rounded rect, scissored to the bar (two rects
    // double the alpha at the seam; a bar with its own corners shows the content through).
    auto &rd = g_pHyprRenderer->context().m_data;
    const CBox savedClip = rd.clipBox;
    rd.clipBox = pxb(plate, s);
    safeRenderRect("tile", pxb(box, s), cfg::colors.title_pill.get(a),
                   {.round = pxr(round, s), .roundingPower = roundPow});
    rd.clipBox = savedClip;
    const double iconSpan = hasIcon ? (iconSide + style.iconGap) * k : 0.0;
    if (hasIcon) {
      const LRect at{lay.contentX, plate.y + (plate.h - iconSide * k) * 0.5, iconSide * k,
                     iconSide * k};
      // The icon's shadow is the plugin's own: Hyprland's shader divides by the shape.
      auto spec = render::iconShadowSpec(iconSide, cfg::look.icon_shadow_offset,
                                         cfg::look.icon_shadow_blur);
      const auto shadowCol = cfg::colors.shadow.get(1.0);
      spec.r = shadowCol.r;
      spec.g = shadowCol.g;
      spec.b = shadowCol.b;
      if (const auto sh = m_pixel.shadow(spec, at.w, at.h, at.w * 0.2, s); sh.tex)
        g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
            sh.tex,
            pxb(LRect{at.x - sh.pad, at.y - sh.pad, at.w + 2 * sh.pad, at.h + 2 * sh.pad}, s),
            {.a = static_cast<float>(cfg::look.icon_shadow_alpha * a)});
      g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), icon, pxb(at, s), {.a = a});
    }
    if (hasLabel)
      g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
          t.label,
          pxb(CBox(lay.contentX + iconSpan,
                   plate.y + (plate.h - sizes.textH * k) * 0.5, sizes.textW * k,
                   sizes.textH * k), s),
          {.a = a});
    else
      // Nothing to name: the counter is the bar's own text, not a second plate.
      g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
          countTex,
          pxb(CBox(lay.contentX, plate.y + (plate.h - sizes.countH * k) * 0.5,
                   sizes.countW * k, sizes.countH * k), s),
          {.a = a});
  }

  // The counter: the outside pill, pushed clear of a card that reaches it, fading with its card.
  if (lay.counter.h > 0.0) {
    const auto count = render::placeCaption(
        {lay.counter, coveringCards(), look.push, exitT(), opening()});
    const LRect cAt = render::scaledAboutCentre(count.box, count.scale);
    debug::trace::ring("counter_pill", lay.counter, 0, a);
    if (cAt.h > 0.0) {
      safeRenderRect("tile", pxb(CBox(cAt.x, cAt.y, cAt.w, cAt.h), s),
                     cfg::colors.counter_pill.get(a),
                     {.round = pxr(render::pillRadius(cfg::look.counter_round, cAt.w, cAt.h), s),
                      .roundingPower = roundPow});
      const double cPop = count.scale;
      g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
          countTex,
          pxb(LRect{cAt.x + (cAt.w - sizes.countW * cPop) / 2.0,
                    cAt.y + (cAt.h - sizes.countH * cPop) / 2.0,
                    sizes.countW * cPop, sizes.countH * cPop}, s),
          {.a = a});
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
      if (!w || !w->mapped() || w->isHidden() || swapfxActive(w, false))
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
    if (!w) {
      const float gAlpha = static_cast<float>((1.0 - eOut) * 0.85);
      const double k  = 1.0 - 0.3 * eOut;
      const LRect box = render::scaledAboutCentre(g.box, k);
      const auto label = m_pixel.subjectLabel(
          g.subj, model::truncateTitle(m_tabs.label(g.subj)),
          cfg::colors.title_label.get(1.0), cfg::look.title_font_size.get(),
          static_cast<int>(box.w * 0.8));
      drawTabContent(g.subj, box, label, gAlpha, gAlpha);
      continue;
    }
    if (!w->mapped() || w->isHidden())
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

void Overview::renderQuickLook() const {
  if (!m_view.quickLook())
    return;
  const auto m = m_monitor.lock();
  const int idx = m_scene.focused();
  if (!m || idx < 0)
    return;
  const auto &t = m_scene.tiles()[static_cast<std::size_t>(idx)];
  const bool tab = cardOf(t).isTab();
  const auto w = tab ? ownerWindow(t) : tileWindow(t);
  if (!w || !w->mapped() || w->isHidden())
    return;

  const double s = m->m_scale;
  const double e = std::clamp(quickLookProgress(), 0.0, 1.0);
  if (e <= 0.01)
    return;
  constexpr double kPad = 14.0;
  const LRect card = lerp(m_view.quickLookFrom(), quickLookTarget(t), e);
  const LRect content{card.x + kPad, card.y + kPad, card.w - 2.0 * kPad,
                      card.h - 2.0 * kPad};
  const int round = cfg::look.preview_round;
  const float roundPow = cfg::look.preview_round_power;
  const float a = static_cast<float>(e);
  dropShadow(card, s, cfg::colors.shadow.get(1.0), cfg::look.shadow_radius,
             anim::param(anim::Param::ShadowOffset),
             static_cast<float>(e * cfg::look.shadow_alpha), round, roundPow);
  safeRenderRect("quick_look", pxb(card, s), cfg::colors.surface.get(a),
                 {.round = pxr(round, s), .roundingPower = roundPow});
  renderWindowLive(w, m, pxb(content, s), pxb(content, s), a, Time::steadyNow(),
                   render::contentRadiusFor(round, content, cfg::look.border_size, s, false),
                   roundPow, true, true);

  if (e < 0.25)
    return;
  const auto col = cfg::colors.title_label.get(1.0);
  const int font = cfg::look.title_font_size;
  const auto label = tab ? m_pixel.subjectLabel(t.subj, t.labelText, col, font,
                                                static_cast<int>(content.w))
                         : m_pixel.windowLabel(w, t.labelText, col, font,
                                               static_cast<int>(content.w));
  if (!label || label->m_size.x <= 0)
    return;
  // The same bar as a card's: the preview's own bottom edge.
  const render::CaptionStyle style{cfg::look.title_pad_x, cfg::look.title_pad_y, 0.0, 0.0,
                                   0.0, 0.0, card.w, true, 6.0};
  const auto lay = render::captionLayout(
      card, LRect{}, m->m_size.x, m->m_size.y,
      render::CaptionSizes{label->m_size.x, label->m_size.y, 0.0, 0.0, true, false, 0.0, false},
      style);
  const LRect plate = lay.plate;
  if (plate.w <= 0.0 || !drawable(plate, s))
    return;
  auto &rd = g_pHyprRenderer->context().m_data;
  const CBox savedClip = rd.clipBox;
  rd.clipBox = pxb(plate, s);
  safeRenderRect("quick_look", pxb(card, s), cfg::colors.title_pill.get(a),
                 {.round = pxr(round, s), .roundingPower = roundPow});
  rd.clipBox = savedClip;
  g_pHyprOpenGL->renderTexture(g_pHyprRenderer->context(), 
      label, pxb(LRect{lay.contentX, plate.y + (plate.h - label->m_size.y * lay.kFit) * 0.5,
                       label->m_size.x * lay.kFit, label->m_size.y * lay.kFit}, s),
      {.a = a});
}

}

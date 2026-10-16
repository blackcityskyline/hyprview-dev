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

namespace gloview {

namespace {


} // namespace

bool Overview::closeButtonsAlwaysOn() const {
  return cfg::look.close_button_visibility.get() == "always";
}

bool Overview::closeOnDoubleClick() const {
  return cfg::look.close_trigger.get() == "doubleclick";
}

// One rule for every layout, read by the hit-test too, so the ✕ can never appear
// where a click will not close.
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

// A preview always wears the window's own aspect, so the content's arcs meet the
// ring's. A second way to compute a tile box is a second way to be wrong.
LRect Overview::tileBox(size_t i, const LRect &slot) const {
  if (m_view.desktop())
    return slot;
  double aspect = slot.w / std::max(1.0, slot.h);
  if (i < m_scene.tiles().size()) {
    // The window's own proportions, when there is one. A tab takes the slot's aspect, which is
    // what it did before any of this: a tab has no geometry of its own and a card squeezed to a
    // made-up ratio would be worse than one that simply fills the slot it was given.
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

// THE tile: shadow, ring, focus ring, frost, content, pill, ✕ — each under the
// next. A layout contributes a box and flags only.
void Overview::drawTile(size_t i, const TileLook &look) const {
  const auto m = m_monitor.lock();
  if (!m || i >= m_scene.tiles().size())
    return;
  const auto &t = m_scene.tiles()[i];
  // A tab has no window, and this was where it ended. The card chrome above -- frost, ring,
  // shadow, pill -- is drawn from geometry alone and applies to a tab exactly as to a window;
  // only the CONTENT needs a window, and that is the one thing branched further down.
  const auto w = tileWindow(t);
  const bool tab = m_tabs.isTab(t.subj);
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
  const int ringW = focused ? (framed ? cfg::look.hover_border_size
                                      : cfg::look.select_border_size)
                            : cfg::look.border_size;

  dropShadow(box, s, shadowCol,
             look.lift ? cfg::look.shadow_radius_lift : cfg::look.shadow_radius,
             look.lift ? anim::param(anim::Param::LiftShadow)
                       : anim::param(anim::Param::ShadowOffset),
             static_cast<float>(e * cfg::look.shadow_alpha * f), round, roundPow);

  if (look.frame) {
    if (cfg::look.show_border != 0) {
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

  // Transition frost: the cached blur at CONSTANT alpha (fading it leaks
  // C*e*(1-e) of the sharp desktop) plus the dim rect at the GLOBAL alpha e.
  //
  // A tab needs it ALWAYS. A window's card background is the window's own content, so once the
  // transition is over there is nothing left to draw and skipping the frost is free. A tab has
  // no window: its card is this frost or it is nothing at all, and 13 cards of a browser with
  // no screenshots yet came out as 13 holes in the grid. Not a special case -- it is the same
  // backdrop, held for a subject whose content does not exist yet.
  const double apNow = entryNow(t, static_cast<int>(i));
  if (tab || (opening() && (e < 0.999 || apNow < 0.999))) {
    if (const auto btex = backdropBlurTexture(); btex && btex->ok()) {
      const double pxW = static_cast<double>(m->m_pixelSize.x);
      const double pxH = static_cast<double>(m->m_pixelSize.y);
      const CBox  boxPx = pxb(box, s);
      CTexPassElement::SRenderData td{};
      td.tex           = btex;
      td.box           = boxPx;
      td.a             = 1.0F;
      td.round         = pxr(round, s);
      td.roundingPower = roundPow;
      td.allowCustomUV = true;
      auto &uvTL = g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft;
      auto &uvBR = g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight;
      uvTL = Vector2D{boxPx.x / pxW, boxPx.y / pxH};
      uvBR = Vector2D{(boxPx.x + boxPx.width) / pxW,
                      (boxPx.y + boxPx.height) / pxH};
      g_pHyprRenderer->draw(td, g_pHyprRenderer->m_renderData.damage);
      uvTL = Vector2D{-1.0, -1.0};
      uvBR = Vector2D{-1.0, -1.0};
      // The frost covers the global dim: re-apply it at the global alpha.
      safeRenderRect("tile", boxPx, cfg::blur.backdrop.get(static_cast<float>(e)),
                                {.round = pxr(round, s), .roundingPower = roundPow});
    }
  }
  if (tab) {
    // What the browser side left: a screenshot, an icon, or nothing. Nothing is not a special
    // case -- the frost above already drew the card, so a tab with no art yet is a card with
    // its caption and no picture, which is the honest state of a browser that has not got
    // round to writing one.
    if (const auto tex = tabArt(t))
      g_pHyprOpenGL->renderTexture(tex, pxb(box, s), {.a = f});
  } else {
    renderWindowLive(w, m, pxb(box, s), pxb(box, s), f, Time::steadyNow(),
                     contentRadiusPx(round, ringW, roundPow, s, !look.contain),
                     roundPow, m_place.clocks.tiles.done(glideDur()), look.contain);
  }

  if (look.tone > 0.0)
    safeRenderRect("tile", pxb(box, s), argb(shadowCol, look.tone * e * f),
                              {.round = pxr(round, s), .roundingPower = roundPow});

  if ((framed || selected) && !look.lift) {
    // The caption was captured to the card's own width, so the pill measures the
    // texture it draws — not the uncut entry it used to.
    //
    // The pill shows WHATEVER there is: the title, or the carousel's n / total, or both.
    // Gating it on a title used to hide the counter outright on a window with no caption,
    // which is the one card where the position is least obvious and most wanted.
    const bool hasLabel = t.label && t.label->m_size.x > 0;
    // The counter is its own pill UNDER the caption, not a stem on it. The stem shape was
    // tried and abandoned: two translucent shapes cannot share a silhouette without either a
    // gap at the join (Hyprland rounds all four corners with one radius) or a density step
    // where they overlap -- an opaque shoulder under the pill measured 0.086 brighter over
    // the stem than beside it, and any two @0.8 layers are 0.069 apart. Neither reads as one
    // piece, so it is two pills with their own knobs instead.
    const bool wantCounter = look.counter && m_scene.tiles().size() > 1;
    // Built in the scene pass, not here: paint only reads, and the first frame for a new
    // string used to allocate a texture mid-pass. The constant colour is not optional --
    // PixelCache keys on (text, maxW) and bakes the FIRST frame's colour, so a fading one
    // here would bake the counter dim and leave it dim. The fade rides the draw alpha.
    auto countTex = wantCounter ? t.countLabel : SP<Render::ITexture>{};
    const bool hasCounter = countTex && countTex->m_size.x > 0;
    if (hasLabel || hasCounter) {
    // Each pill is sized off the texture it draws plus ITS OWN padding, so the two can be
    // set independently; the counter's size comes from the run it counts, not from the title.
    const double lw   = hasLabel ? t.label->m_size.x : 0.0;
    const double lh   = hasLabel ? t.label->m_size.y : 0.0;
    const double countW = hasCounter ? countTex->m_size.x : 0.0;
    const double countH = hasCounter ? countTex->m_size.y : 0.0;
    const double padX = cfg::look.title_pad_x, padY = cfg::look.title_pad_y;
    const double cPadX = cfg::look.counter_pad_x, cPadY = cfg::look.counter_pad_y;
    // With no title there is no pill to hang anything off, so the counter stands alone --
    // and stands alone it sizes itself, not to a width it has no reason to match.
    const double textW = hasLabel ? lw : countW;
    const double textH = hasLabel ? lh : countH;
    const double pw   = textW + 2 * (hasLabel ? padX : cPadX);
    const double ph   = textH + 2 * (hasLabel ? padY : cPadY);
    const double cGap = hasLabel ? cfg::look.counter_gap : 0.0;
    // The counter rides with the caption, so the clamp that keeps the pair on screen has to
    // reserve its height too -- otherwise a card near the bottom shoves the number off the
    // panel. Solved from sizes, which do not depend on where the block lands.
    const double counterH = hasLabel && hasCounter ? countH + 2 * cPadY : 0.0;
    const double stackGap = counterH > 0.0 ? cGap : 0.0;
    const double margin = hasLabel ? cfg::look.title_margin : cfg::look.counter_margin;
    const double px = render::insetWithin(box.cx() - pw / 2.0, pw, m->m_size.x, 6.0);
    // Each knob names a card EDGE, nothing else. On one edge the counter is always the outer
    // pill -- above the card it leads, below the card it trails -- so the pair reads the same
    // way whichever way round the card is; split across two edges there is no order to speak
    // of, and each pill simply hangs off its own.
    const bool titleBelow = cfg::look.title_position.get() != "top";
    const std::string cpos = cfg::look.counter_position.get();
    const bool counterBelow = cpos.empty() ? titleBelow : cpos != "top";
    const bool sameEdge = counterH <= 0.0 || counterBelow == titleBelow;
    const render::CaptionPills pos =
        render::captionPills(box.y, box.h, ph, counterH, cfg::look.title_gap, stackGap,
                             titleBelow, m->m_size.y, margin);
    const double counterY =
        sameEdge ? pos.counterY
                 : render::captionPillY(box.y, box.h, counterH, cfg::look.title_gap,
                                        counterBelow, m->m_size.y, cfg::look.counter_margin);
    // Yields like every other chrome element when a preview reaches it -- but AFTER the
    // clamp above, and deliberately unclamped itself: the clamp exists to keep the caption on
    // screen while the card is laid out, while the push is what shoves the pill clear. With
    // the clamp applied last the pill travelled to the screen edge, stopped there and just
    // faded (measured on exit).
    //
    // EACH pill pushes on its OWN box. The union was measured displacing the pair by 60px
    // sideways with no preview over either of them: on opposite edges the union spans the
    // whole card column, so every preview on the card touched it and a centred counter sat
    // visibly off centre for as long as the cursor was anywhere near.
    const LRect pill{px, pos.titleY, pw, ph};
    // Centred on the MONITOR, not on the pill. It counts the whole run, and the run's centre
    // is the only centre it has; riding the pill's left edge instead meant a long title
    // clamped at the screen edge dragged the number off with it. Clamped the same way, so a
    // wide counter on a narrow monitor still lands inside.
    const double cW = countW + 2 * cPadX;
    const LRect counter{render::insetWithin(m->m_size.x / 2.0 - cW / 2.0, cW, m->m_size.x, 6.0),
                        counterY, cW, counterH};
    const Vector2D push = pushOut(pill);
    const Vector2D cPush = pushOut(counter);
    // Traced before the push AND after it: the two differ, and reading only the drawn box
    // cannot tell a misplacement apart from a preview shoving a correctly placed pill off
    // the screen -- which is the whole difference between the two bugs this found.
    debug::trace::ring("title_pill", pill,
                       pxr(render::pillRadius(cfg::look.title_round, pw, ph), s), e * f);
    debug::trace::ring("counter_pill", counter, 0, e * f);
    debug::trace::ring("title_card", box, pxr(round, s), e * f);
    debug::trace::ring("caption_push", LRect{push.x, push.y, cPush.x, cPush.y}, 0, e * f);
    const double ux = pill.x + push.x;
    const double uy = pill.y + push.y;
    if (counterH > 0.0) {
      const LRect cAt{counter.x + cPush.x, counter.y + cPush.y, counter.w, counter.h};
      // The configured radius at the run's own roundingPower. NOT 1.0: the cut is
      // distanceWithRounding (src/render/shaders/glsl/rounding.glsl:7), a p-norm of the offset
      // from the corner centre, and p = 1 is the manhattan metric -- a diamond, not an arc.
      safeRenderRect("tile", pxb(CBox(cAt.x, cAt.y, cAt.w, cAt.h), s),
                              cfg::colors.counter_pill.get(e * f),
                              {.round = pxr(render::pillRadius(cfg::look.counter_round, cAt.w, cAt.h), s),
                               .roundingPower = roundPow});
      g_pHyprOpenGL->renderTexture(
          countTex,
          pxb(LRect{cAt.x + (cAt.w - countW) / 2.0, cAt.y + cPadY, countW, countH}, s),
          {.a = static_cast<float>(e * f)});
    }
    debug::trace::ring("title_pill_drawn", LRect{ux, uy, pw, ph},
                       pxr(render::pillRadius(cfg::look.title_round, pw, ph), s), e * f);
    safeRenderRect("tile", pxb(CBox(ux, uy, pw, ph), s),
                              cfg::colors.title_pill.get(e * f),
                              {.round = pxr(render::pillRadius(cfg::look.title_round, pw, ph), s),
                               .roundingPower = roundPow});
    if (hasLabel)
      g_pHyprOpenGL->renderTexture(t.label, pxb(CBox(ux + padX, uy + padY, lw, lh), s),
                                 {.a = static_cast<float>(e * f)});
    else
      g_pHyprOpenGL->renderTexture(
          countTex,
          pxb(LRect{ux + (pw - countW) / 2.0, uy + cPadY, countW, countH}, s),
          {.a = static_cast<float>(e * f)});
    }
  }

  const auto closeGlyph = m_pixel.closeGlyph();
    if (look.button && !look.lift && closeGlyph && closeGlyph->m_size.x > 0) {
    const LRect br = closeButtonRect(box);
    safeRenderRect("tile", pxb(br, s), cfg::colors.close_button.get(e),
                              {.round = pxr(br.h / 2.0, s)});
    const double gw = closeGlyph->m_size.x, gh = closeGlyph->m_size.y;
    const double gs = std::min((br.w * cfg::look.close_glyph_fill) / std::max(1.0, gw),
                               (br.h * cfg::look.close_glyph_fill) / std::max(1.0, gh));
    g_pHyprOpenGL->renderTexture(
        closeGlyph,
        pxb(LRect{br.x + (br.w - gw * gs) / 2.0, br.y + (br.h - gh * gs) / 2.0,
                  gw * gs, gh * gs}, s),
        {.a = static_cast<float>(e)});
  }
}

void Overview::renderTiles() const {
  const auto m = m_monitor.lock();
  if (!m)
    return;
  // GL_BLEND left off by a previous pass writes the ✕ glyph's transparent padding
  // through as opaque black; restore the exact func.
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
    // A tab has no window, and this used to be where it ended: `!w` was true for every tab,
    // so a tabs run drew nothing at all -- and drew it silently, because "no window" is also
    // what a window that has gone looks like, and the two were not told apart.
    if (m_tabs.isTab(t.subj)) {
      if (swapfxActiveSubject(t.subj, false))
        continue;
    } else {
      const auto w = tileWindow(t);
      if (!w || !w->m_isMapped || w->isHidden() || swapfxActive(w, false))
        continue; // flying via a landing: its slot is painted by renderSwapFX
    }
    TileLook look;
    look.box = tileRect(i);
    look.button = wantButtons;
    drawTile(i, look);
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
  const double eOut = curves::eval(m_place.recipes.ghost.curve, p); // 0..1 gone

  for (const auto &g : m_fx.ghosts()) {
    const auto w = m_windows.windowOf(g.subj);
    if (!w || !w->m_isMapped || w->isHidden())
      continue;
    const double k  = 1.0 - 0.3 * eOut; // shrink toward its own center
    const double cx = g.box.x + g.box.w / 2.0, cy = g.box.y + g.box.h / 2.0;
    const LRect box{cx - g.box.w * k / 2.0, cy - g.box.h * k / 2.0,
                    g.box.w * k, g.box.h * k};
    LRect slidBox = box;
    float gAlpha = static_cast<float>((1.0 - eOut) * 0.85);
    // Same three branches as the entry, and the same order: a slide moves the box, a fade
  // leaves it alone and lets gAlpha below do the work, and a pop scales it. Checking a
  // style by equality against each member is what let `fade` reach none of them.
  if (m_place.recipes.exitStyle.travel == anim::Travel::Slide && m_view.wsSlideDir() != 0) {
    if (m_place.recipes.exitStyle.axis == anim::Axis::Horizontal)
      slidBox.x -= static_cast<double>(m_view.wsSlideDir()) * m->m_size.x * eOut;
    else
      slidBox.y -= static_cast<double>(m_view.wsSlideDir()) * m->m_size.y * eOut;
  }
    const CBox px = pxb(slidBox, scale);
    renderWindowLive(w, m, px, px, gAlpha, when,
                     contentRadiusPx(cfg::look.preview_round,
                                     cfg::look.border_size, roundPow, scale),
                     roundPow, true);
  }
}

} // namespace gloview

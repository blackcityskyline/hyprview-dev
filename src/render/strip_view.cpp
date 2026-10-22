#include <algorithm>
#include <cmath>

#include <linux/input-event-codes.h>

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "../debug/log.hpp"
#include "gl_util.hpp"
#include "../config/config.hpp"
#include "../overview.hpp"
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "window_content.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {

CBox box(const LRect &r) { return CBox{r.x, r.y, r.w, r.h}; }

}

// Z2 strip chrome. Thumbnails follow in renderStripWindows, hints/close in
// renderStripButtons: anything that sits ON TOP of content must come later.
void Overview::renderStrip() const {
  const auto m = m_monitor.lock();
  if (!m || m_strip.empty())
    return;
  const double e = eased();
  // Windowed exit holds the overlay PAST the animation, so gate on blurAlpha(), not e.
  const double stripE = blurAlpha();
  if (stripE <= 0.01)
    return;
  const double s = m->m_scale;
  const int   previewRound = cfg::look.preview_round;
  const float roundPow     = cfg::look.preview_round_power;


  // Chrome, not backdrop: blurAlpha() is pinned to 1.0 on a close, so the band leaves on the cards' own e.
  const double bandE = opening() ? stripE : e;
  auto bandCol = cfg::colors.strip_band.get(bandE);
  bandCol.a = render::stripBandAlpha(cfg::strip.band_opacity.get(), bandE);

  const LRect bandR  = stripBand();
  const Vector2D slide = stripOffset(e);
  // STRIPBAND: offset, alpha and the rect they came from -- two causes of a blink look alike.
  debug::trace::ring("strip_band", LRect{slide.x, slide.y, bandCol.a, bandE}, 0, bandE);
  debug::trace::ring("strip_band_rect", stripBand(), 0, bandE);
  safeRenderRect("band", pxb(CBox(bandR.x + slide.x, bandR.y + slide.y, bandR.w, bandR.h), s),
                 bandCol, {});

  const int  cardRound = cfg::strip.card_round;
  const auto cardBg    = cfg::colors.strip_card.get(e);
  const auto activeBg  = cfg::colors.strip_active.get(e);
  const auto activeLine = cfg::colors.strip_active_border.get(e);
  const auto hoverLine  = cfg::colors.strip_hover.get(e);
  const auto plusCol    = cfg::colors.strip_plus.get(e);
  const auto allCol     = cfg::colors.strip_all.get(e);
  const bool allWs       = showAllWorkspaces();
  const bool allCardShown = cfg::strip.all_card != 0;


  for (size_t i = 0; i < m_strip.size(); ++i) {
    const auto &it = m_strip[i];
    const bool suppressContent =
        m_drag.lifted() &&
        m_drag.press() == model::Drag::Press::StripWin &&
        static_cast<int>(i) == m_drag.idx();
    const bool hover = static_cast<int>(i) == m_scene.hoveredStrip();
    LRect card = stripCardAt(i);
    if (m_newCardAnim && it.id == m_newCardId &&
        it.kind != model::StripItem::Kind::Plus && it.kind != model::StripItem::Kind::All)
      card = render::scaledAboutCentre(card, newCardScale());
    const CBox c = box(card);

    const bool actLike =
        it.active || (allWs && allCardShown && it.kind == model::StripItem::Kind::All);
    const bool expoRing =
        allWs && !allCardShown && it.kind != model::StripItem::Kind::Plus;
    const bool ring = actLike || expoRing;
    if (ring || hover) {
      const auto &lc = ring ? activeLine : hoverLine;
      strokeRing(card, s, lc, actLike ? 3 : 2, cardRound, roundPow);
      debug::trace::ring("strip_card", card, cardRound, 1.0);
    }

    // The All card skips the active fill: its glyph merges into it otherwise.
    const bool allSkipFill = actLike && it.kind == model::StripItem::Kind::All;
    safeRenderRect("strip", pxb(c, s), (actLike && !allSkipFill) ? activeBg : cardBg,
             {.round = pxr(cardRound, s), .roundingPower = roundPow});

    if (it.kind == model::StripItem::Kind::Plus) {
      const auto glyph = render::plusGlyph(card);
      if (glyph.drawable) {
        safeRenderRect("plusH", pxb(box(glyph.horizontal), s), plusCol,
                 {.round = pxr(glyph.horizontal.h / 2, s)});
        safeRenderRect("plusV", pxb(box(glyph.vertical), s), plusCol,
                 {.round = pxr(glyph.vertical.w / 2, s)});
      } else
        debug::dbg("plus glyph skipped: card " + std::to_string(card.w) + "x" +
                   std::to_string(card.h) + " at " + std::to_string(card.x) +
                   "," + std::to_string(card.y));
    } else if (it.kind == model::StripItem::Kind::All) {
      const auto glyph = render::allGlyph(card);
      for (const auto &cell : glyph.cells)
        safeRenderRect("all", pxb(box(cell), s), allCol, {.round = pxr(2, s)});
    } else {
      if (cfg::strip.wallpaper != 0) {
        SP<Render::ITexture> tex = backdropSourceTexture();
        if (tex && tex->ok()) {
          const double texW = std::max(1.0, static_cast<double>(tex->m_size.x));
          const double texH = std::max(1.0, static_cast<double>(tex->m_size.y));
          const auto uv = render::coverFitUV(card.w, card.h, texW, texH);
          CTexPassElement::SRenderData td{};
          td.tex           = tex;
          td.box           = pxb(c, s);
          td.a             = static_cast<float>(e);
          td.round         = pxr(cardRound, s);
          td.roundingPower = roundPow;
          td.allowCustomUV = true;
          auto &uvTL = g_pHyprRenderer->m_renderData.primarySurfaceUVTopLeft;
          auto &uvBR = g_pHyprRenderer->m_renderData.primarySurfaceUVBottomRight;
          uvTL = Vector2D{uv.u0, uv.v0};
          uvBR = Vector2D{uv.u1, uv.v1};
          g_pHyprRenderer->draw(td, g_pHyprRenderer->m_renderData.damage);
          uvTL = Vector2D{-1.0, -1.0};
          uvBR = Vector2D{-1.0, -1.0};
        }
      }
      if (!suppressContent) {
        for (size_t j = 0; j < it.wins.size(); ++j) {
          const auto &sw = it.wins[j];
          const auto w = m_windows.windowOf(sw.subj);
          if (!w || !w->m_isMapped || w->isHidden())
            continue;
          const LRect wbL = stripWinSlotRect(it, card, j);
          // NaN slips past stripWinSlotRect's max() clamps and RASSERTs later.
          if (!std::isfinite(wbL.w) || !std::isfinite(wbL.h) || wbL.w < 1 ||
              wbL.h < 1) {
            debug::dbg("renderStrip BAD slot: " + std::to_string(wbL.w) + "x" +
                       std::to_string(wbL.h) + " at " + std::to_string(wbL.x) +
                       "," + std::to_string(wbL.y) + " rel=" +
                       std::to_string(sw.rel.w) + "x" +
                       std::to_string(sw.rel.h) + " card=" +
                       std::to_string(card.w) + "x" + std::to_string(card.h));
            continue;
          }
          const int wRound = clampRound(previewRound, wbL.w, wbL.h);
          if (stripSlotGrabbed(i, j)) {
            strokeRing(wbL, s, cfg::colors.hover_border.get(e), 2, wRound, roundPow);
            debug::trace::ring("strip_hover", wbL, wRound, e);
          }
        }
      }
    }

    if (it.label && it.label->m_size.x > 0) {
      const LRect lb = render::stripLabelBox(card, it.label->m_size.x, it.label->m_size.y,
                                              cfg::strip.label_band, cfg::strip.label_extra);
      const float la =
          it.active ? static_cast<float>(e) : static_cast<float>(e) * 0.75F;
      g_pHyprOpenGL->renderTexture(it.label, pxb(box(lb), s), {.a = la});
    }
  }
}

bool Overview::stripSlotGrabbed(size_t i, size_t j) const {
  return m_drag.press() == model::Drag::Press::StripWin && !m_drag.lifted() &&
         static_cast<int>(i) == m_drag.idx() && static_cast<int>(j) == m_drag.winIdx();
}

void Overview::renderStripWindows() const {
  const auto m = m_monitor.lock();
  if (!m || m_strip.empty())
    return;
  const double e = eased();
  if (e <= 0.01)
    return;
  const double scale = m->m_scale;
  const auto when    = Time::steadyNow();
  const int previewRound = cfg::look.preview_round;
  const float roundPow   = cfg::look.preview_round_power;

  for (size_t i = 0; i < m_strip.size(); ++i) {
    const auto &it = m_strip[i];
    if (it.kind == model::StripItem::Kind::Plus || it.kind == model::StripItem::Kind::All)
      continue;
    LRect card = stripCardAt(i);
    for (size_t j = 0; j < it.wins.size(); ++j) {
      if (m_drag.lifted() &&
          m_drag.press() == model::Drag::Press::StripWin &&
          static_cast<int>(i) == m_drag.idx() &&
          static_cast<int>(j) == m_drag.winIdx())
        continue;
      const auto w = m_windows.windowOf(it.wins[j].subj);
      if (!w || !w->m_isMapped || w->isHidden())
        continue;
      if (swapfxActive(w, true))
        continue;
      const LRect slot = stripWinSlotRect(it, card, j);
      if (slot.w < 1.0 || slot.h < 1.0)
        debug::dbg("strip slot dropped: card " + std::to_string(i) + " slot " +
                   std::to_string(j) + " rel=" + std::to_string(it.wins[j].rel.w) +
                   "x" + std::to_string(it.wins[j].rel.h) + " -> " +
                   std::to_string(slot.w) + "x" + std::to_string(slot.h));
      const int round = clampRound(previewRound, slot.w, slot.h);
      const CBox slotPx = pxb(slot, scale);
      const CBox cardPx = pxb(card, scale);
      if (slot.w >= 1.0 && slot.h >= 1.0 && it.ws && it.ws->m_isSpecialWorkspace)
        debug::dbg("strip special slot: " + std::to_string(slotPx.w) + "x" +
                   std::to_string(slotPx.h) + "px of " + std::to_string(slot.w) + "x" +
                   std::to_string(slot.h) + " card=" + std::to_string(card.w) + "x" +
                   std::to_string(card.h));
      // Grab ring or plain border: whichever is on top, the content radius follows it.
      const int ringW = stripSlotGrabbed(i, j) ? 2 : cfg::look.border_size;
      renderWindowLive(w, m, slotPx, cardPx, static_cast<float>(e), when,
                       contentRadiusPx(round, ringW, scale),
                       roundPow, true);
    }
  }
}

void Overview::renderStripButtons() const {
  const auto m = m_monitor.lock();
  if (!m || m_strip.empty())
    return;
  const double e = eased();
  if (e <= 0.01)
    return;
  const double s = m->m_scale;
  const int cardRound = cfg::strip.card_round;
  const float roundPow = cfg::look.preview_round_power;
  const bool hoverOnly = closeOnHover();
  const bool travelling = m_drag.travelSq() > 64.0;
  const bool dropping = m_drag.armed() && m_drag.lifted() && travelling;
  const bool isRmb = dropping && m_drag.button() == BTN_RIGHT;
  // Both buttons swap on a slot drop, so slot rings apply to either.
  const bool rmbSwap = dropping && m_drag.press() == model::Drag::Press::StripWin;

  if (rmbSwap && m_drag.idx() >= 0 &&
      m_drag.idx() < static_cast<int>(m_strip.size()) &&
      m_drag.winIdx() < static_cast<int>(m_strip[m_drag.idx()].wins.size())) {
    const LRect rmbSlot = stripWinSlotRect(m_strip[m_drag.idx()],
                                            stripCardAt(m_drag.idx()),
                                            m_drag.winIdx());
    strokeRingPx(pxb(rmbSlot, s),
                 cfg::colors.select_border.get(e),
                 0.7F * static_cast<float>(e),
                 pxr(clampRound(cfg::look.preview_round, rmbSlot.w, rmbSlot.h), s),
                 roundPow);
  }

  const auto dragW = m_windows.windowOf(m_drag.win());

  for (size_t i = 0; i < m_strip.size(); ++i) {
    const auto &it = m_strip[i];
    if (it.kind == model::StripItem::Kind::Plus || it.kind == model::StripItem::Kind::All)
      continue;
    const LRect card = stripCardAt(i);

    if (dropping) {
      const auto hoverCol = argb(cfg::colors.hover_border.get(e));
      const auto hintCol  = argb(cfg::colors.drop_hint.get(e));

      if (isRmb) {
        for (size_t j = 0; j < it.wins.size(); ++j) {
          const auto v = m_windows.windowOf(it.wins[j].subj);
          if (!v || v == dragW || !v->m_isMapped || v->isHidden())
            continue;
          const LRect sl = stripWinSlotRect(it, card, j);
          if (!sl.contains(m_drag.x(), m_drag.y()))
            continue;
          const int sr = clampRound(cfg::look.preview_round, sl.w, sl.h);
          safeRenderRect("rmbswaphint",
                         pxb(CBox{sl.x, sl.y, sl.w, sl.h}, s),
                         hintCol,
                         {.round = pxr(sr, s), .roundingPower = roundPow});
          strokeRingPx(pxb(sl, s), hoverCol, 0.9F * static_cast<float>(e), pxr(sr, s), roundPow);
          debug::trace::ring("strip_slot_a", sl, sr, e);
          break;
        }
      } else if (static_cast<int>(i) == m_scene.hoveredStrip()) {
        const bool dwindle = cfg::behavior.tile_layout.get() == "dwindle";
        bool slotFound = false;
        for (size_t j = 0; j < it.wins.size(); ++j) {
          const auto v = m_windows.windowOf(it.wins[j].subj);
          if (!v || v == dragW)
            continue;
          const LRect sl = stripWinSlotRect(it, card, j);
          if (!sl.contains(m_drag.x(), m_drag.y()))
            continue;
          slotFound = true;
          const int sr = clampRound(cfg::look.preview_round, sl.w, sl.h);
          if (dwindle) {
            const LRect half = render::dwindleHalf(sl, m_drag.x(), m_drag.y());
            const int hr = clampRound(cfg::look.preview_round, half.w, half.h);
            safeRenderRect("dwindlehalf",
                           pxb(CBox{half.x, half.y, half.w, half.h}, s),
                           hintCol,
                           {.round = pxr(hr, s), .roundingPower = roundPow});
            strokeRingPx(pxb(sl, s), hoverCol, 0.45F * static_cast<float>(e), pxr(sr, s), roundPow);
            debug::trace::ring("strip_slot_b", sl, sr, e);
          } else {
            strokeRingPx(pxb(sl, s), hoverCol,
                         0.9F * static_cast<float>(e), pxr(sr, s), roundPow);
          }
          break;
        }
        if (!slotFound && it.wins.empty()) {
          safeRenderRect("emptycard", pxb(card, s), hintCol,
                         {.round = pxr(cardRound / 2, s), .roundingPower = roundPow});
        }
      }
    }

    if (closeButtonsAlwaysOn() ||
        (hoverOnly ? static_cast<int>(i) == m_scene.hoveredStrip()
                   : m_view.desktop())) {
      const LRect br = closeButtonRect(card);
      const double rad = br.w / 2.0;
      safeRenderRect("closebtn", 
          pxb(box(br), s),
          cfg::colors.close_button.get(e),
          {.round = pxr(rad, s)});
      const auto closeGlyph = m_pixel.closeGlyph();
      if (closeGlyph && closeGlyph->m_size.x > 0) {
        const auto glyph = render::glyphRect(br, closeGlyph->m_size.x, closeGlyph->m_size.y,
                                             cfg::look.close_glyph_fill);
        g_pHyprOpenGL->renderTexture(closeGlyph, pxb(glyph, s), {.a = static_cast<float>(e)});
      }
    }
  }
}

}

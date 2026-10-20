#include <algorithm>
#include <cctype>
#include <cmath>

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

#include "geometry.hpp"
#include "gl_util.hpp"
#include "tile_look.hpp"
#include "../config/config.hpp"
#include "../overview.hpp"
#include "../clock/curves.hpp"
#include "window_content.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {
// Down-right of the pointer, so the drop zones stay visible under the preview. Functions, not
// namespace-scope constants: a config value read before PLUGIN_INIT crashed the session (2.5).
double dragOffsetX() { return cfg::look.drag_offset_x; }
double dragOffsetY() { return cfg::look.drag_offset_y; }
double dragScale() {
  return std::clamp(static_cast<double>(cfg::look.drag_size), 0.15, 1.0);
}
}

LRect Overview::dragBox() const {
  const int dragIdx = draggedTile();
  if (dragIdx < 0)
    return LRect{0, 0, 0, 0};
  const LRect base = m_scene.tiles()[dragIdx].target;
  if (m_view.desktop())
    return LRect{m_drag.x() - m_drag.grabX(), m_drag.y() - m_drag.grabY(),
                 base.w, base.h};
  const LRect full = tileBox(static_cast<size_t>(dragIdx), base);
  const double k   = dragScale();
  return LRect{m_drag.x() + dragOffsetX(), m_drag.y() + dragOffsetY(), full.w * k, full.h * k};
}

LRect Overview::dragStripBox() const {
  if (m_windows.windowOf(m_drag.win()) == nullptr)
    return LRect{0, 0, 0, 0};
  const double sw    = m_drag.fromBox().w > 1.0 ? m_drag.fromBox().w : 150.0;
  const double ww    = sw * dragScale();
  const auto size    = m_windows.windowOf(m_drag.win())->sizeAnimation()->goal();
  const double aspect = (size.x > 0 && size.y > 0) ? size.x / size.y : 16.0 / 9.0;
  return LRect{m_drag.x() + dragOffsetX(), m_drag.y() + dragOffsetY(), ww, ww / std::max(0.1, aspect)};
}

double Overview::dragLiftProgress() const {
  return curves::eval(m_place.recipes.lift.curve, m_place.clocks.dragLift.raw(m_place.recipes.lift.ms));
}

LRect Overview::dragVisualBox() const {
  const LRect to = m_drag.press() == model::Drag::Press::Tile ? dragBox()
                                                               : dragStripBox();
  return render::dragVisualBox(m_drag.fromBox(), to, m_drag.lifted(), dragLiftProgress(),
                               anim::param(anim::Param::LiftScale), anim::param(anim::Param::LiftRise));
}

void Overview::renderDragTile() const {
  const int dragIdx = draggedTile();
  if (dragIdx < 0) {
    if (m_drag.press() == model::Drag::Press::StripWin && m_windows.windowOf(m_drag.win()) != nullptr)
      drawDragStripChrome();
    return;
  }
  TileLook look;
  look.box   = dragVisualBox();
  look.lift  = true;
  look.round = clampRound(cfg::look.preview_round, look.box.w, look.box.h);
  drawTile(static_cast<size_t>(dragIdx), look);
}

void Overview::drawDragStripChrome() const {
  const auto m = m_monitor.lock();
  const auto w = m_windows.windowOf(m_drag.win());
  if (!m || !w)
    return;
  const double s        = m->m_scale;
  const double e        = eased();
  const LRect  lb       = dragVisualBox();
  const int    round    = clampRound(cfg::look.preview_round, lb.w, lb.h);
  const float  roundPow = cfg::look.preview_round_power;
  const auto   shadowCol = cfg::colors.shadow.get(1.0);
  const auto   hoverCol  = cfg::colors.hover_border.get(e);

  // The drag box can be empty: the guarded wrappers in gl_util.hpp exist for exactly this.
  dropShadow(lb, s, shadowCol, 14.0, 30.0, static_cast<float>(e * 0.18), round,
             roundPow);

  // A stroke, not a filled underlay: a fill shows through the window's transparency.
  const int th = cfg::look.hover_border_size; // 0 = no ring
  strokeRing(lb, s, hoverCol, th, round, roundPow);
  debug::trace::ring("drag_tile", lb, round, hoverCol.a);
}

void Overview::renderDragWindow() const {
  if (draggedTile() >= 0)
    return; // drawn whole by renderDragTile
  const auto m = m_monitor.lock();
  const auto w = m_windows.windowOf(m_drag.win());
  if (!m || !w || !w->m_isMapped || w->isHidden())
    return;
  if (m_drag.press() != model::Drag::Press::StripWin)
    return;

  const double scale = m->m_scale;
  const float  roundPow = cfg::look.preview_round_power;
  const LRect  lb    = dragVisualBox();
  const int    round = clampRound(cfg::look.preview_round, lb.w, lb.h);
  const CBox   px    = pxb(lb, scale);
  renderWindowLive(w, m, px, px, static_cast<float>(eased()), Time::steadyNow(),
                   contentRadiusPx(round, cfg::look.hover_border_size, scale),
                   roundPow);
}

LRect Overview::killZoneRect() const {
  const auto m = m_monitor.lock();
  if (!m)
    return {};
  if (cfg::behavior.kill_zone == 0)
    return {};
  return render::killZoneRect(stripAnchor(), m->m_size.x, m->m_size.y,
                               cfg::behavior.kill_zone_size);
}

double Overview::killZoneGlow(double lx, double ly) const {
  return render::killZoneGlow(killZoneRect(), lx, ly, cfg::behavior.kill_zone_falloff);
}

void Overview::renderKillZone() const {
  const auto m = m_monitor.lock();
  if (!m || m_killGlow <= 0.004)
    return;
  const LRect z = killZoneRect();
  if (z.w < 1.0 || z.h < 1.0)
    return;

  const double s    = m->m_scale;
  const double glow = m_killGlow;
  const auto   col  = cfg::colors.select_border.get(eased());
  // ONE falloff from the SCREEN edge: two ramps put a step where they meet.
  const int kReach = cfg::behavior.kill_zone_reach;
  for (const auto &step : render::killZoneRamp(z, m->m_size.x, m->m_size.y, kReach)) {
    const double a = step.alpha * glow;
    if (a < 0.004)
      continue;
    safeRenderRect("fx", pxb(step.line, s), argb(col, a), {});
  }
}

// A subject, not a window: a pulse rings whatever the run shows, and a tab's card can ring.
void Overview::kickPulse(model::SubjectRef s) {
  if (s == model::SubjectRef{} || !anim::cfg(anim::Id::Pulse).on)
    return;
  m_fx.kickPulse(s);
}

void Overview::renderPulses(bool strip) const {
  if (m_fx.pulses().empty())
    return;
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const double s = m->m_scale;
  const float roundPow = cfg::look.preview_round_power;
  const auto col = cfg::colors.hover_border.get(1.0);
  for (const auto &p : m_fx.pulses()) {
    const auto w = m_windows.windowOf(p.subj);
    if (!w)
      continue;
    const double pr = std::clamp(p.p, 0.0, 1.0);
    if (strip) {
      for (size_t i = 0; i < m_strip.size(); ++i) {
        const auto &it = m_strip[i];
        if (it.kind != model::StripItem::Kind::Ws)
          continue;
        for (size_t j = 0; j < it.wins.size(); ++j) {
          if (m_windows.windowOf(it.wins[j].subj) != w)
            continue;
          const LRect card = stripCardAt(i);
          const LRect sl = stripWinSlotRect(it, card, j);
          strokeRingPx(pxb(sl, s), col,
                       static_cast<float>((1.0 - pr) * 0.9),
                       pxr(clampRound(cfg::look.preview_round,
                                      sl.w, sl.h), s),
                       roundPow);
          debug::trace::ring("pulse_strip", sl,
                             clampRound(cfg::look.preview_round, sl.w, sl.h), 1.0 - pr);
        }
      }
    } else {
      for (size_t i = 0; i < m_scene.tiles().size(); ++i) {
        if (tileWindow(m_scene.tiles()[i]) != w)
          continue;
        const LRect lb = tileRect(i);
        drawPulseRing(pxb(lb, s),
                      pxr(clampRound(cfg::look.preview_round, lb.w, lb.h), s),
                      roundPow, col, pr);
        debug::trace::ring("pulse_grid", lb,
                           clampRound(cfg::look.preview_round, lb.w, lb.h), pr);
        break;
      }
    }
  }
}

void Overview::drawPulseRing(const CBox &boxPx, int round, float roundPow,
                             const CHyprColor &col, double p) const {
  const double k  = curves::eval(anim::cfg(anim::Id::Pulse).curve, p) *
                  anim::param(anim::Param::PulseScale);
  const double cx = boxPx.x + boxPx.w / 2.0, cy = boxPx.y + boxPx.h / 2.0;
  const CBox ring{cx - boxPx.w * (0.5 + k), cy - boxPx.h * (0.5 + k),
                  boxPx.w * (1.0 + 2.0 * k), boxPx.h * (1.0 + 2.0 * k)};
  strokeRingPx(ring, col, static_cast<float>((1.0 - p) * 0.9),
               static_cast<int>(std::max(2.0, round + round * 0.5 * k)),
               roundPow);
}

// One resolver for painter and trace, or the trace reports a motion that never happened.
std::vector<LRect> Overview::swapDests(const PHLWINDOW &w, bool strip) const {
  std::vector<LRect> out;
  if (!strip) {
    for (size_t i = 0; i < m_scene.tiles().size(); ++i)
      if (tileWindow(m_scene.tiles()[i]) == w)
        out.push_back(tileRect(i));
    return out;
  }
  for (size_t i = 0; i < m_strip.size(); ++i) {
    const auto &it = m_strip[i];
    if (it.kind != model::StripItem::Kind::Ws)
      continue;
    for (size_t j = 0; j < it.wins.size(); ++j)
      if (m_windows.windowOf(it.wins[j].subj) == w)
        out.push_back(stripWinSlotRect(it, stripCardAt(i), j));
  }
  return out;
}

void Overview::renderSwapFX() const {
  if (m_fx.swapfx().empty())
    return;
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const double s       = m->m_scale;
  const auto when      = Time::steadyNow();
  const int  round     = cfg::look.preview_round;
  const float roundPow = cfg::look.preview_round_power;

  for (const auto &fx : m_fx.swapfx()) {
    const auto w = m_windows.windowOf(fx.subj);
    if (!w || !w->m_isMapped || w->isHidden())
      continue;
    std::vector<LRect> dests = swapDests(w, fx.toStrip);
    if (dests.empty())
      continue;

    const double t = fx.clock.raw(fx.ms);
    const double p = curves::eval(fx.curve, t);
    const bool vertical = fx.style.travel == anim::Travel::Slide &&
                          fx.style.axis == anim::Axis::Vertical;
    render::SwapFlightInput flight;
    flight.travel = fx.style.travel;
    flight.axis = fx.style.axis;
    flight.t = t;
    flight.p = p;
    flight.lift = cfg::look.swap_lift;
    if (vertical) {
      flight.q1 = curves::eval(fx.curve, t / 0.5);
      flight.q2 = curves::eval(fx.curve, (t - 0.5) / 0.5);
    }
    if (fx.style.travel == anim::Travel::Pop)
      flight.popK = 0.7 + 0.3 * curves::eval(anim::cfg(anim::Id::Card).curve, (t - 0.3) / 0.7);
    for (const LRect &to : dests) {
    // No ring here: the pulse owns rings.
    flight.from = fx.from;
    flight.to = to;
    const auto moved = render::swapFlight(flight);
    const LRect box = moved.box;
    const float alpha = static_cast<float>(moved.alpha);

    // A flight draws at EVERY surface the window occupies, sizes orders of magnitude apart, so
    // the box can shrink past a pixel: renderRoundedShadow RASSERTs, and that takes the session.
    const CBox px = pxb(box, s);
    const int lr = clampRound(round, box.w, box.h); // logical: dropShadow scales it
    if (alpha > 0.02F)
      dropShadow(box, s, cfg::colors.shadow.get(), 6.0, 16.0,
                 static_cast<float>(0.18 * alpha), lr, roundPow);
    renderWindowLive(w, m, px, px, alpha, when,
                     contentRadiusPx(round, cfg::look.border_size, s),
                     roundPow, true);
    }
  }
}

bool Overview::isAboveLayer(const std::string &ns) const {
  return session::isAboveLayer(ns, cfg::layer.above_namespaces.get());
}

// renderLayer is renderer-protected, so opted-in TOP/OVERLAY surfaces go through the immediate leaf.
void Overview::renderAboveLayers() const {
  const auto m = m_monitor.lock();
  if (!m || !g_pHyprRenderer)
    return;
  const auto when = Time::steadyNow();
  for (int idx : {2, 3}) { // LAYER_TOP, LAYER_OVERLAY
    for (const auto &ref : m->m_layerSurfaceLayers[idx]) {
      const auto ls = ref.lock();
      if (!ls || !ls->m_mapped || !ls->wlSurface() ||
          !ls->wlSurface()->resource())
        continue;
      if (!isAboveLayer(ls->m_namespace))
        continue;

      const auto pos = ls->positionAnimation()->value();
      const auto size = ls->sizeAnimation()->value();
      if (!(size.x > 0 && size.y > 0))
        continue;

      g_pHyprRenderer->damageBox(
          CBox{pos.x, pos.y, size.x, size.y});

      CSurfacePassElement::SRenderData data{};
      data.pMonitor = m;
      data.when     = when;
      data.pos      = pos;
      data.w        = std::max(size.x, 1.0);
      data.h        = std::max(size.y, 1.0);
      data.fadeAlpha = 1.F;
      data.alpha    = 1.F;
      data.decorate = false;
      data.rounding = 0;
      data.blur     = false;
      data.surfaceCounter = 0;

      const auto root = ls->wlSurface()->resource();
      root->breadthfirst(
          [&data, &root](SP<CWLSurfaceResource> s, const Vector2D &offset,
                         void *) {
            if (!s || !s->m_current.texture || s->m_current.size.x < 1 ||
                s->m_current.size.y < 1)
              return;
            data.localPos    = offset;
            data.texture     = s->m_current.texture;
            data.surface     = s;
            data.mainSurface = s == root;
            g_pHyprRenderer->draw(data, g_pHyprRenderer->m_renderData.damage);
            data.surfaceCounter++;
          },
          nullptr);
    }
  }
}

}

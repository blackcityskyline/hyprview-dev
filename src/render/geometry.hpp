#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <span>
#include <string>

#include "../layout.hpp"

// Pure geometry, monitor-local LOGICAL px: no GL, no Hyprland, no config reads (AGENTS.md 2.15).

namespace hyprview::render {

enum class Corner {
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
};

enum class Anchor {
    Top,
    Bottom,
    Left,
    Right,
};

[[nodiscard]] inline bool isHorizontal(Anchor a) {
    return a == Anchor::Top || a == Anchor::Bottom;
}

[[nodiscard]] inline Anchor parseAnchor(const std::string& anchor,
                                        const std::string& fallback) {
    const auto& a = anchor.empty() ? fallback : anchor;
    if (a == "bottom")
        return Anchor::Bottom;
    if (a == "left")
        return Anchor::Left;
    if (a == "right")
        return Anchor::Right;
    return Anchor::Top;
}

// Band thickness. std::clamp requires !comp(hi, lo) and does not check it: offset_max is unbounded.
[[nodiscard]] inline double stripThickness(double cross, double height, double offsetMax) {
    const double hi    = cross * offsetMax;
    const double floor = std::min(100.0, hi);
    return std::clamp(height, floor, hi);
}

[[nodiscard]] inline double stripOffset(double cross, double offset) {
    return std::clamp(offset, 0.0, cross * 0.4);
}

[[nodiscard]] inline LRect stripBand(Anchor a, double W, double H, double thickness,
                                     double offset) {
    switch (a) {
        case Anchor::Bottom: return LRect{0, H - thickness - offset, W, thickness};
        case Anchor::Left:   return LRect{offset, 0, thickness, H};
        case Anchor::Right:  return LRect{W - thickness - offset, 0, thickness, H};
        case Anchor::Top:
        default:             return LRect{0, offset, W, thickness};
    }
}

// The reveal slide: the band comes in along its own edge, off-screen at e == 0.
[[nodiscard]] inline LVec2 stripSlide(Anchor a, double e, double thickness,
                                         double slideFrac) {
    const double d = (1.0 - e) * thickness * slideFrac;
    switch (a) {
        case Anchor::Bottom: return LVec2{0.0, d};
        case Anchor::Left:   return LVec2{-d, 0.0};
        case Anchor::Right:  return LVec2{d, 0.0};
        case Anchor::Top:
        default:             return LVec2{0.0, -d};
    }
}

// Full brightness inside the zone, nothing by `falloff` px away: a distance, not a bool.
[[nodiscard]] inline double killZoneGlow(const LRect& zone, double lx, double ly, double falloff) {
    if (zone.w < 1.0 || zone.h < 1.0 || falloff <= 0.0)
        return 0.0;
    const double dx = std::max({zone.x - lx, 0.0, lx - (zone.x + zone.w)});
    const double dy = std::max({zone.y - ly, 0.0, ly - (zone.y + zone.h)});
    const double d  = std::sqrt(dx * dx + dy * dy);
    const double t  = std::clamp(1.0 - d / falloff, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Scale and rise apply at every progress INCLUDING zero: a stationary hold still animates.
[[nodiscard]] inline LRect dragVisualBox(const LRect& fromBox, const LRect& to, bool lifted, double progress,
                                         double liftScale, double liftRise) {
    const double p = progress;
    const LRect  b = (p < 1.0 && lifted) ? lerp(fromBox, to, p) : to;

    const double s  = 1.0 + liftScale * p;
    const double dy = -liftRise * p;
    const double cx = b.cx(), cy = b.cy();
    return LRect{cx - b.w * s / 2.0, cy - b.h * s / 2.0 + dy, b.w * s, b.h * s};
}

[[nodiscard]] inline double captionPillY(double cardY, double cardH, double pillH,
                                         double gap, bool below, double screenH,
                                         double margin) {
    return below ? std::min(cardY + cardH + gap, screenH - pillH - margin)
                 : std::max(margin, cardY - gap - pillH);
}

// Both pills on ONE card edge; the counter is the OUTER pill either way.
struct CaptionPills {
    double titleY;
    double counterY;
};

[[nodiscard]] inline CaptionPills captionPills(double cardY, double cardH, double titleH,
                                               double counterH, double cardGap,
                                               double stackGap, bool below, double screenH,
                                               double margin) {
    const double top = captionPillY(cardY, cardH, titleH + stackGap + counterH, cardGap,
                                    below, screenH, margin);
    return below ? CaptionPills{top, top + titleH + stackGap}      // card, title, counter
                 : CaptionPills{top + counterH + stackGap, top};   // counter, title, card
}

// Negative configured = a capsule; capped at half the shorter side, or the shape turns inside out.
[[nodiscard]] inline int pillRadius(double configured, double w, double h) {
    const double cap = std::min(w, h) * 0.5;
    const double want = configured < 0.0 ? cap : std::min(configured, cap);
    return static_cast<int>(std::max(0.0, want));
}

// Keep a box inside a screen, with an inset. std::clamp requires lo <= hi and does not check it:
// a caption sized off a window title can be wider than the screen minus both insets.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
}

// First match wins: the GRID hit-test, deliberately NOT the carousel's, which needs front-most.
[[nodiscard]] inline int firstHit(double lx, double ly, const LRect *boxes,
                                   std::size_t n) {
  for (std::size_t i = 0; i < n; ++i)
    if (boxes[i].contains(lx, ly))
      return static_cast<int>(i);
  return -1;
}

[[nodiscard]] inline LRect fitInside(const LRect &outer, double aspect) {
  if (outer.w <= 0.0 || outer.h <= 0.0 || aspect <= 0.0)
    return outer;

  const double outerAspect = outer.w / outer.h;
  if (std::abs(outerAspect - aspect) <= 0.01)
    return outer;

  if (outerAspect > aspect) {
    const double w = outer.h * aspect;
    return LRect{outer.x + (outer.w - w) / 2.0, outer.y, w, outer.h};
  }

  const double h = outer.w / aspect;
  return LRect{outer.x, outer.y + (outer.h - h) / 2.0, outer.w, h};
}

[[nodiscard]] inline LRect winSlotRect(const LRect& rel, const LRect& card) {
    return LRect{card.x + rel.x * card.w, card.y + rel.y * card.h,
                 std::max(2.0, rel.w * card.w), std::max(2.0, rel.h * card.h)};
}
[[nodiscard]] inline LRect closeButtonRect(const LRect& card, double sizeScale, Corner corner) {
    const double scale = std::max(0.3, sizeScale);
    const double r     = std::clamp(std::min(card.w, card.h) * 0.11, 9.0, 18.0) * scale;
    const double inset = r + 6.0;
    const double top   = card.y + inset;
    const double right = card.x + card.w - inset;
    const double low   = card.y + card.h - inset;

    double cx = right, cy = top;
    switch (corner) {
        case Corner::TopLeft: cx = card.x + inset; cy = top; break;
        case Corner::BottomRight: cx = right; cy = low; break;
        case Corner::BottomLeft: cx = card.x + inset; cy = low; break;
        case Corner::TopRight: break;
    }
    return LRect{cx - r, cy - r, 2 * r, 2 * r};
}

[[nodiscard]] inline double pillScale(double cardW, double inset, double pillW) {
    if (pillW <= 0.0)
        return 1.0;
    return std::min(1.0, std::max(cardW - inset, 1.0) / pillW);
}

// How far a caption comes down with its card. refW is the CENTRED card, the one scale 1.0 belongs to.
[[nodiscard]] inline double cardScale(double cardW, double refW) {
    if (refW <= 0.0 || cardW <= 0.0)
        return 1.0;
    return std::clamp(cardW / refW, 0.45, 1.0);
}


struct Escape {
  double dx = 0.0;
  double dy = 0.0;
  double depth = 0.0;
};

[[nodiscard]] inline Escape escapeFrom(const LRect &self, const LRect &cover, double ox,
                                       double oy) {
  Escape e;
  if (ox < oy) {
    e.dx = std::copysign(1.0, self.cx() - cover.cx());
    e.depth = ox;
    return e;
  }
  e.depth = oy;
  const double up = std::copysign(1.0, self.cy() - cover.cy());
  if (up < 0.0 && self.y + self.h <= cover.y) {
    e.dx = std::copysign(1.0, self.cx() - cover.cx());
    e.depth = ox;
    return e;
  }
  e.dy = up;
  return e;
}


// 1.0 all of it, 0.0 gone: min() of shoved / card leaving, in ONE place; exitFrac ignored while opening.
[[nodiscard]] inline double captionRemains(double shove, double pillW, double pillH,
                                          double exitFrac, bool opening) {
  constexpr double kGoneOver = 0.25; // of its own width
  const double byShove =
      1.0 - std::clamp(shove / std::max(std::max(pillW, pillH) * kGoneOver, 1.0), 0.0, 1.0);
  if (opening)
    return byShove;
  return std::min(byShove, std::clamp(1.0 - exitFrac, 0.0, 1.0));
}

[[nodiscard]] inline LRect scaledAboutCentre(const LRect &r, double k) {
  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
  const double w = r.w * k, h = r.h * k;
  return LRect{cx - w / 2.0, cy - h / 2.0, w, h};
}

// One answer per caption: where the pills land, and what decides it.
struct CaptionSizes {
  double textW = 0.0, textH = 0.0;
  double countW = 0.0, countH = 0.0;
  bool hasLabel = false;
  bool hasCounter = false;
};

struct CaptionStyle {
  double padX = 0.0, padY = 0.0;
  double cPadX = 0.0, cPadY = 0.0;
  double cardGap = 0.0;
  double stackGap = 0.0;
  double margin = 0.0;
  double cMargin = 0.0;
  double cardRefW = 0.0; // the centred card, the one scale 1.0 belongs to
  double gridGapMin = 1.0;
  bool below = true;        // title hangs off the card's bottom edge
  bool counterBelow = true; // counter hangs off the card's bottom edge
};

struct CaptionLayout {
  LRect pill;     // pre-push, pre-collapse
  LRect counter;  // pre-push, pre-collapse; h == 0 when there is no counter
  double kFit = 1.0;
};

[[nodiscard]] inline CaptionLayout captionLayout(const LRect &box, const LRect &band,
                                                 double screenW, double screenH,
                                                 const CaptionSizes &s,
                                                 const CaptionStyle &st) {
  // Nothing to label: nothing to draw, not an empty pill with padding around it.
  if (!s.hasLabel && !s.hasCounter)
    return CaptionLayout{};
  const double textW = s.hasLabel ? s.textW : s.countW;
  const double textH = s.hasLabel ? s.textH : s.countH;
  const double padX  = s.hasLabel ? st.padX  : st.cPadX;
  const double padY  = s.hasLabel ? st.padY  : st.cPadY;
  const double counterH = s.hasLabel && s.hasCounter ? s.countH + 2 * st.cPadY : 0.0;

  const double kCard = cardScale(box.w, st.cardRefW);
  const double kFit  = std::min(kCard, pillScale(box.w, st.gridGapMin, textW + 2 * padX));
  const double pw = (textW + 2 * padX) * kFit;
  const double ph = (textH + 2 * padY) * kFit;

  const double stackGap = counterH > 0.0 ? st.stackGap : 0.0;
  const double margin   = s.hasLabel ? st.margin : st.cMargin;

  // The band is part of the edge margin: on its edge it thickens it, elsewhere not.
  const bool bandOnTop = band.y < screenH * 0.5;
  const double marginTop    = margin + (bandOnTop ? band.h : 0.0);
  const double marginBottom = margin + (bandOnTop ? 0.0 : band.h);
  const bool sameEdge = counterH <= 0.0 || st.counterBelow == st.below;

  const double cW = s.countW + 2 * st.cPadX;
  CaptionLayout lay;
  lay.kFit = kFit;
  lay.pill = LRect{insetWithin(box.cx() - pw / 2.0, pw, screenW, 6.0), 0.0, pw, ph};
  lay.counter = LRect{insetWithin(screenW / 2.0 - cW / 2.0, cW, screenW, 6.0), 0.0, cW, counterH};
  if (counterH <= 0.0) {
    lay.pill.y = captionPillY(box.y, box.h, ph, st.cardGap, st.below, screenH,
                              st.below ? marginBottom : marginTop);
    return lay;
  }
  if (sameEdge) {
    const CaptionPills pos =
        captionPills(box.y, box.h, ph, counterH, st.cardGap, stackGap, st.below, screenH,
                     st.below ? marginBottom : marginTop);
    lay.pill.y = pos.titleY;
    lay.counter.y = pos.counterY;
  } else {
    lay.pill.y = captionPillY(box.y, box.h, ph, st.cardGap, st.below, screenH,
                              st.below ? marginBottom : marginTop);
    lay.counter.y = captionPillY(box.y, box.h, counterH, st.cardGap, st.counterBelow, screenH,
                                 st.cMargin);
  }
  return lay;
}

// The escape push: out from under every cover, by the deepest overlap.
[[nodiscard]] inline LVec2 pushOut(const LRect &r, const std::span<const LRect> &covers) {
  double best = 0.0;
  LVec2 dir{0.0, 0.0};
  for (const auto &b : covers) {
    const double ox = std::min(b.x + b.w, r.x + r.w) - std::max(b.x, r.x);
    const double oy = std::min(b.y + b.h, r.y + r.h) - std::max(b.y, r.y);
    if (ox <= 0.0 || oy <= 0.0)
      continue;

    // SWALLOWED: no cheaper axis to leave by -- eject toward the nearest edge instead.
    LVec2 d{0.0, 0.0};
    double depth = 0.0;
    if (ox >= r.w && oy >= r.h) {
      double near = r.x - b.x;
      d = LVec2{-1.0, 0.0};
      if (b.x + b.w - (r.x + r.w) < near) {
        near = b.x + b.w - (r.x + r.w);
        d = LVec2{1.0, 0.0};
      }
      if (r.y - b.y < near) {
        near = r.y - b.y;
        d = LVec2{0.0, -1.0};
      }
      if (b.y + b.h - (r.y + r.h) < near) {
        near = b.y + b.h - (r.y + r.h);
        d = LVec2{0.0, 1.0};
      }
      depth = near;
    } else {
      const Escape esc = escapeFrom(r, b, ox, oy);
      depth = esc.depth;
      d = LVec2{esc.dx, esc.dy};
    }
    if (depth > best) {
      best = depth;
      dir = d;
    }
  }
  if (best <= 0.0)
    return {};
  return LVec2{dir.x * best, dir.y * best};
}

// How far the strip assembly leaves the band's edge: the deepest card penetration,
// along the anchor axis only. The caller applies it to band AND cards together.
[[nodiscard]] inline LVec2 stripPush(const LRect &band, Anchor a,
                                     const std::span<const LRect> &covers) {
  double deepest = 0.0;
  for (const auto &b : covers) {
    double pen = 0.0;
    switch (a) {
      case Anchor::Bottom: pen = (b.y + b.h) - band.y; break;
      case Anchor::Top:    pen = (band.y + band.h) - b.y; break;
      case Anchor::Left:   pen = (band.x + band.w) - b.x; break;
      case Anchor::Right:  pen = (b.x + b.w) - band.x; break;
    }
    deepest = std::max(deepest, pen);
  }
  if (deepest <= 0.0)
    return {};
  const double d = deepest;
  switch (a) {
    case Anchor::Bottom: return LVec2{0.0, d};
    case Anchor::Top:    return LVec2{0.0, -d};
    case Anchor::Left:   return LVec2{-d, 0.0};
    case Anchor::Right:  return LVec2{d, 0.0};
    default:             return LVec2{0.0, 0.0};
  }
}

} // namespace hyprview::render

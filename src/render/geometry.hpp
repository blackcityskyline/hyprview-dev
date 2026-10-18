#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>

#include "../layout.hpp"

// Pure geometry, monitor-local LOGICAL px: no GL, no Hyprland, no cfg:: reads, every value
// an argument. A strip that stops being a fixed band on a fixed edge is then a new Anchor
// value rather than a new code path (docs/ROADMAP.md), and a browser tab has no CWindow* to
// ask for a size so the caller passes the aspect it already has.

namespace gloview::render {

enum class Corner {
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
};

// Which screen edge the strip band is pinned to. A new placement is a new Anchor value,
// not a new code path -- which holds only while nothing here reads cfg::.
enum class Anchor {
    Top,
    Bottom,
    Left,
    Right,
};

[[nodiscard]] inline bool isHorizontal(Anchor a) {
    return a == Anchor::Top || a == Anchor::Bottom;
}

// `anchor` is the configured string, `fallback` the older knob read when it is empty.
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

// The band's thickness, perpendicular to the anchored edge. std::clamp requires !comp(hi, lo)
// and does not check it, so a small offset_max silently inverted the range: below 100/cross
// (about 0.09 on 1080p) the 100px floor vanished and the band came out as thin as the number
// asked for, with no diagnostic. offset_max has no bound in the config, so it is reachable.
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

// Full brightness inside the zone, nothing by `falloff` px away. A distance rather than a
// bool: a zone blank until the cursor crosses it says nothing while the user is aiming.
[[nodiscard]] inline double killZoneGlow(const LRect& zone, double lx, double ly, double falloff) {
    if (zone.w < 1.0 || zone.h < 1.0 || falloff <= 0.0)
        return 0.0;
    const double dx = std::max({zone.x - lx, 0.0, lx - (zone.x + zone.w)});
    const double dy = std::max({zone.y - ly, 0.0, ly - (zone.y + zone.h)});
    const double d  = std::sqrt(dx * dx + dy * dy);
    const double t  = std::clamp(1.0 - d / falloff, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The box a dragged card is drawn at this frame: glided from where it was picked up, then
// lifted. Scale and rise apply at every progress INCLUDING zero -- a stationary hold
// (fromBox == to) still has to animate, and moving them only while moving froze it.
[[nodiscard]] inline LRect dragVisualBox(const LRect& fromBox, const LRect& to, bool lifted, double progress,
                                         double liftScale, double liftRise) {
    const double p = progress;
    const LRect  b = (p < 1.0 && lifted) ? lerp(fromBox, to, p) : to;

    const double s  = 1.0 + liftScale * p;
    const double dy = -liftRise * p;
    const double cx = b.cx(), cy = b.cy();
    return LRect{cx - b.w * s / 2.0, cy - b.h * s / 2.0 + dy, b.w * s, b.h * s};
}

// Where a caption pill hangs off a card edge, clamped onto the screen. `below` picks the edge
// and the clamp mirrors: below, measured down from the card's lower edge and capped at the
// screen's bottom; above, measured up from the upper edge and floored at the top. One side's
// clamp wrong and the pill pins to the card edge instead of the screen edge.
[[nodiscard]] inline double captionPillY(double cardY, double cardH, double pillH,
                                         double gap, bool below, double screenH,
                                         double margin) {
    return below ? std::min(cardY + cardH + gap, screenH - pillH - margin)
                 : std::max(margin, cardY - gap - pillH);
}

// Both pills on ONE card edge, title and counter. The counter is the OUTER pill either way:
// above the card it leads, below it trails. A counter between the card and the title reads as
// the card's own furniture rather than a number about the run. `cardGap` and `stackGap` are
// different knobs -- one value for both glued the caption to the card whenever there was no
// counter to measure against. Clamped as one block, or the outer walks off screen while the
// inner sits on the margin.
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

// Negative = a capsule, which is what every pill here has been by default. Capped at half the
// shorter side: past that the shape turns inside out rather than rounding more.
[[nodiscard]] inline int pillRadius(double configured, double w, double h) {
    const double cap = std::min(w, h) * 0.5;
    const double want = configured < 0.0 ? cap : std::min(configured, cap);
    return static_cast<int>(std::max(0.0, want));
}

// Keep a box inside a screen, with an inset. std::clamp requires lo <= hi and does not check
// it, so a box wider than the screen minus both insets inverts the range silently -- and the
// caption is sized off a window title, which gets there on a small monitor.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
}
// The largest box of the given aspect that fits inside `outer`, centred. A preview always
// wears the window's own aspect so the content's arcs meet the ring's. The 0.01 tolerance:
// a goal a fraction off the slot's aspect gets the slot, because a preview one pixel narrower
// than its ring reads as a misaligned ring. It used to sit in an anonymous namespace in
// tile_view.cpp, where nothing could check it.
// Which of a row of boxes is under the cursor, or -1. First match wins, in order: this is the
// GRID hit-test and deliberately NOT the carousel's, whose cards overlap and where the
// front-most must win. One helper that secretly did both is how a click lands on the card
// behind the one you aimed at. -1 is a normal answer, asked on every mouse move.
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

// Where one window's thumbnail sits inside a card, from a card-relative rectangle. `rel` is
// relative so the card can be sized from the strip's settings and the thumbnails follow.
// The 2px floor is on both axes: a zero-width thumbnail is invisible, and a window on a tall
// thin strip would get a clickable sliver it does not have.
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

// How far a caption pill comes down to fit its card: 1.0 fits, less shrinks it. The run's
// cards shrink in rounding steps while the label is rasterised once at full width, so a pill
// sized off the texture alone is the same width on every card and wider than a shrunken one.
// One factor over the WHOLE pill including padding, so the label narrows with the card
// instead of being squeezed inside a pill that stayed tall.
[[nodiscard]] inline double pillScale(double cardW, double inset, double pillW) {
    if (pillW <= 0.0)
        return 1.0;
    return std::min(1.0, std::max(cardW - inset, 1.0) / pillW);
}

// How far a caption comes down with its card. The label is rasterised at one size, so a card
// two slots out was labelled at the CENTRED card's caption size and the outermost card of a
// five-card run was worst. refW is that centred card, the one scale 1.0 belongs to; a layout
// that never set one (off the run) leaves every card at full size. Floored, because a pill
// scaled to nothing is an absent caption, not a small one.
[[nodiscard]] inline double cardScale(double cardW, double refW) {
    if (refW <= 0.0 || cardW <= 0.0)
        return 1.0;
    return std::clamp(cardW / refW, 0.45, 1.0);
}


// Which axis a chrome element takes to get out from under a card that reached it, and how
// far. Vertical escape from a card is UP when the element sits above the card's centre, and
// for a caption that is the one direction separating it from its own card -- so a long title
// was shoved up and up until it read as the strip moving. Refusing the vertical outright is
// the same defect from the other side: the pill stops being pushed at all. So sideways, same
// depth, pressed against its own card's top edge; downward pushes are untouched, because
// below the card is still beside it. Arithmetic only, so a regression is a red test.
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


// How much of a caption is left: 1.0 all of it, 0.0 gone. Two reasons it leaves and this is
// the ONE place that weighs them -- it is being shoved (a preview reached it and pushOut()
// walked it up toward the strip band) or its card is leaving (the deck collapses every card
// toward its own centre on the way out). The shove is over in a QUARTER of the caption's own
// width; the exit fraction is the card's own, so no frame has a card gone and its caption
// not. exitFrac is IGNORED while opening, where the clock runs the other way. min(), not a
// blend: either of the two is enough on its own.
[[nodiscard]] inline double captionRemains(double shove, double pillW, double pillH,
                                          double exitFrac, bool opening) {
  constexpr double kGoneOver = 0.25; // of its own width
  const double byShove =
      1.0 - std::clamp(shove / std::max(std::max(pillW, pillH) * kGoneOver, 1.0), 0.0, 1.0);
  if (opening)
    return byShove;
  return std::min(byShove, std::clamp(1.0 - exitFrac, 0.0, 1.0));
}

// The same factor applied to a rect, about its own centre: a caption shrinks where it is
// rather than travelling while it shrinks. Both pills go through here, so the title and the
// number cannot carry two rules on one screen.
[[nodiscard]] inline LRect scaledAboutCentre(const LRect &r, double k) {
  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
  const double w = r.w * k, h = r.h * k;
  return LRect{cx - w / 2.0, cy - h / 2.0, w, h};
}

} // namespace gloview::render

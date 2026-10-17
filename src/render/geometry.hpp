#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>

#include "../layout.hpp"

// Pure geometry, monitor-local LOGICAL px. No GL, no Hyprland, no config reads: every
// value a caller needs is an argument.
//
// Two reasons it is an argument and not a lookup. A strip that stops being a fixed
// band on a fixed edge -- see docs/ROADMAP.md -- is a new Anchor value, not a new code
// path, and only if nothing here reads cfg:: itself. And a card that is a browser tab
// instead of a window has no CWindow* to ask for a size: the caller passes the aspect
// it already has.

namespace gloview::render {

enum class Corner {
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
};

// Which screen edge the strip band is pinned to. A new placement -- a floating strip, a
// second band -- is a new Anchor value rather than a new code path, and only while nothing
// here reads cfg::.
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

// The band's thickness, perpendicular to the anchored edge.
//
// std::clamp requires !comp(hi, lo) and does not check it, so a small offset_max silently
// inverted the range instead of aborting. Below 100/cross -- about 0.09 on a 1080p monitor
// -- the 100px floor vanished and the band came out as thin as the number asked for, with
// no diagnostic. offset_max has no bound in the config, so that value is reachable.
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

// Full brightness inside the zone, nothing by `falloff` px away, smoothstepped between.
// A distance, not a bool: a zone blank until the cursor crosses it says nothing while
// the user is still aiming it.
[[nodiscard]] inline double killZoneGlow(const LRect& zone, double lx, double ly, double falloff) {
    if (zone.w < 1.0 || zone.h < 1.0 || falloff <= 0.0)
        return 0.0;
    const double dx = std::max({zone.x - lx, 0.0, lx - (zone.x + zone.w)});
    const double dy = std::max({zone.y - ly, 0.0, ly - (zone.y + zone.h)});
    const double d  = std::sqrt(dx * dx + dy * dy);
    const double t  = std::clamp(1.0 - d / falloff, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The box a dragged card is drawn at this frame: glided from where it was picked up,
// then lifted. Scale and rise apply at every progress including zero, because a
// stationary hold -- fromBox == to, the card held down without moving -- still has to
// animate, and applying them only while moving left that hold frozen.
[[nodiscard]] inline LRect dragVisualBox(const LRect& fromBox, const LRect& to, bool lifted, double progress,
                                         double liftScale, double liftRise) {
    const double p = progress;
    const LRect  b = (p < 1.0 && lifted) ? lerp(fromBox, to, p) : to;

    const double s  = 1.0 + liftScale * p;
    const double dy = -liftRise * p;
    const double cx = b.cx(), cy = b.cy();
    return LRect{cx - b.w * s / 2.0, cy - b.h * s / 2.0 + dy, b.w * s, b.h * s};
}

// Where a caption pill hangs off a card edge, clamped onto the screen. `below` picks which
// edge. The clamp is the mirror image on each side: below it is measured down from the
// card's lower edge and capped against the screen's bottom, above it is measured up from the
// card's upper edge and floored against the screen's top. Getting one side's clamp wrong
// pins the pill to the card edge instead of the screen edge.
[[nodiscard]] inline double captionPillY(double cardY, double cardH, double pillH,
                                         double gap, bool below, double screenH,
                                         double margin) {
    return below ? std::min(cardY + cardH + gap, screenH - pillH - margin)
                 : std::max(margin, cardY - gap - pillH);
}

// Both pills on ONE card edge, title and counter. The counter is the OUTER pill either way:
// above the card it leads, below the card it trails. A counter sitting between the card and
// the title reads as part of the card's own furniture rather than as a number about the
// run, and only one of the two edges can have the counter further out than the title by
// accident -- this is the invariant that stops it.
//
// `cardGap` is the card-to-caption distance and `stackGap` the title-to-counter one. They are
// different knobs and passing one value for both is how the caption ended up glued to the
// card with no gap at all whenever there was no counter to measure against.
//
// Clamped as one block. Clamping the pills separately lets the outer one walk off the
// screen while the inner one sits on the margin.
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

// The corner radius to hand Hyprland, from a configured one. Negative = a capsule, which is
// what every pill here has been by default. Capped at half the shorter side: past that the
// shape turns inside out rather than rounding more.
[[nodiscard]] inline int pillRadius(double configured, double w, double h) {
    const double cap = std::min(w, h) * 0.5;
    const double want = configured < 0.0 ? cap : std::min(configured, cap);
    return static_cast<int>(std::max(0.0, want));
}

// Keep a box inside a screen, with an inset. std::clamp requires lo <= hi and does not
// check it, so a box wider than the screen minus both insets silently inverted the range.
// The caption is sized off a window title, and a long one on a small monitor gets there.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
}
// The largest box of the given aspect that fits inside `outer`, centred.
//
// A preview always wears the window's own aspect so the content's arcs meet the ring's. A
// second way to compute a tile box is a second way to be wrong -- and this one sat in an
// anonymous namespace in tile_view.cpp, where nothing could check it.
//
// The 0.01 tolerance: a window whose goal is a fraction off the slot's own aspect gets the
// slot rather than a sub-pixel letterbox, because a preview that is one pixel narrower than
// its ring reads as a misaligned ring.
// Which of a row of boxes is under the cursor, or -1.
//
// First match wins, in order. This is the GRID hit-test and it is deliberately NOT the
// carousel's: carousel cards overlap and there the front-most must win, which is the
// opposite rule, and one helper that secretly did both is how a click lands on the card
// behind the card you aimed at.
//
// -1 for an empty row and for a miss. The caller asks "what is under the cursor" on every
// mouse move, and "nothing" is a normal answer.
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

// Where one window's thumbnail sits inside a card, from a card-relative rectangle.
//
// `rel` is stored relative so the card can be sized from the strip's settings and the
// thumbnails follow. The 2px floor is on both axes: a thumbnail of zero width is
// invisible, so a window on a tall thin strip would get a clickable sliver it does not
// have.
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

// How far a caption pill comes down to fit its card. 1.0 fits, less shrinks it.
//
// The run's cards shrink in rounding steps with distance, while the label is rasterised once
// at full width and never changes. A pill sized off the texture alone is therefore the same
// width on every card, and on a shrunken one it is wider than the thing it labels -- measured
// on a hover over a secondary card, where it read as a title bar belonging to no card.
//
// One factor over the WHOLE pill, padding included, so the label narrows with the card rather
// than being squeezed inside a pill that stayed tall.
[[nodiscard]] inline double pillScale(double cardW, double inset, double pillW) {
    if (pillW <= 0.0)
        return 1.0;
    return std::min(1.0, std::max(cardW - inset, 1.0) / pillW);
}

// How far a caption comes down with its card. The run's cards shrink in steps with distance
// and the label is rasterised at one size, so a card two or three slots out was labelled at
// the CENTRED card's caption size: the same pill on a card half the width, and worst on the
// outermost cards of a five-card run. refW is the centred card -- the one scale 1.0 belongs
// to. A layout that never set one (off the run) leaves every card at its own full size.
//
// Floored, because a pill scaled to nothing is not a small caption, it is an absent one, and
// the last ring of a wide run is where it would happen first.
[[nodiscard]] inline double cardScale(double cardW, double refW) {
    if (refW <= 0.0 || cardW <= 0.0)
        return 1.0;
    return std::clamp(cardW / refW, 0.45, 1.0);
}

} // namespace gloview::render

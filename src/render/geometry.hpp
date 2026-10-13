#pragma once

#include <algorithm>
#include <cmath>

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

// Where the caption block starts, and where each of its two pills lands inside it.
//
// `below` hangs the block off the card's LOWER edge, so the block's top is card.bottom +
// gap and the caller clamps DOWN from there. Otherwise it sits above the card's upper edge,
// measured UP from it -- so the caller has to clamp from the other side, and getting that
// wrong is what drops the caption off the top of the screen.
//
// Inside the block the two knobs are independent, which is the whole reason this is a
// function rather than two lines: `title_position` moves both pills together, and
// `counter_position` swaps them without touching the block. Four combinations, no layout
// engine.
struct CaptionStack {
    double titleY;
    double counterY;
};

[[nodiscard]] inline CaptionStack stackCaption(double top, double titleH, double counterH,
                                              double gap, bool counterBelow) {
    if (counterH <= 0.0)  // one pill standing alone: the order knob has nothing to order
        return {top, top};
    return counterBelow ? CaptionStack{top, top + titleH + gap}
                        : CaptionStack{top + counterH + gap, top};
}

// The corner radius to hand Hyprland, from a configured one. Negative = a capsule, which is
// what every pill here has been by default. Capped at half the shorter side: past that the
// shape turns inside out rather than rounding more.
[[nodiscard]] inline int pillRadius(double configured, double w, double h) {
    const double cap = std::min(w, h) * 0.5;
    const double want = configured < 0.0 ? cap : std::min(configured, cap);
    return static_cast<int>(std::max(0.0, want));
}

// The box a caption yields as: the pill and the counter pill below it are one caption, so
// the yield has to be measured against both or they drift apart. An EMPTY counter (one card,
// or no counter on screen) collapses to the pill: taking min/max against a zero rect would
// anchor the box at the screen origin and let a preview nowhere near the caption shove it
// sideways -- which is exactly the bug this guard was written for.
[[nodiscard]] inline LRect captionBody(const LRect& pill, const LRect& counter) {
    if (counter.w <= 0.0 || counter.h <= 0.0)
        return pill;
    const double x = std::min(pill.x, counter.x);
    const double y = std::min(pill.y, counter.y);
    return LRect{x, y, std::max(pill.x + pill.w, counter.x + counter.w) - x,
                 std::max(pill.y + pill.h, counter.y + counter.h) - y};
}

// Keep a box inside a screen, with an inset. std::clamp requires lo <= hi and does not
// check it, so a box wider than the screen minus both insets silently inverted the range.
// The caption is sized off a window title, and a long one on a small monitor gets there.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
}
// The close glyph sits in one corner of a card, at a size that tracks the card but stays
// clickable on a small one.
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

} // namespace gloview::render
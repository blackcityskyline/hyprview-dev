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

// The counter's sprout: a narrower capsule hanging off the caption pill's bottom edge,
// flush against it. Touching, never overlapping -- two translucent rects over the same
// pixels blend twice and the join reads as a dark seam, which is the one thing "one
// shape" must not look like. The width is clamped so the sprout's FLAT top (w - h, both
// are capsules) lands inside the pill's own flat bottom (pill.w - pill.h): past that the
// pill's bottom arc lifts away and the two part with a notch of backdrop between them.
// A zero text size means no sprout -- one card has no "1 / 1" to show.
[[nodiscard]] inline LRect captionSprout(const LRect& pill, double textW, double textH,
                                          double padX, double padY) {
    if (textW <= 0.0 || textH <= 0.0)
        return LRect{0.0, 0.0, 0.0, 0.0};
    const double sh = textH + 2.0 * padY;
    const double sw = std::min(textW + 2.0 * padX, std::max(sh, pill.w - pill.h) + sh);
    return LRect{pill.cx() - sw * 0.5, pill.y + pill.h, sw, sh};
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
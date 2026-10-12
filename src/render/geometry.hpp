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

// The counter's sprout: the stem hanging off the caption pill's bottom edge.
//
// Touching, never overlapping: two translucent rects over the same pixels blend twice and
// the join reads as a dark seam, which is the one thing "one shape" must not look like. The
// pill is a capsule, so its bottom is FLAT across pill.w - pill.h and only the last
// pill.h / 2 at each end is an arc. A sprout whose width fits that flat span therefore
// lands on the pill with no gap at all, and it can carry whatever corner radius it likes
// -- the rounding cuts inward, and the pill is already covering that line.
//
// That is what makes it a STEM and not a second oval: the width is clamped to the flat
// span so the two share a straight edge, and the caller rounds only lightly, so the join
// reads as one body with a tab rather than a mushroom. A zero text size means no sprout --
// one card has no "1 / 1" to show.
[[nodiscard]] inline LRect captionSprout(const LRect& pill, double textW, double textH,
                                          double padX, double padY) {
    if (textW <= 0.0 || textH <= 0.0)
        return LRect{0.0, 0.0, 0.0, 0.0};
    const double sh = textH + 2.0 * padY;
    // Floored at 0, not at sh: a pill narrower than it is tall has no flat bottom at all,
    // and max(sh, pill.w - pill.h) let the sprout overshoot zero there -- the gap this
    // clamp exists to prevent, back again.
    const double flatBottom = std::max(0.0, pill.w - pill.h);
    const double sw         = std::min(textW + 2.0 * padX, flatBottom);
    return LRect{pill.cx() - sw * 0.5, pill.y + pill.h, sw, sh};
}

// The sprout's corner radius: a stem, not a capsule. Half the height is what made it read
// as a second pill; a quarter keeps the bottom soft and still lands the join on the pill's
// flat edge, since the radius only ever cuts inward from there.
[[nodiscard]] inline double captionSproutRound(double sproutH, double sproutW) {
    return std::min(std::min(sproutH * 0.25, sproutW * 0.5), sproutH * 0.5);
}

// The box a caption yields as: the pill and its stem are one object, so the yield has to
// be measured against both or they come apart. An EMPTY stem (no counter -- one card, or
// no label) collapses to the pill: taking min/max against a zero rect would anchor the box
// at the screen origin and let a preview nowhere near the caption shove it sideways.
[[nodiscard]] inline LRect captionBody(const LRect& pill, const LRect& sprout) {
    if (sprout.w <= 0.0 || sprout.h <= 0.0)
        return pill;
    const double x = std::min(pill.x, sprout.x);
    const double y = std::min(pill.y, sprout.y);
    return LRect{x, y, std::max(pill.x + pill.w, sprout.x + sprout.w) - x,
                 std::max(pill.y + pill.h, sprout.y + sprout.h) - y};
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
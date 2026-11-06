#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "../anim_types.hpp"
#include "../layout.hpp"
#include "caption.hpp"

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


// Keep a box inside a screen, with an inset. std::clamp requires lo <= hi and does not check it:

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






// The badge has one schedule, played in both directions: k == 0 is gone, k == 1 is at rest.
struct BadgePose {
  double pop = 1.0;
  double alpha = 1.0;
};

[[nodiscard]] inline BadgePose badgePose(double k) {
  constexpr double kPopPeak = 0.77; // where the overshoot sits, as a share of k
  constexpr double kFadeIn = 0.81;  // alpha reaches 1 a touch after the peak
  const double t = std::clamp(k, 0.0, 1.0);
  BadgePose out;
  out.pop = t >= kPopPeak ? 1.0 + 0.2 * (1.0 - t) / (1.0 - kPopPeak)
                          : 0.65 + 0.55 * t / kPopPeak;
  out.alpha = std::min(1.0, t / kFadeIn);
  return out;
}

// The badge's own window of the run: in at the front of the open, out at the end of the close.
[[nodiscard]] inline double badgeK(double openT, double exitT, bool opening, double span) {
  if (span <= 0.0)
    return 1.0;
  return opening ? std::clamp(openT / span, 0.0, 1.0)
                 : std::clamp(1.0 - exitT / span, 0.0, 1.0);
}

// What a card shows in its frame: a tab with no picture shows the plate and the dim title.
enum class CardKind { Window, Tab };
enum class CardContent { Window, Picture, PlateAndTitle };

[[nodiscard]] inline CardContent cardContent(CardKind kind, bool hasPicture) {
  if (kind == CardKind::Window)
    return CardContent::Window;
  return hasPicture ? CardContent::Picture : CardContent::PlateAndTitle;
}

// The badge's texture size: steps of eight, so a growing card reuses the icon it has.
[[nodiscard]] inline int badgePx(double side, double scale) {
  const int px = static_cast<int>(std::lround(side * scale));
  return std::max(16, (px + 7) / 8 * 8);
}

[[nodiscard]] inline LRect scaledAboutCentre(const LRect &r, double k) {
  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
  const double w = r.w * k, h = r.h * k;
  return LRect{cx - w / 2.0, cy - h / 2.0, w, h};
}

[[nodiscard]] inline double badgeSide(double cardH) {
  return std::clamp(cardH * 0.12825, 25.65, 57.0);
}

// The badge's own square: the corner the caption does not reach.
[[nodiscard]] inline LRect badgeBox(const LRect &card, double side, double pop) {
  return scaledAboutCentre(
      LRect{card.x + side * 0.6, card.y + card.h - side * 1.6, side, side}, pop);
}




// The strip's own alpha: the slide is shorter than the band, so a ramp on the slide left the
// curtain invisible while it was still half on screen.
[[nodiscard]] inline double stripFade(double bandE) {
  return std::clamp(bandE * 2.0, 0.0, 1.0);
}

// The band's alpha has ONE source: the knob is the band, and the band's own clock scales it. The colour string's alpha is not a second source.
[[nodiscard]] inline double stripBandAlpha(double opacityPct, double bandE) {
  return std::clamp(opacityPct / 100.0, 0.0, 1.0) * bandE;
}

// The label's box: in the band ABOVE the card, shrunk to what the card allows.
[[nodiscard]] inline LRect stripLabelBox(const LRect &card, double labelW, double labelH,
                                          double labelBand, double labelExtra) {
  const double maxW = card.w + labelExtra;
  double w = labelW, h = labelH;
  if (w > maxW && w > 0.0) {
    const double k = maxW / w;
    w *= k;
    h *= k;
  }
  return LRect{card.cx() - w / 2.0, card.y - labelBand + (labelBand - h) / 2.0, w, h};
}

// The "+" glyph: a cross in the middle of the card.
struct PlusGlyph {
  LRect horizontal;
  LRect vertical;
  bool drawable = false;
};

[[nodiscard]] inline PlusGlyph plusGlyph(const LRect &card) {
  const double t = std::max(2.0, card.h * 0.04);
  const double L = std::min(card.w, card.h) * 0.34;
  const double cx = card.cx(), cy = card.cy();
  PlusGlyph g;
  g.drawable = L > 0.5;
  g.horizontal = LRect{cx - L / 2.0, cy - t / 2.0, L, t};
  g.vertical = LRect{cx - t / 2.0, cy - L / 2.0, t, L};
  return g;
}

// The "All" glyph: a 2x2 grid of squares in the middle of the card.
struct AllGlyph {
  LRect cells[4];
};

[[nodiscard]] inline AllGlyph allGlyph(const LRect &card) {
  const double pad = std::min(card.w, card.h) * 0.26;
  const double gw = card.w - 2 * pad, gh = card.h - 2 * pad;
  const double cg = std::max(2.0, std::min(card.w, card.h) * 0.07);
  const double cw = std::max(2.0, (gw - cg) / 2.0);
  const double ch = std::max(2.0, (gh - cg) / 2.0);
  const double gx = card.x + pad, gy = card.y + pad;
  AllGlyph g;
  int k = 0;
  for (int r = 0; r < 2; ++r)
    for (int col = 0; col < 2; ++col)
      g.cells[k++] = LRect{gx + col * (cw + cg), gy + r * (ch + cg), cw, ch};
  return g;
}

// The wallpaper crop: cover-fit, so the card shows the image whole.
struct CoverUV {
  double u0 = 0.0, v0 = 0.0, u1 = 1.0, v1 = 1.0;
};

[[nodiscard]] inline CoverUV coverFitUV(double cardW, double cardH, double texW,
                                        double texH) {
  CoverUV uv;
  const double cardRatio = cardW / std::max(1.0, cardH);
  const double texRatio = texW / std::max(1.0, texH);
  if (cardRatio > texRatio) {
    const double f = texRatio / cardRatio;
    uv.v0 = (1.0 - f) / 2.0;
    uv.v1 = (1.0 + f) / 2.0;
  } else {
    const double f = cardRatio / texRatio;
    uv.u0 = (1.0 - f) / 2.0;
    uv.u1 = (1.0 + f) / 2.0;
  }
  return uv;
}

// How a window's own surface maps into the tile it is drawn in: one scale, one
// translation. Over-cover from the CENTER -- fitting exactly shows the backing as seams.
struct WindowFitInput {
  double destX = 0.0, destY = 0.0, destW = 0.0, destH = 0.0;
  double logicalX = 0.0, logicalY = 0.0, logicalW = 0.0, logicalH = 0.0;
  double monX = 0.0, monY = 0.0, monScale = 1.0;
  bool settled = false;
  bool contain = false;
  double seamPad = 0.0;
};

struct WindowFit {
  bool valid = false;
  double scale = 1.0;
  LVec2 translate;
};

[[nodiscard]] inline WindowFit windowFit(const WindowFitInput &in) {
  WindowFit fit;
  const double lw = std::max(in.logicalW, 5.0);
  const double lh = std::max(in.logicalH, 5.0);
  const double pad = (in.settled || in.contain) ? 0.0 : in.seamPad;
  const double sW = (in.destW + pad) / std::max(lw * in.monScale, 5.0);
  const double sH = (in.destH + pad) / std::max(lh * in.monScale, 5.0);
  const double scale = in.contain ? std::min(sW, sH) : std::max(sW, sH);
  if (!(scale > 0.0))
    return fit;

  const double tlX = (in.logicalX - in.monX) * in.monScale;
  const double tlY = (in.logicalY - in.monY) * in.monScale;
  const double winCx = tlX + lw * in.monScale / 2.0;
  const double winCy = tlY + lh * in.monScale / 2.0;
  const double destCx = in.destX + in.destW / 2.0;
  const double destCy = in.destY + in.destH / 2.0;
  fit.valid = true;
  fit.scale = scale;
  fit.translate = LVec2{destCx / scale - winCx, destCy / scale - winCy};
  return fit;
}

// The drawn box, global logical: damage is clipped to getTexBox() (SurfacePassElement.cpp:31).
struct WindowBox {
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

[[nodiscard]] inline WindowBox previewBox(const WindowFitInput &in, const WindowFit &fit) {
  if (!fit.valid)
    return {};
  const double s = in.monScale > 0.0 ? in.monScale : 1.0;
  const double tlX = (in.logicalX - in.monX) * s;
  const double tlY = (in.logicalY - in.monY) * s;
  return WindowBox{in.monX + (tlX + fit.translate.x) * fit.scale / s,
                   in.monY + (tlY + fit.translate.y) * fit.scale / s,
                   std::max(in.logicalW * fit.scale, 5.0),
                   std::max(in.logicalH * fit.scale, 5.0)};
}

// The run's dim: the focused card is clear, the next one takes the near tone, and
// every step past it is the far tone tapered.
[[nodiscard]] inline double carouselTone(int depth, double nearTone, double farTone,
                                         double taper) {
  if (depth <= 0)
    return 0.0;
  const double base = depth == 1 ? nearTone : farTone;
  return base * std::pow(taper, static_cast<double>(depth - 1));
}

// The run paints far -> near by DEPTH, and the cards the run does not show paint first, behind everything. The focused card and its ring end up on top.
struct CarouselCardRef {
  int tile = 0;
  int depth = 3;
  bool visible = false;
};

[[nodiscard]] inline std::vector<int> carouselPaintOrder(
    const std::span<const CarouselCardRef> &cards) {
  std::vector<CarouselCardRef> sorted(cards.begin(), cards.end());
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const CarouselCardRef &a, const CarouselCardRef &b) {
                     if (a.visible != b.visible)
                       return !a.visible;
                     return a.depth > b.depth;
                   });
  std::vector<int> paint;
  paint.reserve(sorted.size());
  for (const auto &c : sorted)
    paint.push_back(c.tile);
  return paint;
}

// A click on the run must land on the FRONT-most card: the run overlaps, so the first match is the wrong one; the grid's opposite rule lives in firstHit().
struct CarouselHitCard {
  int tile = 0;
  int depth = 3;
  bool visible = false;
  LRect box;
};

[[nodiscard]] inline int carouselHit(const std::span<const CarouselHitCard> &cards,
                                     double lx, double ly) {
  int best = -1, bestDepth = 0;
  for (const auto &c : cards) {
    if (!c.visible)
      continue;
    if (!c.box.contains(lx, ly))
      continue;
    if (best < 0 || c.depth < bestDepth) {
      bestDepth = c.depth;
      best = c.tile;
    }
  }
  return best;
}

// The wallpaper covers the monitor: scaled to fill, centred, the overflow cut off.
struct CoverFit {
  double scale = 1.0;
  double x = 0.0;
  double y = 0.0;
};

[[nodiscard]] inline CoverFit coverFit(double destW, double destH, double texW, double texH) {
  CoverFit f;
  const double destRatio = destW / std::max(1.0, destH);
  const double texRatio = texW / std::max(1.0, texH);
  if (destRatio > texRatio) {
    f.scale = destW / std::max(1.0, texW);
    f.y = (destH - texH * f.scale) / 2.0;
  } else {
    f.scale = destH / std::max(1.0, texH);
    f.x = (destW - texW * f.scale) / 2.0;
  }
  return f;
}

// The black check: a frame that is black everywhere is not a backdrop. The threshold is low on purpose -- a dark frame of a video wallpaper IS the wallpaper.
[[nodiscard]] inline bool looksBlack(unsigned char r, unsigned char g, unsigned char b) {
  constexpr unsigned char kBlack = 8;
  return r < kBlack && g < kBlack && b < kBlack;
}

// A wallpaper that changes several times IN A ROW is animation; one re-creation of a static wallpaper is not.
inline constexpr int kSourceAnimatedStreak = 4;

// The pulse ring: the tile's box, grown by the pulse. k is the pulse's own
// progress, already scaled.
[[nodiscard]] inline LRect pulseRing(const LRect &box, double k) {
  const double cx = box.cx(), cy = box.cy();
  return LRect{cx - box.w * (0.5 + k), cy - box.h * (0.5 + k), box.w * (1.0 + 2.0 * k),
                box.h * (1.0 + 2.0 * k)};
}

// The ring's own thickness grows with the pulse.
[[nodiscard]] inline int pulseRingWidth(int round, double k) {
  return static_cast<int>(std::max(2.0, round + round * 0.5 * k));
}

// The half of a slot the pointer is in: the drop hint shows the half it will fill.
[[nodiscard]] inline LRect dwindleHalf(const LRect &slot, double px, double py) {
  LRect half = slot;
  if (slot.w >= slot.h) {
    half.w = slot.w / 2.0;
    if (px >= slot.cx())
      half.x = slot.cx();
  } else {
    half.h = slot.h / 2.0;
    if (py >= slot.cy())
      half.y = slot.cy();
  }
  return half;
}

// The ✕ glyph inside its button: it fills the button by `fill` of the tighter side,
// and it is centred. ONE rule for every surface that draws it.
[[nodiscard]] inline LRect glyphRect(const LRect &button, double glyphW, double glyphH,
                                     double fill) {
  const double scale =
      std::min((button.w * fill) / std::max(1.0, glyphW),
                (button.h * fill) / std::max(1.0, glyphH));
  const double w = glyphW * scale, h = glyphH * scale;
  return LRect{button.x + (button.w - w) / 2.0, button.y + (button.h - h) / 2.0, w, h};
}

struct MenuLayout {
    LRect panel;
    LRect browser;
    LRect carousel;
};

[[nodiscard]] inline MenuLayout menuLayout(double x, double y, double monW, double monH) {
    constexpr double kPad = 10.0;
    constexpr double kButton = 44.0;
    constexpr double kGap = 8.0;
    const double w = kPad * 2.0 + kButton * 2.0 + kGap;
    const double h = kPad * 2.0 + kButton;
    MenuLayout out;
    out.panel = LRect{std::clamp(x, 0.0, std::max(0.0, monW - w)),
                      std::clamp(y, 0.0, std::max(0.0, monH - h)), w, h};
    out.browser = LRect{out.panel.x + kPad, out.panel.y + kPad, kButton, kButton};
    out.carousel = LRect{out.browser.x + kButton + kGap, out.browser.y, kButton, kButton};
    return out;
}

// The ring on top of a tile: hover over selection over base.
[[nodiscard]] inline int focusRingWidth(bool showFocusBorder, bool framed, bool selected,
                                        int hoverWidth, int selectWidth, int baseWidth) {
  if (!showFocusBorder)
    return 0;
  if (framed)
    return hoverWidth;
  if (selected)
    return selectWidth;
  return baseWidth;
}

// The part of the backdrop that shows through a frost: the tile's box, normalized and zoomed.
struct FrostUV {
  double u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
};

inline constexpr double kBackdropPush = 0.10;

// Advancing, not receding: UV stays inside [0,1], so the frame crops its edge instead of stretching it (a sharp frame showed the seams).
[[nodiscard]] inline double backdropZoom(double push) {
  return 1.0 + kBackdropPush * std::clamp(push, 0.0, 1.0);
}

[[nodiscard]] inline FrostUV frostUV(double x, double y, double w, double h, double pxW,
                                     double pxH, double zoom) {
  const double k    = 1.0 / std::max(zoom, 1e-6);
  const auto   axis = [k](double p, double len) { return 0.5 + (p / len - 0.5) * k; };
  return {axis(x, pxW), axis(y, pxH), axis(x + w, pxW), axis(y + h, pxH)};
}

// The GL layer's own arithmetic, pure: the radii and the pixel conversion. They
// lived in gl_util.hpp, which pulls GL in, so no test could reach them.
[[nodiscard]] inline int pxr(double round, double s) { return static_cast<int>(round * s); }

[[nodiscard]] inline int outerRoundPx(double round, double borderSize,
                                      double roundingPower, double scale) {
  const double correction =
      borderSize * (M_SQRT2 - 1.0) * std::max(2.0 - roundingPower, 0.0);
  return static_cast<int>((round + borderSize - correction) * scale);
}

// A radius past half the shorter side inverts the shape, so it clamps.
[[nodiscard]] inline int clampRound(int round, double w, double h) {
  return static_cast<int>(
      std::clamp(static_cast<double>(round), 0.0, std::min(w, h) * 0.5));
}

// PIXELS, not logical: renderWindowLive over-covers by this much so sub-pixel
// snapping cannot open a seam.
inline constexpr float kSeamPadPx = 1.5F;

// The radius a tile's CONTENT carries so its arc lands on the ring's outer edge; +1, Hyprland subtracts one. The radius is the CONTENT's own: a radius past half the
// content's side turns the shape inside out.
[[nodiscard]] inline int contentRadiusPx(int round, int width, double scale,
                                         bool overCovers = true) {
  return pxr(round, scale) + static_cast<int>(std::lround(width * scale)) +
         (overCovers ? static_cast<int>(kSeamPadPx) : 0) + 1;
}

// The content's radius, clamped to the content's own box.
[[nodiscard]] inline int contentRadiusFor(int round, const LRect &content, int border,
                                         double scale, bool overCovers = true) {
  return contentRadiusPx(clampRound(round, content.w, content.h), border, scale, overCovers);
}

// What the backdrop puts UNDER the tiles (AGENTS.md 2.28): with the blur on, the sharp source is not an answer.
enum class BackdropBase { Blur, OpaqueDim, Source };

[[nodiscard]] inline BackdropBase backdropBase(bool blurEnabled, bool blurUsable) {
  if (!blurEnabled)
    return BackdropBase::Source;
  return blurUsable ? BackdropBase::Blur : BackdropBase::OpaqueDim;
}

// The content's box inside the card: the window keeps its own aspect.
[[nodiscard]] inline LRect contentInside(const LRect &card, double windowW, double windowH) {
  if (windowW <= 0.0 || windowH <= 0.0)
    return card;
  return fitInside(card, windowW / windowH);
}

// The kill zone sits at the screen edge OPPOSITE the strip: the edge the strip is not on, so a dragged window never has to cross the strip to reach it.
[[nodiscard]] inline LRect killZoneRect(Anchor strip, double screenW, double screenH,
                                        double thickness) {
  switch (strip) {
    case Anchor::Bottom: return LRect{0.0, 0.0, screenW, thickness};
    case Anchor::Top:    return LRect{0.0, screenH - thickness, screenW, thickness};
    case Anchor::Left:   return LRect{screenW - thickness, 0.0, thickness, screenH};
    case Anchor::Right:  return LRect{0.0, 0.0, thickness, screenH};
  }
  return LRect{0.0, 0.0, screenW, thickness};
}

// The zone's glow: one line per step from the screen edge inward, fading. A
// horizontal band glows along rows, a vertical band along columns.
struct KillRampStep {
  LRect line;
  double alpha = 0.0; // before the cursor's own glow multiplies it
};

[[nodiscard]] inline std::vector<KillRampStep> killZoneRamp(const LRect &zone, double screenW,
                                                             double screenH, int reach) {
  std::vector<KillRampStep> ramp;
  if (reach <= 0)
    return ramp;
  ramp.reserve(static_cast<std::size_t>(reach));
  const bool horizontal = zone.w >= zone.h;
  const bool fromLow    = horizontal ? (zone.y + zone.h <= screenH / 2.0)
                                      : (zone.x + zone.w <= screenW / 2.0);
  for (int d = 0; d < reach; ++d) {
    const double f = static_cast<double>(d) / reach;
    KillRampStep step;
    step.alpha = std::pow(1.0 - f, 2.5) * 0.4675;
    if (horizontal)
      step.line = fromLow ? LRect{0.0, zone.y + d, zone.w, 1.0}
                          : LRect{0.0, zone.y + zone.h - 1 - d, zone.w, 1.0};
    else
      step.line = fromLow ? LRect{zone.x + d, 0.0, 1.0, zone.h}
                          : LRect{zone.x + zone.w - 1 - d, 0.0, 1.0, zone.h};
    ramp.push_back(step);
  }
  return ramp;
}

// The flight of a swap: four kinds, one answer each. A swap on the vertical axis is NOT a slide on Y -- it arcs up and comes down on the other card.
struct SwapFlight {
  LRect box;
  double alpha = 1.0;
};

struct SwapFlightInput {
  anim::Travel travel = anim::Travel::Slide;
  anim::Axis axis = anim::Axis::Horizontal;
  LRect from;
  LRect to;
  double t = 0.0;     // raw time, 0..1
  double p = 0.0;     // eased progress of the whole flight
  double q1 = 0.0;    // curve at t / 0.5
  double q2 = 0.0;    // curve at (t - 0.5) / 0.5
  double popK = 1.0;  // 0.7 + 0.3 * cardCurve((t - 0.3) / 0.7)
  double lift = 0.0;
};

[[nodiscard]] inline SwapFlight swapFlight(const SwapFlightInput &in) {
  SwapFlight f;
  if (in.travel == anim::Travel::Slide && in.axis == anim::Axis::Vertical) {
    if (in.t < 0.5) {
      f.box = in.from;
      f.box.y = in.from.y - (in.from.h + in.lift) * in.q1;
    } else {
      f.box = in.to;
      f.box.y = in.to.y - (in.to.h + in.lift) * (1.0 - in.q2);
      f.alpha = std::min(1.0, in.q2 * 2.0);
    }
    return f;
  }
  if (in.travel == anim::Travel::Fade) {
    if (in.t < 0.5) {
      f.box = in.from;
      f.alpha = 1.0 - in.t / 0.5;
    } else {
      f.box = in.to;
      f.alpha = (in.t - 0.5) / 0.5;
    }
    return f;
  }
  if (in.travel == anim::Travel::Pop) {
    if (in.t < 0.3) {
      f.box = in.from;
      f.alpha = 1.0 - in.t / 0.3;
    } else {
      const double k = in.popK;
      f.box = in.to;
      f.box.x = in.to.x + in.to.w * (1.0 - k) / 2.0;
      f.box.y = in.to.y + in.to.h * (1.0 - k) / 2.0;
      f.box.w = in.to.w * k;
      f.box.h = in.to.h * k;
      f.alpha = std::min(1.0, (in.t - 0.3) / 0.7 * 3.0);
    }
    return f;
  }
  f.box = lerp(in.from, in.to, in.p);
  return f;
}

// How far the strip assembly leaves the band's edge: the deepest card penetration, along the anchor axis only. The caller applies it to band AND cards together.
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

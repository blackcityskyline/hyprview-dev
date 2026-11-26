#pragma once

// THE CAPTION, two owners: the NAME is a pill on the card's bottom-left, the COUNTER outside.

#include "../layout.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace hyprview::render {

[[nodiscard]] inline double captionPillY(double cardY, double cardH, double pillH,
                                         double gap, bool below, double screenH,
                                         double margin) {
    return below ? std::min(cardY + cardH + gap, screenH - pillH - margin)
                 : std::max(margin, cardY - gap - pillH);
}

// a caption sized off a window title can be wider than the screen minus both insets.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
}

// Negative configured = a capsule; capped at half the shorter side, or the shape turns inside out.
[[nodiscard]] inline int pillRadius(double configured, double w, double h) {
    const double cap = std::min(w, h) * 0.5;
    const double want = configured < 0.0 ? cap : std::min(configured, cap);
    return static_cast<int>(std::max(0.0, want));
}

// How far a caption comes down with its card. refW is the CENTRED card, the one scale 1.0 belongs to.
[[nodiscard]] inline double cardScale(double cardW, double refW) {
  if (refW <= 0.0 || cardW <= 0.0)
    return 1.0;
  return std::clamp(cardW / refW, 0.45, 1.0);
}

struct CaptionSizes {
  double textW = 0.0, textH = 0.0;
  double countW = 0.0, countH = 0.0;
  bool hasLabel = false;
  bool hasCounter = false;
  double iconW = 0.0;
  bool hasIcon = false;
};

struct CaptionStyle {
  double padX = 0.0, padY = 0.0;
  double cPadX = 0.0, cPadY = 0.0; // the counter pill's own padding
  double edgeGap = 0.0;            // card edge to the counter pill
  double cMargin = 0.0;            // screen margin kept for the counter pill
  double cardRefW = 0.0;           // the centred card, the one scale 1.0 belongs to
  bool counterBelow = true;        // the counter hangs off the card's bottom edge
  double iconGap = 6.0;            // between the icon and the text
  double pillInset = 8.0;          // the pill's own margin from the card's edges
};

struct CaptionLayout {
  LRect plate;           // the pill: icon and text plus padding, inset in the card's bottom-left
  LRect counter;         // the counter's own pill, outside the card; h == 0 when there is none
  double contentX = 0.0; // left edge of the icon+text group inside the pill
  double kFit = 1.0;     // the text's scale, shrunk so the pill still fits the card
  double inset = 0.0;
};

[[nodiscard]] inline CaptionLayout captionLayout(const LRect &card, const LRect &band,
                                                 double screenW, double screenH,
                                                 const CaptionSizes &s,
                                                 const CaptionStyle &st) {
  // Nothing to label: nothing to draw, not an empty plate with padding around it.
  if (!s.hasLabel && !s.hasCounter)
    return CaptionLayout{};

  // No label: the plate IS the counter, inside the card, in the place the title would take.
  const bool label = s.hasLabel;
  const double textW = label ? s.textW : s.countW;
  const double textH = label ? s.textH : s.countH;
  const double padX = label ? st.padX : st.cPadX;
  const double padY = label ? st.padY : st.cPadY;
  const bool icon = label && s.hasIcon && s.iconW > 0.0;
  const double iconW = icon ? s.iconW + st.iconGap : 0.0;
  const double innerH = icon ? std::max(textH, s.iconW) : textH;

  // The plate is the card's own bottom edge, the card's size; only the TEXT shrinks to fit it.
  const double kCard = cardScale(card.w, st.cardRefW);
  const double ph = (innerH + 2.0 * padY) * kCard;
  const double contentW = textW + iconW;
  const double room = std::max(card.w - 2.0 * (st.pillInset + padX * kCard), 1.0);
  const double kFit = contentW > 0.0 ? std::min(kCard, room / contentW) : kCard;

  CaptionLayout lay;
  lay.kFit = kFit;
  lay.plate = LRect{card.x + st.pillInset, card.y + card.h - ph - st.pillInset,
                    contentW * kFit + 2.0 * padX * kCard, ph};
  lay.contentX = lay.plate.x + padX * kCard;
  lay.inset = st.pillInset;
  if (!label || !s.hasCounter)
    return lay;

  const double cW = s.countW + 2.0 * st.cPadX;
  const double cH = s.countH + 2.0 * st.cPadY;
  // The strip band thickens the margin of ITS edge; the counter keeps off it there.
  const bool bandOnTop = band.y < screenH * 0.5;
  const bool bandHere = bandOnTop != st.counterBelow;
  const double margin = st.cMargin + (bandHere ? band.h : 0.0);
  lay.counter = LRect{insetWithin(screenW * 0.5 - cW * 0.5, cW, screenW, 6.0),
                      captionPillY(card.y, card.h, cH, st.edgeGap, st.counterBelow, screenH,
                                   margin),
                      cW, cH};
  return lay;
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

[[nodiscard]] inline double captionRemains(double covered, double pillW, double pillH,
                                          double exitFrac, bool opening) {
  constexpr double kGoneOver = 0.25; // of its own width
  const double byCover =
      1.0 - std::clamp(covered / std::max(std::max(pillW, pillH) * kGoneOver, 1.0), 0.0, 1.0);
  if (opening)
    return byCover;
  return std::min(byCover, std::clamp(1.0 - exitFrac, 0.0, 1.0));
}

struct CaptionInput {
  LRect home;                        // where the counter sits when nothing covers it
  std::span<const LRect> covers;     // the cards that can cover it
  bool slides = true;                // the counter steps aside from a card that grazes it
  double exitFrac = 0.0;             // 0..1 on the way out
  bool opening = true;
};

struct Caption {
  LRect box;           // where it is drawn
  double scale = 1.0;  // 1.0 whole, 0.0 gone
  bool covered = false; // a card has it outright
  LVec2 shove{};       // how far it stepped aside
};

[[nodiscard]] inline Caption placeCaption(const CaptionInput &in) {
  Caption out;
  out.box = in.home;
  if (in.home.w <= 0.0)
    return out;

  double depth = 0.0;
  bool swallowed = false;
  for (const auto &b : in.covers) {
    const double ox = std::min(b.x + b.w, in.home.x + in.home.w) - std::max(b.x, in.home.x);
    const double oy = std::min(b.y + b.h, in.home.y + in.home.h) - std::max(b.y, in.home.y);
    if (ox <= 0.0 || oy <= 0.0)
      continue;
    if (ox >= in.home.w && oy >= in.home.h) {
      swallowed = true;
      depth = std::max(depth, std::min({in.home.x - b.x, b.x + b.w - (in.home.x + in.home.w),
                                        in.home.y - b.y, b.y + b.h - (in.home.y + in.home.h)}));
      continue;
    }
    depth = std::max(depth, std::min(ox, oy));
  }

  if (in.slides) {
    out.shove = pushOut(in.home, in.covers);
    out.box.x += out.shove.x;
    out.box.y += out.shove.y;
  }

  out.covered = swallowed;
  out.scale = captionRemains(swallowed ? depth : 0.0, in.home.w, in.home.h, in.exitFrac,
                              in.opening);
  return out;
}

} // namespace hyprview::render

#pragma once

// THE CAPTION. One module, one rule (AGENTS.md 2.35): a card that HAS the caption takes it
// away; a card that grazes it only lets it run under, whole.

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

// a caption sized off a window title can be wider than the screen minus both insets.
[[nodiscard]] inline double insetWithin(double x, double size, double screenW,
                                        double inset) {
    const double hi = screenW - size - inset;
    return hi < inset ? screenW * 0.5 : std::clamp(x, inset, hi);
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
  double iconW = 0.0; // the site's favicon, inside the pill before the text
  bool hasIcon = false;
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
  double iconGap = 6.0;     // between the favicon and the first glyph
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
  // The caption's scale is the CARD's scale: a long title is not a smaller title.
  const double kFit = kCard;
  const double iconW = s.hasIcon ? s.iconW + st.iconGap : 0.0;
  const double innerH = s.hasIcon ? std::max(textH, s.iconW) : textH;
  const double pw = (textW + iconW + 2 * padX) * kFit;
  const double ph = (innerH + 2 * padY) * kFit;

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

// The caption's fate: how much of it is left.
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
  LRect home;                        // where the caption sits when nothing covers it
  std::span<const LRect> covers;     // the cards that can cover it
  bool slides = true;                // the caption steps aside from a card that grazes it
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

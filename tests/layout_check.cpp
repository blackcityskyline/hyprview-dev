// computeLayout: the three tile engines, as arithmetic.
//
// computeCarousel has its own check. computeLayout -- which produces every box the
// grid, rows and natural views hand to the renderer -- had none, and it is the one
// that can emit a NEGATIVE box. layoutRows guards its scale with std::max(0.01, ...);
// layoutGrid has no such floor:
//
//   cellW = (area.w - (cols - 1) * gap) / cols
//
// inset() only guarantees area.w >= 1, so with a narrow area and enough windows to
// need a second column, cellW goes below zero, scale follows it, and every box in
// that row is negative. Downstream that reads as "no tile" rather than "wrong tile",
// because drawTile drops anything under a pixel -- which is the guard that makes the
// crash survivable and the disappearance invisible.
//
// So the invariants below are the ones a renderer downstream depends on and no
// caller checks: the count is preserved, nothing is negative, and nothing escapes the
// area it was given. A layout is free to be ugly; it is not free to be un-drawable.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "layout.hpp"

using namespace hyprview;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("  FAIL  %s\n", what.c_str());
    ++failures;
  }
}

std::string num(double v) { return std::to_string(static_cast<int>(v * 100) / 100.0); }

// Everything downstream of here treats a box as drawable, and a negative one is what
// the compositor's render entry points assert on.
void drawable(const std::vector<LRect> &out, const std::string &what) {
  for (size_t i = 0; i < out.size(); ++i) {
    check(out[i].w > 0.0 && out[i].h > 0.0,
          what + ": box " + std::to_string(i) + " is " + num(out[i].w) + "x" +
              num(out[i].h));
  }
}

void contained(const std::vector<LRect> &out, const LRect &area,
               const std::string &what) {
  // A pixel of slack: the arithmetic scales and rounds, and a box that lands exactly
  // on the edge is correct, not a bug worth a failing test.
  const double eps = 1.0;
  for (size_t i = 0; i < out.size(); ++i) {
    const LRect &r = out[i];
    check(r.x >= area.x - eps && r.y >= area.y - eps &&
              r.x + r.w <= area.x + area.w + eps &&
              r.y + r.h <= area.y + area.h + eps,
          what + ": box " + std::to_string(i) + " " + num(r.x) + "," + num(r.y) +
              " " + num(r.w) + "x" + num(r.h) + " escapes the area");
  }
}

std::vector<LRect> naturals(const std::vector<std::pair<double, double>> &sizes) {
  std::vector<LRect> v;
  for (const auto &[w, h] : sizes)
    v.push_back(LRect{0, 0, w, h});
  return v;
}

const char *engineName(Engine e) {
  switch (e) {
  case Engine::Grid: return "grid";
  case Engine::Natural: return "natural";
  default: return "rows";
  }
}

// The area computeLayout works in. layout.cpp has this as a static inset(); a test
// that reimplemented it differently would be testing its own arithmetic instead.
LRect insetArea(const LRect &a, const LayoutCfg &c) {
  return LRect{a.x + c.padLeft, a.y + c.padTop,
               std::max(1.0, a.w - c.padLeft - c.padRight),
               std::max(1.0, a.h - c.padTop - c.padBottom)};
}

// `fits` is the stronger claim, and it is only claimed where it can be true: two
// drawable windows cannot both sit inside a one-pixel area, and layoutRows floors its
// scale at 0.01 precisely so a starved area yields small boxes rather than none. On a
// real screen the layout must stay inside the area; on a degenerate one it must merely
// stay positive, because that is what the renderer's asserts turn on.
void engines(const std::string &what, const std::vector<LRect> &ns,
             const LRect &area, const LayoutCfg &base, bool fits = true) {
  for (Engine e : {Engine::Rows, Engine::Grid, Engine::Natural}) {
    LayoutCfg cfg = base;
    cfg.engine = e;
    const std::string tag = what + "/" + engineName(e);
    const auto out = computeLayout(ns, area, cfg);
    check(out.size() == ns.size(),
          tag + ": got " + std::to_string(out.size()) + " boxes for " +
              std::to_string(ns.size()) + " windows");
    drawable(out, tag);
    if (fits && !ns.empty())
      contained(out, insetArea(area, cfg), tag);
  }
}

} // namespace

int main() {
  const LRect screen{0, 0, 1920, 1080};

  // Ordinary: the layout the user actually sees.
  engines("9 equal", naturals({{673, 756}, {673, 756}, {673, 756},
                               {673, 756}, {673, 756}, {673, 756},
                               {673, 756}, {673, 756}, {673, 756}}),
          screen, {});
  engines("mixed", naturals({{1920, 1080}, {673, 374}, {700, 400},
                             {320, 240}, {1920, 1080}}),
          screen, {});
  engines("one", naturals({{1920, 1080}}), screen, {});
  engines("thin strip", naturals({{1920, 40}, {1920, 40}, {1920, 40}}),
          screen, {});

  // A 1px window: layoutRows floors its scale, so the others must not produce a box
  // that cannot be drawn.
  engines("hairline", naturals({{1, 1}, {1, 1}, {1, 1}, {1, 1}}), screen, {});

  // The gap outgrows the area. inset() floors the area at 1px but nothing floors the
  // gap, so a second column is wider than the space it has to live in.
  {
    LayoutCfg cfg;
    cfg.padLeft = cfg.padRight = cfg.padTop = cfg.padBottom = 10.0;
    cfg.gap = 900.0;
    engines("gap eats area", naturals({{1920, 1080}, {1920, 1080}, {1920, 1080}}),
            LRect{0, 0, 100, 100}, cfg, /*fits=*/false);
  }

  // Degenerate area: a monitor nobody has, or padding that ate it whole.
  engines("1x1 area", naturals({{1920, 1080}, {1920, 1080}}), LRect{0, 0, 1, 1}, {},
          /*fits=*/false);
  engines("zero area", naturals({{1920, 1080}, {1920, 1080}}), LRect{0, 0, 0, 0}, {},
          /*fits=*/false);
  {
    LayoutCfg cfg;
    cfg.padLeft = cfg.padRight = cfg.padTop = cfg.padBottom = 5000.0;
    engines("padding eats screen", naturals({{1920, 1080}, {1920, 1080}}), screen,
            cfg, /*fits=*/false);
  }

  // No windows at all: every engine has to return an empty vector, not crash on the
  // sqrt of nothing.
  for (Engine e : {Engine::Rows, Engine::Grid, Engine::Natural}) {
    LayoutCfg cfg;
    cfg.engine = e;
    check(computeLayout({}, screen, cfg).empty(),
          std::string("empty input on ") + engineName(e) + " returns boxes");
  }

  // Natural keeps the arrangement, so two neighbours stay neighbours and the whole
  // set keeps its aspect ratio -- a grid would satisfy neither.
  {
    LayoutCfg cfg;
    cfg.engine = Engine::Natural;
    const auto ns = std::vector<LRect>{{0, 0, 400, 300}, {500, 0, 400, 300},
                                       {0, 400, 400, 300}, {500, 400, 400, 300}};
    const auto out = computeLayout(ns, screen, cfg);
    const double ar = 400.0 / 300.0;
    for (size_t i = 0; i < out.size(); ++i)
      check(std::abs(out[i].w / out[i].h - ar) < 1e-6,
            "natural: box " + std::to_string(i) + " deformed to " +
                num(out[i].w / out[i].h));
  }

  if (failures) {
    std::printf("layout: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("layout: ok\n");
  return 0;
}

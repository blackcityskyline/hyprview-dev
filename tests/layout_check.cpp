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

  // The desktop view: the windows of two workspaces sit at the SAME place, so without a cell
  // per workspace the cards land on top of each other.
  {
    LayoutCfg cfg;
    cfg.engine = Engine::Natural;
    const std::vector<LRect> nat{{0, 0, 960, 1080}, {0, 0, 960, 1080}};
    const auto out = computeLayout(nat, screen, cfg, {1, 2});
    const bool apart = out.size() == 2 && (out[0].x != out[1].x || out[0].y != out[1].y);
    printf("two workspaces, one place: %s\n", apart ? "apart" : "ON TOP OF EACH OTHER");
    check(apart, "the cards of two workspaces must not share a box");
    // One group: the windows keep their own places, as before.
    const auto one = computeLayout(nat, screen, cfg, {1, 1});
    check(one.size() == 2 && one[0].x == one[1].x, "one workspace keeps the windows together");
  }

  // The desktop view's grid: the groups make a square, the ceiling caps it, and the rows
  // past the ceiling hang below the screen for the scrolling to walk.
  {
    LayoutCfg cfg;
    cfg.engine = Engine::Natural;
    cfg.cellGap = 8.0;
    std::vector<LRect> nat;
    std::vector<int> groups;
    for (int g = 0; g < 4; ++g) {
      nat.push_back(LRect{0, 0, 1354, 756});
      groups.push_back(g);
    }
    const auto out = computeLayout(nat, screen, cfg, groups);
    int cols = 0, rows = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      bool newCol = true, newRow = true;
      for (size_t j = 0; j < i; ++j) {
        if (std::abs(out[j].x - out[i].x) < 1.0)
          newCol = false;
        if (std::abs(out[j].y - out[i].y) < 1.0)
          newRow = false;
      }
      cols += newCol ? 1 : 0;
      rows += newRow ? 1 : 0;
    }
    printf("four workspaces: %d columns, %d rows\n", cols, rows);
    check(cols == 2 && rows == 2, "four workspaces make two columns and two rows");

    cfg.cellCols = 2;
    cfg.cellRows = 1;
    const auto capped = computeLayout(nat, screen, cfg, groups);
    int below = 0;
    for (const auto &r : capped)
      if (r.y >= screen.h - 1.0)
        ++below;
    check(below == 2, "the row past the ceiling hangs below the screen");

  }

  // The knob lifts the height fit only and the width fit always binds, so more columns
  // make smaller tiles and the grid stays on the screen. The monitor and the pads mirror
  // the live desktop view: 1366x768 with the band and the caption room off the area.
  {
    LayoutCfg cfg;
    cfg.engine    = Engine::Natural;
    cfg.cellGap   = 8.0;
    cfg.padTop    = 190;
    cfg.padBottom = 112;
    const LRect mon{0, 0, 1366, 768};

    std::vector<LRect> four, five;
    std::vector<int>   ig, jg;
    for (int g = 0; g < 4; ++g) { four.push_back(LRect{0, 0, 1354, 756}); ig.push_back(g); }
    for (int g = 0; g < 5; ++g) { five.push_back(LRect{0, 0, 1354, 756}); jg.push_back(g); }

    cfg.cellScale = 1.0;
    const auto plain   = computeLayout(four, mon, cfg, ig);
    cfg.cellScale      = 1.125;
    const auto boosted = computeLayout(four, mon, cfg, ig);
    check(boosted[0].w > plain[0].w * 1.1, "the knob lifts the height-limited tiles");

    // The fit weighs the cell gap scaled, the walk draws it raw: the overshoot per row is
    // (cols-1)*gap*(1-scale), tolerable at these sizes and approved in the live look.
    double right = 0.0;
    for (const auto &r : boosted)
      right = std::max(right, r.x + r.w);
    double tol = 1.0 * 8.0 * (1.0 - boosted[0].w / 1354.0) + 0.5;
    check(right <= mon.w - cfg.padRight + tol, "the boosted grid stays inside the monitor");

    const auto fiveOut = computeLayout(five, mon, cfg, jg);
    check(fiveOut[0].w < boosted[0].w, "more columns make smaller tiles");
    right = 0.0;
    for (const auto &r : fiveOut)
      right = std::max(right, r.x + r.w);
    tol = 2.0 * 8.0 * (1.0 - fiveOut[0].w / 1354.0) + 0.5;
    check(right <= mon.w - cfg.padRight + tol, "the wide grid stays inside the monitor");
  }

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

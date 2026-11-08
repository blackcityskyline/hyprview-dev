// The launcher panel's box: centred under the strip band, never touching the edges, and the
// row list scrolling instead of running off the bottom.
//
// The panel's geometry decides whether the launcher reads as Spotlight or as a dropdown glued
// to the screen, and a screenshot cannot tell "centred in the grid" from "centred on the
// monitor" on a symmetric screen. Hyprland-free, like the rest of render/geometry.hpp.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/render/launcher_layout.hpp"

using hyprview::LRect;
using hyprview::render::launcherLayout;
using hyprview::render::launcherRowAt;
using hyprview::render::launcherScroll;
using hyprview::render::LauncherLayout;
using hyprview::render::LauncherMetrics;

namespace {

int g_checks = 0;

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

void near(double got, double expected, const char *what) {
  ++g_checks;
  if (std::fabs(got - expected) > 1e-6) {
    std::printf("FAIL %s: got %g, want %g\n", what, got, expected);
    std::exit(1);
  }
}

} // namespace

int main() {
  // A 1920x1080 area with the defaults: the panel is centred, the rows are inside it, and the
  // footer does not overlap the last row.
  const LRect area{0, 150, 1920, 900};
  const LauncherLayout l = launcherLayout(area, {});
  want(l.fit, true, "the panel fits a normal screen");
  near(l.panel.cx(), area.cx(), "the panel is centred in the grid's area");
  near(l.panel.y, area.y + 16.0, "it starts one gap under the band");
  want(l.rows.size() == 7, true, "seven rows by default");
  want(l.panel.x > 0.0 && l.panel.x + l.panel.w < area.w, true,
       "the panel leaves space on both sides");
  near(l.panel.x, area.w - (l.panel.x + l.panel.w), "and the two margins are equal");

  // The rows are laid under the field, in order, and the last one clears the footer.
  want(l.rows.front().y >= l.field.y + l.field.h, true, "the list starts under the field");
  for (std::size_t i = 1; i < l.rows.size(); ++i)
    want(l.rows[i].y > l.rows[i - 1].y, true, "the rows go down");
  want(l.rows.back().y + l.rows.back().h <= l.footer.y, true,
       "the last row clears the footer");

  // A narrow area gives way on the sides, not on the centring.
  LauncherMetrics wide;
  wide.width = 4000.0;
  const LauncherLayout l2 = launcherLayout(area, wide);
  near(l2.panel.w, 1920.0 - 2.0 * (1920.0 * 0.10), "an oversized width is capped by the inset");
  near(l2.panel.cx(), area.cx(), "the capped panel is still centred");

  // A short area drops rows rather than spilling: the list scrolls.
  LauncherMetrics shortArea;
  shortArea.rows = 10;
  const LauncherLayout l3 = launcherLayout(LRect{0, 150, 1200, 300}, shortArea);
  want(l3.fit, true, "a short area still fits");
  want(l3.rows.size() < 10, true, "rows are dropped, not drawn past the edge");
  want(l3.panel.y + l3.panel.h <= 450.0, true, "the panel stays inside the area");
  want(static_cast<int>(l3.rows.size()) >= 1, true, "at least one row survives");

  // Too small is not a panel with a negative width.
  want(launcherLayout(LRect{0, 0, 30, 900}, {}).fit, false, "a sliver of an area fits nothing");
  want(launcherLayout(LRect{0, 0, 900, 20}, {}).fit, false, "nor does a flat one");

  // Hit-testing is row-exact.
  want(launcherRowAt(l, l.rows[2].cx(), l.rows[2].cy()) == 2, true, "a row hit");
  want(launcherRowAt(l, l.panel.cx(), l.field.cy()) == -1, true, "the field is not a row");
  want(launcherRowAt(l, -10, -10) == -1, true, "off-panel is no row");

  // Scroll: the selection stays visible, the window moves by the least it can.
  want(launcherScroll(0, 20, 5) == 0, true, "the top needs no scroll");
  want(launcherScroll(4, 20, 5) == 0, true, "the fifth row is already visible");
  want(launcherScroll(5, 20, 5) == 1, true, "one past the window scrolls one row");
  want(launcherScroll(19, 20, 5) == 15, true, "the last row pins the window to the end");
  want(launcherScroll(3, 3, 5) == 0, true, "a list shorter than the window never scrolls");
  want(launcherScroll(3, 3, 0) == 0, true, "no visible rows is not a division by zero");

  std::printf("launcher layout: %d checks\n", g_checks);
  std::puts("launcher layout ok");
  return 0;
}

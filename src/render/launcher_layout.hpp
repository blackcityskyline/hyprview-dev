#pragma once

#include "../layout.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

// Pure launcher geometry, monitor-local LOGICAL px: no GL, no Hyprland, no config reads.
// The panel is a Spotlight-shaped column centred in the grid's own area, under the strip
// band: it never touches the screen edge, and the cards keep their side margins.

namespace hyprview::render {

struct LauncherMetrics {
  double width       = 620.0; // the panel's own width, before the side inset caps it
  double sideInset   = 0.10;  // share of the area kept free on each side, at least
  double minInset    = 24.0;
  double pad         = 14.0;
  double fieldH      = 52.0;
  double rowH        = 44.0;
  double rowGap      = 2.0;
  int    rows        = 7;     // visible rows; the list scrolls past this
  double gap         = 16.0;  // between the strip band and the panel
  double footerH     = 30.0;
  double round       = 18.0;
};

struct LauncherLayout {
  bool                fit = false; // the area was too small to place anything
  LRect               panel;
  LRect               field;
  std::vector<LRect>  rows;
  LRect               footer;
};

// `area` is the box the grid cards live in: the launcher centres in it and takes its width
// from it, so both modes agree on where the launcher stands.
[[nodiscard]] inline LauncherLayout launcherLayout(const LRect &area,
                                                   const LauncherMetrics &m) {
  LauncherLayout out;
  if (area.w < 40.0 || area.h < 40.0)
    return out;

  const double inset = std::max(m.minInset, area.w * m.sideInset);
  const double maxW  = area.w - 2.0 * inset;
  if (maxW < 40.0)
    return out;
  const double w = std::min(std::max(m.width, 40.0), maxW);

  // Rows give way before the panel runs off the bottom.
  int rows = std::max(1, m.rows);
  const double chrome = m.pad * 2.0 + m.fieldH + m.footerH + m.gap + m.rowGap * (rows - 1);
  const double room   = area.h - m.gap - chrome;
  if (room < m.rowH) {
    rows = 0;
  } else if (room < m.rowH * rows) {
    rows = static_cast<int>(room / m.rowH);
  }
  rows = std::clamp(rows, 0, std::max(0, m.rows));

  const double h = chrome + m.rowH * rows;
  const double x = area.x + (area.w - w) / 2.0;
  const double y = area.y + m.gap;

  out.fit   = true;
  out.panel = LRect{x, y, w, h};
  out.field = LRect{x + m.pad, y + m.pad, w - 2.0 * m.pad, m.fieldH};
  out.footer = LRect{x + m.pad, y + h - m.pad - m.footerH, w - 2.0 * m.pad, m.footerH};

  out.rows.reserve(static_cast<std::size_t>(rows));
  double ry = out.field.y + out.field.h + m.gap;
  for (int i = 0; i < rows; ++i) {
    out.rows.push_back(LRect{x + m.pad, ry, w - 2.0 * m.pad, m.rowH});
    ry += m.rowH + m.rowGap;
  }
  return out;
}

// Which row the pointer is over, or -1. Bands own no gaps, so a hover cannot land between two
// rows and mean neither.
[[nodiscard]] inline int launcherRowAt(const LauncherLayout &l, double lx, double ly) {
  for (std::size_t i = 0; i < l.rows.size(); ++i)
    if (l.rows[i].contains(lx, ly))
      return static_cast<int>(i);
  return -1;
}

// The first row drawn: the selection stays inside the visible window, and the list scrolls
// by whole rows. Returns 0 when everything fits.
[[nodiscard]] inline int launcherScroll(int selection, int total, int visible) {
  if (visible <= 0 || total <= visible || selection <= 0)
    return 0;
  return std::clamp(selection - visible + 1, 0, total - visible);
}

} // namespace hyprview::render

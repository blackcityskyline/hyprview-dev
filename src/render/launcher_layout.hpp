#pragma once

#include "../layout.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hyprview::render {

struct LauncherMetrics {
  double width       = 620.0;
  double sideInset   = 0.10;
  double minInset    = 24.0;
  double pad         = 14.0;
  double fieldH      = 52.0;
  double rowH        = 44.0;
  double rowGap      = 2.0;
  int    rows        = 7;
  double gap         = 16.0;
  double footerH     = 30.0;
  double round       = 18.0;
};

struct LauncherLayout {
  bool               fit = false;
  LRect              panel;
  LRect              field;
  std::vector<LRect> rows;
  LRect              footer;
};

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

[[nodiscard]] inline int launcherRowAt(const LauncherLayout &l, double lx, double ly) {
  for (std::size_t i = 0; i < l.rows.size(); ++i)
    if (l.rows[i].contains(lx, ly))
      return static_cast<int>(i);
  return -1;
}

[[nodiscard]] inline int launcherScroll(int selection, int total, int visible) {
  if (visible <= 0 || total <= visible || selection <= 0)
    return 0;
  return std::clamp(selection - visible + 1, 0, total - visible);
}

struct LauncherMenu {
  bool               fit = false;
  LRect              panel;
  std::vector<LRect> rows;
};

[[nodiscard]] inline LauncherMenu launcherMenuAt(const LauncherLayout &l, int selection,
                                                 int count, double width, double rowH) {
  LauncherMenu out;
  if (!l.fit || selection < 0 || selection >= static_cast<int>(l.rows.size()) || count <= 0 ||
      rowH <= 0.0)
    return out;
  const LRect &row = l.rows[static_cast<std::size_t>(selection)];
  const double w = std::min(std::max(width, 40.0), row.w);
  const double h = rowH * static_cast<double>(count);
  const double x = row.x + row.w - w;
  const double floor = l.panel.y + l.panel.h - 8.0;
  double y = row.y + row.h + 2.0;
  if (y + h > floor)
    y = std::max(l.panel.y + 8.0, row.y - h - 2.0);
  out.fit = true;
  out.panel = LRect{x, y, w, h};
  out.rows.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i)
    out.rows.push_back(LRect{x, y + rowH * static_cast<double>(i), w, rowH});
  return out;
}

}

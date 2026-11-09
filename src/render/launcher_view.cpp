
#include "../config/config.hpp"
#include "../overview.hpp"

#include <algorithm>
#include <cmath>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "gl_util.hpp"

using Render::GL::g_pHyprOpenGL;

namespace hyprview {

namespace {

constexpr double kTextPad = 12.0;

void drawText(const SP<Render::ITexture> &tex, const LRect &row, double s, float a,
              bool right, double pad = kTextPad) {
  if (!tex || tex->m_size.x <= 0 || tex->m_size.y <= 0)
    return;
  const double w = tex->m_size.x;
  const double h = tex->m_size.y;
  const double x = right ? row.x + row.w - kTextPad - w : row.x + pad;
  const double y = row.y + (row.h - h) / 2.0;
  g_pHyprOpenGL->renderTexture(tex, pxb(LRect{x, y, w, h}, s), {.a = a});
}

}

void Overview::renderLauncher() const {
  if (!m_launcher.active())
    return;
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const double e = eased();
  if (e <= 0.01)
    return;
  const auto l = launcherLayoutNow();
  if (!l.fit)
    return;

  const auto mt = launcherMetrics();
  const double s = m->m_scale;
  const float a = static_cast<float>(std::clamp(e, 0.0, 1.0));
  const float rp = cfg::look.preview_round_power;
  const int round = static_cast<int>(std::round(mt.round));
  const int rowRound = std::min(round, static_cast<int>(mt.rowH / 2.0));

  dropShadow(l.panel, s, cfg::colors.shadow.get(a), 8.0, cfg::look.shadow_radius_lift,
             cfg::look.shadow_alpha, round, rp);
  safeRenderRect("launcher", pxb(l.panel, s), cfg::colors.launcher_panel.get(a),
                 {.round = pxr(round, s), .roundingPower = rp});
  strokeRing(l.panel, s, cfg::colors.surface.get(a), 1, round, rp);

  safeRenderRect("launcher_field", pxb(l.field, s), cfg::colors.strip_card.get(a),
                 {.round = pxr(rowRound, s), .roundingPower = rp});

  const auto titleCol = cfg::colors.label.get(1.0);
  const auto hintCol = cfg::colors.launcher_hint.get(1.0);
  const auto selectCol = cfg::colors.launcher_select.get(a);
  const auto accent = cfg::colors.select_border.get(a);
  const int font = cfg::launcher.font_size.get();

  const auto query = m_launcher.query();
  const auto qTex = m_pixel.fixedLabel(
      pixel::Slot::LauncherQuery,
      query.empty() ? std::string("type to search") : query,
      query.empty() ? hintCol : titleCol, font + 1,
      static_cast<int>(std::max(20.0, l.field.w - 2.0 * kTextPad)));
  if (qTex && qTex->m_size.x > 0) {
    const LRect at{l.field.x + kTextPad, l.field.y + (l.field.h - qTex->m_size.y) / 2.0,
                   std::min<double>(qTex->m_size.x, l.field.w - 2.0 * kTextPad),
                   qTex->m_size.y};
    g_pHyprOpenGL->renderTexture(qTex, pxb(at, s), {.a = a});
  }

  const int total = static_cast<int>(m_launcher.hits().size());
  const int visible = static_cast<int>(l.rows.size());
  const int first = render::launcherScroll(m_launcher.selection(), total, visible);
  const int sel = m_launcher.selection();

  for (int vi = 0; vi < visible; ++vi) {
    const int hit = first + vi;
    if (hit >= total)
      break;
    const auto *item = m_launcher.itemAt(static_cast<std::size_t>(hit));
    if (!item)
      continue;
    const LRect row = l.rows[static_cast<std::size_t>(vi)];
    if (hit == sel) {
      safeRenderRect("launcher_sel", pxb(row, s), selectCol,
                     {.round = pxr(rowRound, s), .roundingPower = rp});
      strokeRing(row, s, accent, 1, rowRound, rp);
    }

    const auto detail = item->detail.empty()
                            ? SP<Render::ITexture>{}
                            : m_pixel.menuGlyph(item->detail, hintCol, std::max(9, font - 2));
    const double detailW = detail ? detail->m_size.x : 0.0;
    double leftPad = kTextPad;
    if (item->kind == model::LauncherKind::App && !item->icon.empty()) {
      const double iconPx = std::min(26.0, row.h - 14.0);
      const auto icon = m_pixel.appIcon(
          item->icon, std::max(8, static_cast<int>(std::lround(iconPx * s))));
      if (icon) {
        const LRect box{row.x + 10, row.y + (row.h - iconPx) / 2.0, iconPx, iconPx};
        g_pHyprOpenGL->renderTexture(icon, pxb(box, s), {.a = a});
        leftPad = 10.0 + iconPx + 10.0;
      }
    }
    const int titleMax =
        static_cast<int>(std::max(30.0, row.w - leftPad - kTextPad - detailW - 16.0));
    const auto title =
        item->kind == model::LauncherKind::Action || item->kind == model::LauncherKind::App
            ? m_pixel.menuGlyph(item->title, titleCol, font)
            : m_pixel.subjectLabel(item->subj, item->title, titleCol, font, titleMax);
    drawText(title, row, s, a, false, leftPad);
    drawText(detail, row, s, a * 0.9F, true);
  }

  if (m_launcher.menuOpen()) {
    const auto &acts = m_launcher.menuActions();
    const auto menu =
        render::launcherMenuAt(l, sel, static_cast<int>(acts.size()),
                               cfg::launcher.width.get() / 3.0, cfg::launcher.row_height.get());
    if (menu.fit) {
      safeRenderRect("launcher_menu", pxb(menu.panel, s), cfg::colors.launcher_panel.get(a),
                     {.round = pxr(rowRound, s), .roundingPower = rp});
      strokeRing(menu.panel, s, accent, 1, rowRound, rp);
      for (std::size_t i = 0; i < menu.rows.size(); ++i) {
        const bool on = static_cast<int>(i) == m_launcher.menuSelection();
        if (on)
          safeRenderRect("launcher_menu_sel", pxb(menu.rows[i], s), selectCol,
                         {.round = pxr(rowRound, s), .roundingPower = rp});
        const auto text = m_pixel.menuGlyph(model::verbLabel(acts[i]), titleCol, font - 2);
        drawText(text, menu.rows[i], s, a, false);
      }
    }
  }

  const auto footer = m_pixel.menuGlyph("enter open   ctrl+b actions   esc close", hintCol,
                                        std::max(9, font - 3));
  drawText(footer, l.footer, s, a * 0.75F, false);
}

}

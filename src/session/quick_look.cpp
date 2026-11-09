#include "../overview.hpp"
#include "../clock/curves.hpp"
#include "../debug/log.hpp"

#include <algorithm>

namespace hyprview {

namespace {
constexpr double kQuickLookPad = 14.0;
}

void Overview::openQuickLook() {
  const int idx = m_scene.focused();
  if (m_view.quickLook() || idx < 0)
    return;
  const auto &t = m_scene.tiles()[static_cast<std::size_t>(idx)];
  const LRect from = currentBox(t, idx);
  m_view.beginQuickLook(from);

  const auto ql = anim::cfg(anim::Id::QuickLook);
  m_place.clocks.quickLook.begin();
  debug::trace::animStart("quicklook", "quicklook", "", ql.ms, ql.curve.c_str());
  m_place.clocks.quickLookMs    = ql.ms;
  m_place.clocks.quickLookCurve = ql.curve;
  const LRect to = quickLookTarget(t);
  debug::dbg("QL|open|from=" + std::to_string(static_cast<int>(from.x)) + "," +
             std::to_string(static_cast<int>(from.y)) + "," +
             std::to_string(static_cast<int>(from.w)) + "," +
             std::to_string(static_cast<int>(from.h)) + "|to=" +
             std::to_string(static_cast<int>(to.x)) + "," + std::to_string(static_cast<int>(to.y)) +
             "," + std::to_string(static_cast<int>(to.w)) + "," +
             std::to_string(static_cast<int>(to.h)) + "|ms=" + std::to_string(ql.ms));
  damage();
}

void Overview::closeQuickLook() {
  if (!m_view.quickLook() || m_view.quickLookClosing())
    return;
  const int idx = m_scene.focused();
  m_view.startQuickLookClose(
      idx >= 0 ? currentBox(m_scene.tiles()[static_cast<std::size_t>(idx)], idx) : LRect{});
  m_place.clocks.quickLook.seek(1.0 - quickLookProgress(), quickLookDur());
  debug::trace::animStart("quicklook_close", "quicklook", "", quickLookDur(),
                          m_place.clocks.quickLookCurve.c_str());
  debug::dbg("QL|close");
  damage();
}

void Overview::toggleQuickLook() {
  if (m_view.quickLook())
    closeQuickLook();
  else
    openQuickLook();
}

double Overview::quickLookDur() const {
  return m_place.clocks.quickLookMs > 0 ? m_place.clocks.quickLookMs
                                        : anim::cfg(anim::Id::QuickLook).ms;
}

double Overview::quickLookProgress() const {
  const double raw = m_place.clocks.quickLook.raw(quickLookDur());
  const double f   = m_view.quickLookClosing() ? 1.0 - raw : raw;
  const auto  &c   = m_place.clocks.quickLookCurve.empty()
                         ? anim::cfg(anim::Id::QuickLook).curve
                         : m_place.clocks.quickLookCurve;
  return curves::eval(c, f);
}

LRect Overview::quickLookTarget(const model::Tile &t) const {
  const auto m = m_monitor.lock();
  if (!m)
    return {};
  const LRect screen{0.0, 0.0, m->m_size.x, m->m_size.y};
  std::vector<LRect> bands;
  bands.push_back(stripBand());
  if (m_launcher.active()) {
    const auto l = launcherLayoutNow();
    if (l.fit)
      bands.push_back(LRect{screen.x, l.panel.y, screen.w, l.panel.h});
  }
  const auto area = render::quickLookArea(screen, bands);

  const auto w    = cardOf(t).isTab() ? ownerWindow(t) : tileWindow(t);
  const auto size = w ? w->sizeAnimation()->goal() : Vector2D{1.0, 1.0};
  return render::quickLookTarget(area, size.x / std::max(1.0, size.y), kQuickLookPad);
}

}

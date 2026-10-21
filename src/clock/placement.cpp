#include "placement.hpp"

#include <algorithm>

#include "../anim.hpp"
#include "../layout.hpp"
#include "../debug/log.hpp"
#include "curves.hpp"

namespace hyprview::clock {

namespace {

double staggered(double base, int i, int n) {
  return anim::stagger(base, i, n,
                       std::min(anim::param(anim::Param::Stagger),
                                anim::param(anim::Param::StaggerPerTile) * n));
}

}

anim::Id Placement::entryLeaf(const view::State &view) const {
  if (view.expoFlip() != 0)
    return view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return view.wsSlideDir() != 0 ? anim::Id::WsEnter : anim::Id::Appear;
}

anim::Id Placement::ghostLeaf(const view::State &view) const {
  if (view.expoFlip() != 0)
    return view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return view.wsSlideDir() != 0 ? anim::Id::WsExit : anim::Id::Appear;
}

anim::Id Placement::glideLeaf(const view::State &view, bool opening,
                               bool carousel) const {
  if (view.jumpMode())
    return opening ? anim::Id::JumpIn : anim::Id::JumpOut;
  if (view.expoFlip() != 0)
    return view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return carousel ? anim::Id::CarouselStep : anim::Id::Glide;
}

anim::Id Placement::masterId(const view::State &view, bool opening) const {
  switch (anim::masterLeaf(view.jumpMode(), opening)) {
    case anim::MasterLeaf::Open:    return anim::Id::Open;
    case anim::MasterLeaf::JumpOut: return anim::Id::JumpOut;
    case anim::MasterLeaf::Close:   break;
  }
  return anim::Id::Close;
}

double Placement::tileProgress(int i, const view::State &view, bool opening,
                               const model::Scene &scene) const {
  // Read clocks.tileMs, not recipes.glide.ms: a glideLeaf() flip jumps progress.
  const double glideMs = (clocks.tileMs > 0) ? clocks.tileMs : recipes.glide.ms;
  const double base = opening ? clocks.tiles.raw(glideMs) : progress.raw();
  // The FOCUSED tile is never staggered: stagger() assumes a rising 0..1, so on close it would delay the landing.
  if (view.run() || i == scene.focused())
    return base;
  return staggered(base, i, static_cast<int>(scene.tiles().size()));
}

double Placement::entryProgress(int i, const model::Scene &scene) const {
  return curves::eval(
      recipes.entry.curve,
      staggered(clocks.rebuild.raw(recipes.entry.ms), i,
                static_cast<int>(scene.tiles().size())));
}

double Placement::entryNow(const model::Tile &t, int i,
                           const model::Scene &scene) const {
  return t.landed >= 1.0 ? 1.0 : entryProgress(i, scene);
}

double Placement::entryFade(const model::Tile &t, int i, const Frame &f) const {
  // Only on a close that is NOT committing: fading a window about to become real double-images.
  if (!f.opening && anim::tileExit(f.goesHome(t)) == anim::TileExit::InPlace)
    return m_eased;
  if (t.landed >= 1.0 || recipes.enterStyle.travel != anim::Travel::Fade)
    return 1.0;
  return entryProgress(i, f.scene);
}

LRect Placement::box(const model::Tile &t, int i, const Frame &f) const {
  const std::string &curve =
      clocks.tileCurve.empty() ? recipes.glide.curve : clocks.tileCurve;
  const double e = curves::eval(curve, tileProgress(i, f.view, f.opening, f.scene));
  // The closed end of the flight: where the card belongs.
  const auto &a = f.opening ? t.natural : t.home;
  const auto &b = t.target;
  LRect r{lerp(a.x, b.x, e), lerp(a.y, b.y, e), lerp(a.w, b.w, e),
          lerp(a.h, b.h, e)};
  // A card off the carousel run has a zeroed target: nothing paints it, hold the last box.
  if (f.view.run() && b.w < 1.0 && a.w >= 1.0)
    r = a;
  // An empty natural grows out of the corner if interpolated.
  if (a.w < 1.0 && b.w >= 1.0)
    r = b;
  // The deck's hide motion, in the grid as well as the run: close() pins a card that cannot land.
  if (!f.opening && anim::cardHides(f.view.carouselDepth(static_cast<std::size_t>(i)),
                                    f.goesHome(t))) {
    const double k = anim::hideShrink(exit(f.opening), anim::param(anim::Param::Collapse));
    const double cx = r.cx(), cy = r.cy();
    r = LRect{cx - r.w * k * 0.5, cy - r.h * k * 0.5, r.w * k, r.h * k};
  }
  // TILEBOX: the box every draw path agrees on; caption and capture read this one number.
  debug::trace::emitState("TILEBOX|i=" + std::to_string(i) + "|subj=" +
                          std::to_string(t.subj.id) + "|a=" + std::to_string(a.x) + "," +
                          std::to_string(a.y) + "," + std::to_string(a.w) + "," +
                          std::to_string(a.h) + "|b=" + std::to_string(b.x) + "," +
                          std::to_string(b.y) + "," + std::to_string(b.w) + "," +
                          std::to_string(b.h) + "|r=" + std::to_string(r.x) + "," +
                          std::to_string(r.y) + "," + std::to_string(r.w) + "," +
                          std::to_string(r.h) + "|run=" + std::to_string(f.view.run() ? 1 : 0) +
                          "|e=" + std::to_string(e));
  const double ap = entryNow(t, i, f.scene);
  if (ap >= 1.0)
    return r;
  // Three branches, and all three are reachable: a style no branch honours is not a setting.
  if (recipes.enterStyle.travel == anim::Travel::Slide) {
    // A slide needs somewhere to come from; without a direction only the scale-in is left.
    if (f.view.wsSlideDir() != 0) {
      if (recipes.enterStyle.axis == anim::Axis::Horizontal)
        r.x += static_cast<double>(f.view.wsSlideDir()) * f.monW * (1.0 - ap);
      else
        r.y -= f.monH * (1.0 - ap);
      return r;
    }
  } else if (recipes.enterStyle.travel == anim::Travel::Fade) {
    return r;
  }
  const double k = 1.0 - anim::param(anim::Param::PopScale) * (1.0 - ap);
  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
  return LRect{cx - r.w * k / 2.0, cy - r.h * k / 2.0, r.w * k, r.h * k};
}

std::vector<std::pair<model::SubjectRef, LRect>>
Placement::capture(const Frame &f, model::SubjectRef exclude) const {
  std::vector<std::pair<model::SubjectRef, LRect>> out;
  const auto &tiles = f.scene.tiles();
  out.reserve(tiles.size());
  for (std::size_t i = 0; i < tiles.size(); ++i)
    if (tiles[i].subj != exclude)
      out.emplace_back(tiles[i].subj, box(tiles[i], static_cast<int>(i), f));
  return out;
}

void Placement::resolve(const view::State &view, bool opening, bool carousel) {
  recipes.resolve(glideLeaf(view, opening, carousel), entryLeaf(view), ghostLeaf(view));
  const auto id = masterId(view, opening);
  m_masterMs = anim::cfg(id).ms;
  m_eased    = curves::eval(anim::cfg(id).curve, progress.raw());
}

} // namespace hyprview::clock

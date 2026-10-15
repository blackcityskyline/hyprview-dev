#include "placement.hpp"

#include <algorithm>

#include "../anim.hpp"
#include "curves.hpp"

namespace gloview::clock {

namespace {

// The ladder spread is the user's, capped at 1: Stagger is the ceiling,
// StaggerPerTile grows it with the tile count and must not push past it.
double staggered(double base, int i, int n) {
  return anim::stagger(base, i, n,
                       std::min(anim::param(anim::Param::Stagger),
                                anim::param(anim::Param::StaggerPerTile) * n));
}

} // namespace

// imperative rule > schema key > default; off -> 1ms, speed divides base
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

double Placement::eased(const view::State &view, bool opening) const {
  return curves::eval(anim::cfg(masterId(view, opening)).curve, progress.raw());
}

double Placement::masterDuration(const view::State &view, bool opening) const {
  return anim::cfg(masterId(view, opening)).ms;
}

double Placement::tileProgress(int i, const view::State &view, bool opening,
                               const model::Scene &scene) const {
  // Read clocks.tileMs, not recipes.glide.ms: a glideLeaf() flip jumps progress.
  const double glideMs = (clocks.tileMs > 0) ? clocks.tileMs : recipes.glide.ms;
  const double base = opening ? clocks.tiles.raw(glideMs) : progress.raw();
  // The FOCUSED tile is never staggered: stagger() assumes a rising 0..1, so on close it
  // would delay the landing -- the same flight, timed differently.
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

void Placement::resolve(const view::State &view, bool opening, bool carousel) {
  recipes.resolve(glideLeaf(view, opening, carousel), entryLeaf(view), ghostLeaf(view));
}

} // namespace gloview::clock
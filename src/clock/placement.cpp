#include "placement.hpp"

#include <algorithm>

#include "../anim.hpp"
#include "../layout.hpp"
#include "../debug/log.hpp"
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

double Placement::entryNow(const model::Tile &t, int i,
                           const model::Scene &scene) const {
  return t.landed >= 1.0 ? 1.0 : entryProgress(i, scene);
}

double Placement::entryFade(const model::Tile &t, int i, const Frame &f) const {
  // A card with nowhere to go cannot fly anywhere -- close() pinned its natural box to its
  // own tile box -- so it leaves by dissolving on the master clock instead of standing
  // still until the overlay is torn out from under it. Only ever on a close that is NOT
  // committing: fading a window that is about to become real reads as a double image
  // (AGENTS.md), so every tile that does go home stays at full alpha and flies.
  if (!f.opening && anim::tileExit(f.goesHome(t)) == anim::TileExit::InPlace)
    return m_eased;
  // A fade is alpha only, and it fades whenever the tile is playing its entry.
  if (t.landed >= 1.0 || recipes.enterStyle.travel != anim::Travel::Fade)
    return 1.0;
  return entryProgress(i, f.scene);
}

LRect Placement::box(const model::Tile &t, int i, const Frame &f) const {
  const std::string &curve =
      clocks.tileCurve.empty() ? recipes.glide.curve : clocks.tileCurve;
  const double e = curves::eval(curve, tileProgress(i, f.view, f.opening, f.scene));
  const auto &a = t.natural;
  const auto &b = t.target;
  LRect r{lerp(a.x, b.x, e), lerp(a.y, b.y, e), lerp(a.w, b.w, e),
          lerp(a.h, b.h, e)};
  // A card off the carousel run has a zeroed target. Nothing paints it, so it must not
  // resize on its way there either -- hold the box it was last shown at.
  if (f.view.run() && b.w < 1.0 && a.w >= 1.0)
    r = a;
  // And a card that has NEVER had a box of its own has an empty natural: a tab, which is a
  // slot inside a window and not a place on the desktop. Interpolating from nothing starts
  // the card at the origin, so it grew out of the top left corner of the monitor and only
  // then became itself -- which is the card everyone can see flying out of the corner on
  // every step. It has exactly one place it has ever been, and that is its target.
  //
  // Here rather than in the callers because this is the one function every draw path goes
  // through: startTileGlide, replayReflow and stepAltTab each set natural by their own rules
  // and each of them missed this case at least once.
  if (a.w < 1.0 && b.w >= 1.0)
    r = b;
  // The deck's hide motion, in the grid as well as the run. A card that cannot land was
  // pinned to its own box by close(); without this it simply stood there while the backdrop
  // faded and was gone in a frame.
  if (!f.view.run() && !f.opening && anim::cardHides(0, f.goesHome(t))) {
    const double k = anim::hideShrink(exit(f.opening), anim::param(anim::Param::Collapse));
    const double cx = r.cx(), cy = r.cy();
    r = LRect{cx - r.w * k * 0.5, cy - r.h * k * 0.5, r.w * k, r.h * k};
  }
  // TILEBOX: the box every draw path agrees on, with the two it was interpolated from. The
  // overlay's cards shrink in rounding steps with distance and the caption and the capture
  // both read this one number, so "the card is at the wrong size" and "the pill is at the
  // wrong place" have not been separable from the outside: a card's OWN rect was traced by
  // name (title_card) and read as the window's box, and every conclusion drawn from it was
  // about the wrong rectangle. One line, both ends in it.
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
  // Three branches, and all three are reachable. A style that no branch honours is not a
  // setting.
  if (recipes.enterStyle.travel == anim::Travel::Slide) {
    // A slide needs somewhere to come from. Without a direction there is no edge to enter
    // from, so the tile is already where it belongs and only its scale-in is missing.
    if (f.view.wsSlideDir() != 0) {
      if (recipes.enterStyle.axis == anim::Axis::Horizontal)
        r.x += static_cast<double>(f.view.wsSlideDir()) * f.monW * (1.0 - ap);
      else
        r.y -= f.monH * (1.0 - ap);
      return r;
    }
  } else if (recipes.enterStyle.travel == anim::Travel::Fade) {
    return r; // alpha only -- entryFade() reads the same clock for it
  }
  // Pop, and a slide with no direction: the scale-in.
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

} // namespace gloview::clock
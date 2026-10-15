#pragma once

#include "../anim_recipes.hpp"
#include "../model/scene.hpp"
#include "../view/state.hpp"
#include "clock.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include "../layout.hpp"
#include "progress.hpp"

namespace gloview::clock {

// Everything that answers "how far along is this", and nothing else.
//
// Owns the three things that make a position mean something -- the clocks that advance,
// the run's Progress, and the resolved recipes -- and answers from them. It does not draw,
// does not read the compositor, and does not decide anything: it is the one place the
// numbers come from, so two callers asking at the same moment cannot disagree.
//
// The scene and the view are passed per call rather than held. They belong to the run, not
// to the clock, and holding them here would make this the coordinator again under a smaller
// name -- which is the shape this refactor is trying to leave.
class Placement {
  public:
    // The run's overall fade, on the master clock's own curve. Read LIVE, so a config edit
    // mid-fade retimes the rest of the ramp.
    // The run's overall fade, on the master clock's own curve. Read LIVE, so a config edit
    // mid-fade retimes the rest of the ramp.
    //
    // Resolved once per frame by resolve(), not per call. Six files ask for eased() and two
    // for masterDuration(), and both are the same curve lookup plus the same master leaf --
    // evaluating that twenty times a frame across the paint is work for one answer. A
    // config edit still applies on the next frame, which is the same thing the live read
    // bought.
    [[nodiscard]] double eased() const { return m_eased; }
    [[nodiscard]] double masterDuration() const { return m_masterMs; }

    // How far the close has run, 0 -> 1.
    [[nodiscard]] double exit(bool opening) const { return progress.exit(opening); }



    // Which behavior the master clock is currently playing. Three leaves, because the run
    // can be open, jumping out, or closing, and each has its own row.
    [[nodiscard]] anim::Id masterId(const view::State &view, bool opening) const;
    [[nodiscard]] anim::Id entryLeaf(const view::State &view) const;
    [[nodiscard]] anim::Id ghostLeaf(const view::State &view) const;
    [[nodiscard]] anim::Id glideLeaf(const view::State &view, bool opening,
                                     bool carousel) const;

    // Tile i's flight progress. currentBox lerps natural -> target as e RISES, so close
    // feeds it the run's progress as-is; flipping that put every card on its natural box on
    // the first close frame.
    [[nodiscard]] double tileProgress(int i, const view::State &view, bool opening,
                                      const model::Scene &scene) const;

    [[nodiscard]] double entryProgress(int i, const model::Scene &scene) const;

    // What a position can only be measured against: the run's modes, the tile list, the
    // monitor it is laid out in, and whether this tile has somewhere to land.
    struct Frame {
      const view::State &view;
      const model::Scene &scene;
      // The workspace the run returns to. A tile on it flies home; one on any other
      // workspace cannot, and leaves by fading instead. Null means "nothing to compare
      // against", which reads as home -- the flight the run has always done.
      PHLWORKSPACEREF landing;
      bool opening;
      double monW, monH;

      Frame(const view::State &v, const model::Scene &sc, PHLWORKSPACEREF land, bool open,
            double w, double h)
          : view(v), scene(sc), landing(land), opening(open), monW(w), monH(h) {}

      [[nodiscard]] bool goesHome(const model::Tile &t) const {
        const auto w = t.win.lock();
        if (!w || !landing)
          return true;
        return w->m_workspace == landing;
      }
    };

    // Where tile i is this frame, letterboxed by nothing: the raw animated box.
    [[nodiscard]] LRect box(const model::Tile &t, int i, const Frame &f) const;

    // How far ALONG its entry this tile is. t.landed is the flag (0 = armed, 1 = already
    // on screen) and entryProgress is the clock it is played against; a tile that is not
    // playing must not read the clock, or a stale rebuild clock would keep shrinking cards
    // that have already landed.
    [[nodiscard]] double entryNow(const model::Tile &t, int i,
                                  const model::Scene &scene) const;

    // The fade paired with the entry clock, for a card that has nowhere to go.
    [[nodiscard]] double entryFade(const model::Tile &t, int i, const Frame &f) const;

    // Every tile's box as it is this frame, paired with its window, skipping `exclude`.
    //
    // The layout pass captures this BEFORE it rebuilds, and a swap or a drop hands the
    // captured boxes to startTileGlide as each tile's `from`. Twelve call sites, all of them
    // asking the same question at slightly different moments, and none of them able to
    // disagree with where the tile is being drawn.
    [[nodiscard]] std::vector<std::pair<PHLWINDOW, LRect>>
    capture(const Frame &f, const PHLWINDOW exclude) const;

    // Re-resolve the six recipes for the leaves the view currently selects.
    void resolve(const view::State &view, bool opening, bool carousel);

  // Public by value: these are the three things a position is made of, and everything
  // that moves reads and writes them directly. Getters would mean a writer going through a
  // reference-returning method, which is a way of saying "this class has no state" while it
  // plainly does.
  double m_eased = 0.0;
  double m_masterMs = 0.0;
  Clocks clocks;
  Progress progress;
  anim::Recipes recipes;
};

} // namespace gloview::clock
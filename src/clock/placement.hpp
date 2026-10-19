#pragma once

#include "../anim_recipes.hpp"
#include "../model/content_source.hpp"
#include "../model/scene.hpp"
#include "../view/state.hpp"
#include "clock.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include "../layout.hpp"
#include "progress.hpp"

namespace hyprview::clock {

// Owns the clocks, the run's Progress and the resolved recipes, so two callers cannot disagree.
class Placement {
  public:
    // The run's overall fade, on the master clock's curve; a config edit applies next frame.
    [[nodiscard]] double eased() const { return m_eased; }
    [[nodiscard]] double masterDuration() const { return m_masterMs; }

    [[nodiscard]] double exit(bool opening) const { return progress.exit(opening); }

    // Three leaves, because the run can be open, jumping out or closing.
    [[nodiscard]] anim::Id masterId(const view::State &view, bool opening) const;
    [[nodiscard]] anim::Id entryLeaf(const view::State &view) const;
    [[nodiscard]] anim::Id ghostLeaf(const view::State &view) const;
    [[nodiscard]] anim::Id glideLeaf(const view::State &view, bool opening,
                                     bool carousel) const;

    // tileProgress lerps natural -> target as e RISES, so close feeds it run progress as-is.
    [[nodiscard]] double tileProgress(int i, const view::State &view, bool opening,
                                      const model::Scene &scene) const;

    [[nodiscard]] double entryProgress(int i, const model::Scene &scene) const;

    struct Frame {
      const view::State &view;
      const model::Scene &scene;
      // Where the run returns to: that tile flies home, any other fades; a default landing
      // means home. Asked through the contract, not a window handle.
      const model::IContentSource &src;
      model::GroupRef landing;
      bool opening;
      double monW, monH;

      Frame(const view::State &v, const model::Scene &sc,
            const model::IContentSource &source, model::GroupRef land, bool open,
            double w, double h)
          : view(v), scene(sc), src(source), landing(land), opening(open), monW(w),
            monH(h) {}

      [[nodiscard]] bool goesHome(const model::Tile &t) const {
        return landing == model::GroupRef{} || src.groupOf(t.subj) == landing;
      }
    };

    [[nodiscard]] LRect box(const model::Tile &t, int i, const Frame &f) const;

    // t.landed is the flag, entryProgress the clock; a landed tile must not read it.
    [[nodiscard]] double entryNow(const model::Tile &t, int i,
                                  const model::Scene &scene) const;

    [[nodiscard]] double entryFade(const model::Tile &t, int i, const Frame &f) const;

    // Subjects rather than windows: the contract is what keeps this file compilable by ctest.
    [[nodiscard]] std::vector<std::pair<model::SubjectRef, LRect>>
    capture(const Frame &f, model::SubjectRef exclude) const;

    void resolve(const view::State &view, bool opening, bool carousel);

  // Public by value: they are the three things a position is made of. A getter would say
  // "this class has no state" while it plainly does.
  double m_eased = 0.0;
  double m_masterMs = 0.0;
  Clocks clocks;
  Progress progress;
  anim::Recipes recipes;
};

} // namespace hyprview::clock

#pragma once

#include "anim.hpp"

// The resolved animation recipes: resolve() is the ONLY writer. Reading config here is the
// deliberate exception to AGENTS.md 2.16 -- the glide leaf changes mid-run, the lift is fixed.
namespace gloview::anim {

class Recipes {
  public:
    Cfg glide;
    Cfg entry;
    Cfg ghost;
    Cfg lift;
    Move enterStyle{Travel::Slide, Axis::Horizontal};
    Move exitStyle{Travel::Slide, Axis::Horizontal};

    // Which behavior the run is currently playing: they change mid-run, hence passed in
    // rather than read.
    void resolve(Id glideLeaf, Id entryLeaf, Id ghostLeaf) {
      glide     = cfg(glideLeaf);
      entry     = cfg(entryLeaf);
      ghost     = cfg(ghostLeaf);
      lift      = cfg(Id::Lift);
      enterStyle = parseMove(style("ws_enter"));
      exitStyle  = parseMove(style("ws_exit"));
    }
};

} // namespace gloview::anim
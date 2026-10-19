#pragma once

#include "anim.hpp"

// resolve() is the ONLY writer; reading config here is the deliberate exception to AGENTS.md 2.16.
namespace hyprview::anim {

class Recipes {
  public:
    Cfg glide;
    Cfg entry;
    Cfg ghost;
    Cfg lift;
    Move enterStyle{Travel::Slide, Axis::Horizontal};
    Move exitStyle{Travel::Slide, Axis::Horizontal};

    void resolve(Id glideLeaf, Id entryLeaf, Id ghostLeaf) {
      glide     = cfg(glideLeaf);
      entry     = cfg(entryLeaf);
      ghost     = cfg(ghostLeaf);
      lift      = cfg(Id::Lift);
      enterStyle = parseMove(style("ws_enter"));
      exitStyle  = parseMove(style("ws_exit"));
    }
};

} // namespace hyprview::anim

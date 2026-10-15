#pragma once

// The six resolved animation recipes the run plays: how long each clock runs, on what
// curve, and which way two of the card moves travel.
//
// They were six fields on Overview, all six assigned in one place at the top of
// updateAnimation and read from six others. Keeping them on the coordinator meant every
// reader of a duration also depended on the coordinator, and there was no way to ask "what
// does the glide cost" without the whole object.
//
// resolve() is the ONLY writer. Config is read here rather than at each use site because the
// leaves are not always the same: the glide follows the leaf the view chose, while the lift
// is fixed. Reading at the point of use is the rule for most keys (AGENTS.md 2.16); this is
// the exception, and it is one call instead of six.
namespace gloview::anim {

class Recipes {
  public:
    Cfg glide;
    Cfg entry;
    Cfg ghost;
    Cfg lift;
    Move enterStyle{Travel::Slide, Axis::Horizontal};
    Move exitStyle{Travel::Slide, Axis::Horizontal};

    // `glideLeaf`, `entryLeaf` and `ghostLeaf` are which behavior the run is currently
    // playing -- they change mid-run, which is why they are passed in rather than read.
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
#pragma once

#include "../layout.hpp"
#include "geometry.hpp"

namespace hyprview {

// How a tile is drawn. The three paths -- the grid, the run, the dragged card --
// used to assemble this by hand, so a field set in one path and forgotten in
// another was a difference between them that nothing said so.
struct TileLook {
  LRect box;
  double round = -1.0;   // <0 = look.preview_round
  bool   frame = true;
  bool   contain = false;
  bool   lift = false;
  bool   counter = false;
  bool   button = false; // the ✕; caller owns the GL_BLEND guard
  double tone = 0.0;
};

// The grid: a plain tile -- framed, not contained, no tone, no counter.
[[nodiscard]] inline TileLook gridLook(const LRect &box, int previewRound, bool button) {
  TileLook l;
  l.box = box;
  l.round = render::clampRound(previewRound, box.w, box.h);
  l.button = button;
  return l;
}

// The run: the centred card is framed and carries the counter; the others are
// contained and dimmed by their depth.
[[nodiscard]] inline TileLook runLook(const LRect &box, int depth, double tone,
                                      bool button, int previewRound) {
  TileLook l;
  l.box = box;
  l.round = render::clampRound(previewRound, box.w, box.h);
  l.frame = depth == 0;
  l.contain = true;
  l.tone = tone;
  l.button = button;
  l.counter = depth == 0;
  return l;
}

// The dragged card: lifted, and nothing else.
[[nodiscard]] inline TileLook dragLook(const LRect &box, int previewRound) {
  TileLook l;
  l.box = box;
  l.round = render::clampRound(previewRound, box.w, box.h);
  l.lift = true;
  return l;
}

} // namespace hyprview

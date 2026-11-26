#pragma once

#include "../layout.hpp"
#include "geometry.hpp"

namespace hyprview {

// How a tile is drawn. The grid, the run and the dragged card assemble it the same way, so a
// field set in one path and forgotten in another is a difference nothing else reports.
struct TileLook {
  LRect box;
  double round = -1.0;   // <0 = look.preview_round
  bool   frame = true;
  bool   ring = true;     // the hover/selection ring may draw here
  bool   contain = false;
  bool   lift = false;
  bool   counter = false;
  bool   button = false; // the ✕; caller owns the GL_BLEND guard
  bool   surface = false; // the plate under the card
  bool   push = true;      // the counter gives way to a card
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
[[nodiscard]] inline TileLook runLook(const LRect &box, bool centred, double tone,
                                      bool button, int previewRound) {
  TileLook l;
  l.box = box;
  l.round = render::clampRound(previewRound, box.w, box.h);
  l.frame = true;   // every depth has an outline
  l.ring = centred; // the focus ring is the centred card's
  l.contain = true;
  l.tone = tone;
  l.button = button;
  l.counter = centred;
  l.surface = true; // the run's cards are frames, and the frame has a plate
  l.push = false;    // a counter on the centred card stays where the run put it
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

}

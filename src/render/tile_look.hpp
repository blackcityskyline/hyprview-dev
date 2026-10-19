#pragma once

#include "../layout.hpp"

namespace hyprview {

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

} // namespace hyprview

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "../anim_types.hpp"
#include "../clock/clocks.hpp"
#include "../layout.hpp"
#include "drag.hpp"
#include "lifecycle.hpp"
#include "scroll.hpp"

namespace gloview::model {

struct Tile {
  PHLWINDOWREF win;
  LRect natural;
  LRect target;
  bool parked = false;
  std::string labelText;
  double landed = 1.0; // 0 = still coming in; ghosts reverse it
  SP<Render::ITexture> label;
};

struct StripWin {
  PHLWINDOWREF win;
  LRect rel;
};

struct StripItem {
  enum class Kind : int { Ws, Plus, All };
  PHLWORKSPACEREF ws;
  int id = 0;
  bool active = false;
  Kind kind = Kind::Ws;
  // id precedes its PHLWORKSPACE object
  bool virtualWs = false;
  LRect card;
  std::vector<StripWin> wins;
  SP<Render::ITexture> label;
};

struct Ghost {
  PHLWINDOWREF win;
  LRect box;
};

// `p` accrues per FRAME, not wall-clock
struct WinPulse {
  PHLWINDOWREF w;
  double p = 0.0;
  std::chrono::steady_clock::time_point last;
};

// The plugin's instantiation of the drag FSM. The template is on the window handle so the
// FSM itself stays free of hyprland/ and reachable from tests/drag_check.cpp.
using Drag = DragT<PHLWINDOWREF>;

// The movement a flight plays. Same type as the workspace enter: they were two enums
// with the same members under different names, which is how `fade` ended up in both
// vocabularies and in neither branch.
using Move = anim::Move;

#include <chrono>

struct SwapFX {
  PHLWINDOWREF win;
  LRect from;
  // Which surface this flight belongs to. A window in the all-workspaces view is drawn
  // twice at once -- a grid card and a strip thumbnail -- and one window can have TWO
  // live flights, one per surface, because the two are not the same gesture: the one the
  // user dragged leaves the cursor, the other slides from where it already was.
  bool        toStrip = false;
  Move        style{};
  double ms = 320;
  std::string curve = "easeinout";
  anim::Tween clock;
  // Set at creation so the ANIM|begin record pairs with the ANIM|end below. 0 =
  // tracing was off, or the flight was rejected for having nowhere to land.
  int         traceSeq = 0;
  std::chrono::steady_clock::time_point traceStart{};
};

}

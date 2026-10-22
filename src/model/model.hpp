#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "../anim_types.hpp"
#include "subject.hpp"
#include "../clock/clocks.hpp"

namespace Render {
// Declared here, not borrowed: a header complete by include order alone breaks when it changes.
class ITexture;
}
#include "../layout.hpp"
#include "drag.hpp"
#include "effects.hpp"
#include "lifecycle.hpp"
#include "scroll.hpp"

namespace hyprview::model {

struct Tile {
  // A subject, not a window handle: a tab has none.
  SubjectRef subj;
  LRect natural;
  LRect home;
  int workspace = 0;  // the desktop view groups the cards by it
  LRect target;
  bool parked = false;
  std::string labelText;
  double landed = 1.0; // 0 = still coming in; ghosts reverse it
  SP<Render::ITexture> label;
  SP<Render::ITexture> countLabel;
};

struct StripWin {
  // Resolution goes through the source, which owns the references.
  SubjectRef subj;
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

// Keeps the FSM free of hyprland/ and reachable from tests/drag_check.cpp; subject, not window.
using Drag = DragT<SubjectRef>;

using Move = anim::Move;

#include <chrono>

}

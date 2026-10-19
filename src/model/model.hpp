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
} // namespace Render
#include "../layout.hpp"
#include "drag.hpp"
#include "lifecycle.hpp"
#include "scroll.hpp"

namespace hyprview::model {

struct Tile {
  // A subject, not a window handle -- a tab has none. The identity carries no refcount:
  // resolution goes through the source that owns it, never through a cast.
  SubjectRef subj;
  LRect natural;
  LRect target;
  bool parked = false;
  std::string labelText;
  double landed = 1.0; // 0 = still coming in; ghosts reverse it
  SP<Render::ITexture> label;
  // Captured at build time: paint only reads; rendering it there would allocate mid-pass.
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

struct Ghost {
  SubjectRef subj;
  LRect box;
};

// `p` accrues per FRAME, not wall-clock
struct WinPulse {
  SubjectRef subj;
  double p = 0.0;
  std::chrono::steady_clock::time_point last;
};

// Keeps the FSM free of hyprland/ and reachable from tests/drag_check.cpp; subject, not window.
using Drag = DragT<SubjectRef>;

using Move = anim::Move;

#include <chrono>

struct SwapFX {
  SubjectRef subj;
  LRect from;
  // One live flight per surface: a window in the all-workspaces view is drawn twice at once.
  bool        toStrip = false;
  Move        style{};
  double ms = 320;
  std::string curve = "easeinout";
  anim::Tween clock;
  // Pairs ANIM|begin with ANIM|end; 0 = tracing off, or the flight had nowhere to land.
  int         traceSeq = 0;
  std::chrono::steady_clock::time_point traceStart{};
};

}

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "../anim_types.hpp"
#include "subject.hpp"
#include "../clock/clocks.hpp"

namespace Render {
// Declared here rather than borrowed from overview.hpp: a header complete only because some
// OTHER header was included first breaks the moment the include order changes.
class ITexture;
} // namespace Render
#include "../layout.hpp"
#include "drag.hpp"
#include "lifecycle.hpp"
#include "scroll.hpp"

namespace gloview::model {

struct Tile {
  // What this tile stands for: a window, or a tab inside one. NOT a window handle -- a tab
  // has none. The identity carries no refcount: resolving one goes through the source that
  // holds the ownership, never through a cast, because an address whose owner has been
  // released is a use-after-free that reads exactly like working code.
  SubjectRef subj;
  LRect natural;
  LRect target;
  bool parked = false;
  std::string labelText;
  double landed = 1.0; // 0 = still coming in; ghosts reverse it
  SP<Render::ITexture> label;
  // The carousel's "n / total", captured at build time for the same reason `label` is:
  // paint only reads, and rendering it there allocated a texture mid-pass.
  SP<Render::ITexture> countLabel;
};

struct StripWin {
  // A subject, not a window: the strip shows whatever the run is showing, and a tab has no
  // window to hold. Resolution goes through the source, which owns the references.
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

// The plugin's instantiation of the drag FSM. The template is on the window handle so the
// FSM itself stays free of hyprland/ and reachable from tests/drag_check.cpp. Subject, not
// window: a drag that could only ever carry a window could not carry a tab.
using Drag = DragT<SubjectRef>;

// The movement a flight plays. Same type as the workspace enter: two enums with the same
// members under different names put `fade` in both vocabularies and in neither branch.
using Move = anim::Move;

#include <chrono>

struct SwapFX {
  SubjectRef subj;
  LRect from;
  // Which surface this flight belongs to. A window in the all-workspaces view is drawn
  // twice at once -- a grid card and a strip thumbnail -- and can have one live flight per
  // surface, because the two are not the same gesture.
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

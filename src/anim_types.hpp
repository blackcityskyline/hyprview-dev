#pragma once

// The animation registry's value types, and nothing else. No hyprland/ include: there is no
// libhyprland, so a header pulling one in cannot be reached from a test, and that is what
// makes src/anim_resolve.cpp linkable alone. A change here is a type, not a value.

#include <cstddef>
#include <string_view>
#include <cstdint>
#include <optional>
#include <string>

namespace gloview::anim {

// Resolved configuration of one registered animation.
struct Cfg {
  bool        on = false;
  double      ms = 0.0;
  std::string curve;
  std::string style;
};

// Imperative override layered over the registered keys (gloview.animation({...})).
struct Rule {
  std::optional<bool>        enabled;
  std::optional<double>      speed;
  std::optional<int>         ms;
  std::optional<std::string> curve;
  std::optional<std::string> style;
};

// The values cfg() reads, lifted out of the config handles so resolve() touches nothing but
// plain C++ and the whole precedence chain is testable headless.
struct Inputs {
  bool        globalEnabled = true;
  bool        enabled = true;         // the leaf's own _enabled
  bool        hasMs = false;          // _ms was set, vs "not set, use the table base"
  int         ms = 0;
  double      speed = 1.0;
  std::string curve;                  // _curve, empty when unset
  const Rule *rule = nullptr;         // gloview.animation{...}, may be null
};

// Precedence: Lua rule > config key > table default. Switched off resolves to 1ms, not to
// its duration -- a clock still has to tick, it just must not move anything.
[[nodiscard]] Cfg resolve(const Inputs &in, int baseMs, const char *curveDefault);

// Two questions the handles cannot answer together: `registered` (does the handle exist)
// and `value` (what it reads). Both callers used to ask only the value, so
// `pulse_enabled = 0` read as not-registered and the disabled animation ran. Unregistered
// defaults to on.
[[nodiscard]] constexpr bool switchOn(bool registered, int value) {
  return !registered || value != 0;
}

// Same split as resolve(), for the same reason. hasValue is what the handle said:
// registered-but-unset falls back to the table default, like every other key here.
[[nodiscard]] double resolveParam(bool hasValue, double value, double fallback);

// How an animation is shaped rather than how long it runs. Resolved live, like the rest.
enum class Param : std::uint8_t {
  Stagger,        // entry ladder: spread of the tiles over the window
  StaggerPerTile, // ... per extra tile, capped by Stagger
  Collapse,       // carousel: fraction of the exit spent shrinking the unfocused cards
  PulseScale,     // pulse ring: how far past the tile edge it reaches
  LiftScale,      // lift: how much the picked-up tile grows
  LiftRise,       // lift: how far it rises, px
  PopScale,        // pop: how much a tile is SMALLER at the start of its scale-in
  ShadowOffset,    // tile shadow offset at rest, px
  LiftShadow,      // tile shadow offset while lifted, px
  Count
};

// Key and default tables beside the enum they are indexed by, both sized by it: a Param
// added without a row is a compile error here and an assertion in the test, not a silently
// wrong slot at runtime. They lived in anim.cpp, where no test could reach them.
inline constexpr std::size_t kParamCount = static_cast<std::size_t>(Param::Count);

inline constexpr const char *const kParamKeys[kParamCount] = {
    "appear_stagger", "appear_stagger_per_tile", "carousel_step_collapse",
    "pulse_ring_scale", "lift_scale", "lift_rise",
    // Scale-in depth and the two tile shadow offsets, which were literals in the render
    // code. The shadow radius was already configurable; the offset was not.
    "appear_pop_scale", "shadow_offset", "lift_shadow_offset"};

inline constexpr double kParamDefaults[kParamCount] = {0.08, 0.015, 0.5, 0.10,
                                                       0.07, 5.0, 0.15, 16.0, 30.0};

// Legal style names, in one place, for ws_enter_style, ws_exit_style, grid_swap_style and
// strip_swap_style. They were string comparisons spread over clocks.cpp and actions.cpp, so
// the set existed only as those branches and a forgotten one was silently ignored.
// `horizontal` stays because it is in every existing config and always resolved to
// slide-horizontal -- by a fall-through that happened to be right.
inline constexpr const char *const kEnterStyles[] = {"slide", "slidevert", "fade", "pop"};

[[nodiscard]] constexpr bool isEnterStyle(std::string_view s) {
  for (const char *k : kEnterStyles)
    if (s == k)
      return true;
  return false;
}

// ws_enter_style / ws_exit_style, resolved once instead of at every comparison: the same
// disease kEnterStyles cures, one layer up. "pop" is an enter-only style (scales the tile in)
// and names fall through to it, so it is a member rather than a fallthrough -- otherwise
// "which names does exit answer to" is unanswerable. One layer of matching quotes is
// removed here: the tokenizer splits on spaces only, so a meant-empty value arrives as the
// two characters "" and num() on those is 0, which made an animation one frame long.
[[nodiscard]] constexpr std::string_view stripQuotes(std::string_view v) {
  return v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()
           ? v.substr(1, v.size() - 2)
           : v;
}

// What a card does on its way from A to B. Unknown names resolve to Slide, the kRows default
// and the value the README documents. It used to be two enums with the same four members under
// different names, and the duplication had already cost a dead key: `fade` was in both
// vocabularies and NEITHER branch drew it. A name in a list no branch honours is worse than an
// absent one, because it reads as a setting.
//
// Spring is deliberately NOT a member: it is stiffness and damping with no end, so it cannot
// live in the same `_ms` scale without lying about what it is (docs/ROADMAP.md).
enum class Travel : int { Slide, Pop, Fade };

// Which way a Slide travels. `slidevert` and `horizontal` are the MANUAL spellings -- they name
// an axis, not a travel, which is why they read as a style and are not one.
enum class Axis : int { Horizontal, Vertical };

struct Move {
  Travel travel = Travel::Slide;
  Axis   axis   = Axis::Horizontal;

  [[nodiscard]] constexpr bool slides() const { return travel == Travel::Slide; }

  [[nodiscard]] constexpr bool equal(const Move &o) const {
    return travel == o.travel && axis == o.axis;
  }
  friend constexpr bool operator==(const Move &, const Move &) = default;
};

// Short spellings of slide-vertical and slide-horizontal, both kept because they are in
// existing configs. `slide-vertical` is canonical and what moveName emits, so a trace record
// parses back.
[[nodiscard]] constexpr Move parseMove(std::string_view s) {
  if (s == "fade")
    return Move{Travel::Fade, Axis::Horizontal};
  if (s == "pop")
    return Move{Travel::Pop, Axis::Horizontal};
  const bool vert = s == "slidevert" || s == "slide-vertical";
  return Move{Travel::Slide, vert ? Axis::Vertical : Axis::Horizontal};
}

// The canonical spellings: what parseMove emits and moveName reads back. Aliases above resolve
// into these, so a user's config keeps working without two names for one thing.
inline constexpr const char *const kMoveNames[] = {"slide-horizontal", "slide-vertical",
                                                   "fade", "pop"};

// For the trace line and the round-trip check: the enum has to name itself, or "every declared
// name parses" is not checkable.
[[nodiscard]] constexpr const char *travelName(Travel t) {
  return t == Travel::Fade ? "fade" : t == Travel::Pop ? "pop" : "slide";
}

[[nodiscard]] constexpr const char *axisName(Axis a) {
  return a == Axis::Vertical ? "vertical" : "horizontal";
}

// What a trace record says about a move, in a spelling parseMove reads back. The axis is only
// spelled for a Slide, where it is half the decision. A dash rather than a space or slash:
// the grammar is key=value with no spaces and '|' as separator. Round-tripping through
// parseMove is the point -- a record nobody can feed back in is nobody's query.
[[nodiscard]] inline std::string moveName(const Move &m) {
  return m.slides() ? std::string(travelName(m.travel)) + "-" + axisName(m.axis)
                    : std::string(travelName(m.travel));
}

// The entry stagger, as pure arithmetic. staggered() in clocks.cpp reads the spread from
// anim::param() and is unreachable from a test, so this is the part under test with the spread
// handed in and capped at 1 inside. n <= 1 returns base: a single tile cannot be staggered
// against anything, and the staggered tile is exempt from the ladder by construction
// (clocks.cpp).
[[nodiscard]] double stagger(double base, int i, int n, double spread);

// Which leaf drives the master clock, and whether the backdrop rides it. These two decisions
// were written out three times in clocks.cpp -- eased(), animDuration() and blurAlpha() each
// re-derived them from m_opening and m_view.jumpMode, and nothing checked they agreed; they
// did once drift, which is what jump_in/jump_out being separate leaves prevents.
//
// jumpMode means the overview was closed by a digit key, so the window keeps flying and the
// clock carrying the close has to keep carrying it. The backdrop rule is AGENTS.md's: with
// windows present it never fades on the way out, and on an empty workspace there is nothing to
// cover so it does.
enum class MasterLeaf : std::uint8_t { Open, Close, JumpOut };

[[nodiscard]] constexpr MasterLeaf masterLeaf(bool jumpMode, bool opening) {
  if (jumpMode && !opening)
    return MasterLeaf::JumpOut;
  return opening ? MasterLeaf::Open : MasterLeaf::Close;
}

// true = the backdrop fades with the clock; false = it holds at full while the real windows'
// own blur covers the exit.
[[nodiscard]] constexpr bool backdropFades(bool opening, bool landingEmpty) {
  return opening || landingEmpty;
}

// What the toggle key means, and where a card goes when the overlay leaves. Both were
// implicit and went wrong the same way: opened on an empty workspace, switched to
// all-workspaces, then Escape, Enter and the toggle all ended in Overview::close(), and each
// spent the exit flying a card toward a tile box on a workspace that held nothing, dropping it
// in one frame -- a tiling motion for a window that was never going to be there. Toggle
// therefore means "take me to what I am looking at" while the overview is up and only opens
// when it is down; Enter already meant that.
enum class DismissIntent : std::uint8_t { Open, Cancel, Commit };

// False on an empty workspace. Committing nothing is cancelling, and saying so here keeps it
// from becoming an activateSelection() that quietly closes.
[[nodiscard]] constexpr DismissIntent toggleIntent(bool overviewOpen,
                                                  bool haveFocusableTile) {
  if (!overviewOpen)
    return DismissIntent::Open;
  return haveFocusableTile ? DismissIntent::Commit : DismissIntent::Cancel;
}

// Home = the window belongs to the workspace this close lands on, so its natural box is where
// it is genuinely going. InPlace = it does not, and animating toward a rectangle on a
// workspace that is not coming is the lie above.
enum class TileExit : std::uint8_t { Home, InPlace };

[[nodiscard]] constexpr TileExit tileExit(bool windowOnLandingWs) {
  return windowOnLandingWs ? TileExit::Home : TileExit::InPlace;
}

// THE deck's hide motion, and the one rule that says who plays it. The carousel has always
// shrunk every non-centred card toward its own centre as the close ran out; a grid never had
// it, which was the whole of the bug -- cancelling out of an all-workspaces deck whose windows
// live on other workspaces left the grid with nothing to play.
//
// One predicate so both layouts ask it: a card hides when it cannot land (nothing is being
// committed, or its window is not on the workspace we land on) or when it is not the focused
// card in the carousel. It lands only when both are false -- exactly the one card being
// committed to, in the one layout that can do it.
[[nodiscard]] constexpr bool cardHides(int carouselDepth, bool canLand) {
  return !canLand || carouselDepth != 0;
}

// shrink toward the card's own centre. out runs 0 -> 1 across the close; collapse is the
// fraction of it the shrink occupies, so it finishes early while the backdrop still holds.
// collapse <= 0 would divide by zero and produce a NaN box, which the compositor draws.
[[nodiscard]] constexpr double hideShrink(double out, double collapse) {
  const double c = collapse > 1e-6 ? collapse : 1e-6;
  const double k = 1.0 - out / c;
  return k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
}

} // namespace gloview::anim
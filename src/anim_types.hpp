#pragma once

// The animation registry's value types, and nothing else. No hyprland/ include: a header
// pulling one in is unreachable from a test, which is what makes anim_resolve.cpp linkable.

#include <cstddef>
#include <string_view>
#include <cstdint>
#include <optional>
#include <string>

namespace gloview::anim {

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
  std::string curve;                  // _curve
  const Rule *rule = nullptr;         // gloview.animation{...}, may be null
};

// Precedence: Lua rule > config key > table default. Switched off resolves to 1ms, not to
// its duration -- a clock still has to tick, it just must not move anything.
[[nodiscard]] Cfg resolve(const Inputs &in, int baseMs, const char *curveDefault);

// `registered` and `value` are two questions the handles cannot answer together: asking only
// the value makes pulse_enabled=0 read as not-registered. Unregistered defaults to on.
[[nodiscard]] constexpr bool switchOn(bool registered, int value) {
  return !registered || value != 0;
}

// The same split as resolve(): registered-but-unset falls back to the table default.
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

// Key and default tables beside the enum, both sized by it: a Param added without a row is
// a compile error here and an assertion in the test, not a silent wrong slot.
inline constexpr std::size_t kParamCount = static_cast<std::size_t>(Param::Count);

inline constexpr const char *const kParamKeys[kParamCount] = {
    "appear_stagger", "appear_stagger_per_tile", "carousel_step_collapse",
    "pulse_ring_scale", "lift_scale", "lift_rise",
    "appear_pop_scale", "shadow_offset", "lift_shadow_offset"};

inline constexpr double kParamDefaults[kParamCount] = {0.08, 0.015, 0.5, 0.10,
                                                       0.07, 5.0, 0.15, 16.0, 30.0};

// Legal style names, in one place, for ws_enter_style, ws_exit_style, grid_swap_style and
// strip_swap_style: a forgotten one must not be silently ignored.
inline constexpr const char *const kEnterStyles[] = {"slide", "slidevert", "fade", "pop"};

[[nodiscard]] constexpr bool isEnterStyle(std::string_view s) {
  for (const char *k : kEnterStyles)
    if (s == k)
      return true;
  return false;
}

// One layer of matching quotes is removed here: the tokenizer splits on spaces only, so a
// meant-empty value arrives as the two characters "" and num() on those is 0.
[[nodiscard]] constexpr std::string_view stripQuotes(std::string_view v) {
  return v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()
           ? v.substr(1, v.size() - 2)
           : v;
}

// What a card does from A to B; unknown names resolve to Slide, the kRows default.
// Spring is deliberately NOT a member: stiffness and damping with no end cannot live on
// the same _ms scale.
enum class Travel : int { Slide, Pop, Fade };

// The manual spellings name an axis, not a travel: they read as a style and are not one.
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

// Short spellings, both kept because they are in existing configs. `slide-vertical` is
// canonical and what moveName emits, so a trace record parses back.
[[nodiscard]] constexpr Move parseMove(std::string_view s) {
  if (s == "fade")
    return Move{Travel::Fade, Axis::Horizontal};
  if (s == "pop")
    return Move{Travel::Pop, Axis::Horizontal};
  const bool vert = s == "slidevert" || s == "slide-vertical";
  return Move{Travel::Slide, vert ? Axis::Vertical : Axis::Horizontal};
}

// What parseMove emits and moveName reads back; the aliases above resolve into these.
inline constexpr const char *const kMoveNames[] = {"slide-horizontal", "slide-vertical",
                                                   "fade", "pop"};

// The spelling the round-trip check feeds back into parseMove.
[[nodiscard]] constexpr const char *travelName(Travel t) {
  return t == Travel::Fade ? "fade" : t == Travel::Pop ? "pop" : "slide";
}

[[nodiscard]] constexpr const char *axisName(Axis a) {
  return a == Axis::Vertical ? "vertical" : "horizontal";
}

// What a trace record says about a move, in a spelling parseMove reads back; the axis is
// only spelled for a Slide. A dash, not a space or slash: key=value, no spaces, '|' separates.
[[nodiscard]] inline std::string moveName(const Move &m) {
  return m.slides() ? std::string(travelName(m.travel)) + "-" + axisName(m.axis)
                    : std::string(travelName(m.travel));
}

// The entry stagger as pure arithmetic, spread handed in (staggered() in clocks.cpp reads
// it from anim::param(), unreachable from a test). n <= 1 returns base.
[[nodiscard]] double stagger(double base, int i, int n, double spread);

// Which leaf drives the master clock. jumpMode means the overview was closed by a digit
// key: the window keeps flying, and the clock carrying the close has to keep carrying it.
enum class MasterLeaf : std::uint8_t { Open, Close, JumpOut };

[[nodiscard]] constexpr MasterLeaf masterLeaf(bool jumpMode, bool opening) {
  if (jumpMode && !opening)
    return MasterLeaf::JumpOut;
  return opening ? MasterLeaf::Open : MasterLeaf::Close;
}

// true = the backdrop fades with the clock; false = it holds at full while the real
// windows' own blur covers the exit (AGENTS.md: never fade out with windows present).
[[nodiscard]] constexpr bool backdropFades(bool opening, bool landingEmpty) {
  return opening || landingEmpty;
}

// What the toggle key means: while the overview is up it takes me to what I am looking at
// (Enter's meaning), and it only opens when the overview is down.
enum class DismissIntent : std::uint8_t { Open, Cancel, Commit };

// False on an empty workspace: committing nothing is cancelling, not an activateSelection()
// that quietly closes.
[[nodiscard]] constexpr DismissIntent toggleIntent(bool overviewOpen,
                                                  bool haveFocusableTile) {
  if (!overviewOpen)
    return DismissIntent::Open;
  return haveFocusableTile ? DismissIntent::Commit : DismissIntent::Cancel;
}

// Home = the window belongs to the workspace this close lands on, so its natural box is
// where it is genuinely going. InPlace otherwise: a box on a workspace that is not coming
// is a lie.
enum class TileExit : std::uint8_t { Home, InPlace };

[[nodiscard]] constexpr TileExit tileExit(bool windowOnLandingWs) {
  return windowOnLandingWs ? TileExit::Home : TileExit::InPlace;
}

// One predicate so both layouts ask it: a card hides when it cannot land (nothing is being
// committed, or its window is not on the workspace we land on) or when it is not the
// focused card in the carousel.
[[nodiscard]] constexpr bool cardHides(int carouselDepth, bool canLand) {
  return !canLand || carouselDepth != 0;
}

// Shrink toward the card's own centre: out runs 0 -> 1 across the close, collapse is the
// fraction it occupies. collapse <= 0 would divide by zero -- a NaN box the compositor draws.
[[nodiscard]] constexpr double hideShrink(double out, double collapse) {
  const double c = collapse > 1e-6 ? collapse : 1e-6;
  const double k = 1.0 - out / c;
  return k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
}

} // namespace gloview::anim

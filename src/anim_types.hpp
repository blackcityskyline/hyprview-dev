#pragma once

// The animation registry's value types, and nothing else. No hyprland/ include: a header pulling
// one in becomes unreachable from a test, and this is what anim_resolve.cpp links.

#include <cstddef>
#include <string_view>
#include <cstdint>
#include <optional>
#include <string>

namespace hyprview::anim {

struct Cfg {
  bool        on = false;
  double      ms = 0.0;
  std::string curve;
  std::string style;
};

// Imperative override layered over the registered keys (hyprview.animation({...})).
struct Rule {
  std::optional<bool>        enabled;
  std::optional<double>      speed;
  std::optional<int>         ms;
  std::optional<std::string> curve;
  std::optional<std::string> style;
};

// Plain values, lifted out of the config handles so resolve() is testable headless.
struct Inputs {
  bool        globalEnabled = true;
  bool        enabled = true;
  bool        hasMs = false;
  int         ms = 0;
  double      speed = 1.0;
  std::string curve;
  const Rule *rule = nullptr;
};

// Precedence: Lua rule > config key > table default. Switched off resolves to 1ms, not to
// its duration -- a clock still has to tick, it just must not move anything.
[[nodiscard]] Cfg resolve(const Inputs &in, int baseMs, const char *curveDefault);

// registered and value are two questions the handles cannot answer together: asking only the
// value makes pulse_enabled=0 read as not-registered. Unregistered defaults to on.
[[nodiscard]] constexpr bool switchOn(bool registered, int value) {
  return !registered || value != 0;
}

[[nodiscard]] double resolveParam(bool hasValue, double value, double fallback);

enum class Param : std::uint8_t {
  Stagger,
  StaggerPerTile,
  Collapse,
  PulseScale,
  LiftScale,
  LiftRise,
  PopScale,
  ShadowOffset,
  LiftShadow,
  Count
};

// Key and default tables beside the enum, both sized by it: a Param added without a row is a
// compile error here, not a silent wrong slot.
inline constexpr std::size_t kParamCount = static_cast<std::size_t>(Param::Count);

inline constexpr const char *const kParamKeys[kParamCount] = {
    "appear_stagger", "appear_stagger_per_tile", "carousel_step_collapse",
    "pulse_ring_scale", "lift_scale", "lift_rise",
    "appear_pop_scale", "shadow_offset", "lift_shadow_offset"};

inline constexpr double kParamDefaults[kParamCount] = {0.08, 0.015, 0.5, 0.10,
                                                       0.07, 5.0, 0.15, 16.0, 30.0};

// Legal style names in one place: a forgotten one must not be silently ignored.
inline constexpr const char *const kEnterStyles[] = {"slide", "slidevert", "fade", "pop"};

[[nodiscard]] constexpr bool isEnterStyle(std::string_view s) {
  for (const char *k : kEnterStyles)
    if (s == k)
      return true;
  return false;
}

// One layer of matching quotes is removed here: the tokenizer splits on spaces only, so a
// meant-empty value arrives as the two characters "".
[[nodiscard]] constexpr std::string_view stripQuotes(std::string_view v) {
  return v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()
           ? v.substr(1, v.size() - 2)
           : v;
}

// What a card does from A to B; unknown names resolve to Slide. Spring is deliberately NOT a
// member: stiffness and damping with no end cannot live on the same _ms scale.
enum class Travel : int { Slide, Pop, Fade };

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

// Short spellings kept because they are in existing configs; slide-vertical is canonical and
// what moveName emits, so a trace record parses back.
[[nodiscard]] constexpr Move parseMove(std::string_view s) {
  if (s == "fade")
    return Move{Travel::Fade, Axis::Horizontal};
  if (s == "pop")
    return Move{Travel::Pop, Axis::Horizontal};
  const bool vert = s == "slidevert" || s == "slide-vertical";
  return Move{Travel::Slide, vert ? Axis::Vertical : Axis::Horizontal};
}

inline constexpr const char *const kMoveNames[] = {"slide-horizontal", "slide-vertical",
                                                   "fade", "pop"};

[[nodiscard]] constexpr const char *travelName(Travel t) {
  return t == Travel::Fade ? "fade" : t == Travel::Pop ? "pop" : "slide";
}

[[nodiscard]] constexpr const char *axisName(Axis a) {
  return a == Axis::Vertical ? "vertical" : "horizontal";
}

// What a trace record says about a move, in a spelling parseMove reads back; a dash, not a
// space or slash -- key=value, no spaces, '|' separates.
[[nodiscard]] inline std::string moveName(const Move &m) {
  return m.slides() ? std::string(travelName(m.travel)) + "-" + axisName(m.axis)
                    : std::string(travelName(m.travel));
}

// Spread is handed in because anim::param() is unreachable from a test; n <= 1 returns base.
[[nodiscard]] double stagger(double base, int i, int n, double spread);

// Which leaf drives the master clock: on a jumpMode close the window keeps flying, and the
// clock carrying the close has to keep carrying it.
enum class MasterLeaf : std::uint8_t { Open, Close, JumpOut };

[[nodiscard]] constexpr MasterLeaf masterLeaf(bool jumpMode, bool opening) {
  if (jumpMode && !opening)
    return MasterLeaf::JumpOut;
  return opening ? MasterLeaf::Open : MasterLeaf::Close;
}

// false = the backdrop holds while the real windows' blur covers the exit (never fade out
// with windows present).
enum class DismissIntent : std::uint8_t { Open, Cancel, Commit };

// False on an empty workspace: committing nothing is cancelling, not an activateSelection().
[[nodiscard]] constexpr DismissIntent toggleIntent(bool overviewOpen,
                                                  bool haveFocusableTile) {
  if (!overviewOpen)
    return DismissIntent::Open;
  return haveFocusableTile ? DismissIntent::Commit : DismissIntent::Cancel;
}

// Home = the window belongs to the workspace this close lands on; InPlace otherwise -- a box
// on a workspace that is not coming is a lie.
enum class TileExit : std::uint8_t { Home, InPlace };

[[nodiscard]] constexpr TileExit tileExit(bool windowOnLandingWs) {
  return windowOnLandingWs ? TileExit::Home : TileExit::InPlace;
}

[[nodiscard]] constexpr bool cardHides(int carouselDepth, bool canLand) {
  return !canLand || carouselDepth != 0;
}

// collapse <= 0 would divide by zero -- a NaN box the compositor draws.
[[nodiscard]] constexpr double hideShrink(double out, double collapse) {
  const double c = collapse > 1e-6 ? collapse : 1e-6;
  const double k = 1.0 - out / c;
  return k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
}

} // namespace hyprview::anim

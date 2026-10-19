#pragma once

// Live handles resolved through the V2 config system; adding one = a handle
// in its domain + a schema row in config.cpp + a README row.

#include <optional>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include "../layout.hpp"


namespace gloview::cfg {

// Runtime overrides, keyed by the registered config key: hl.config accepts only the nested
// form and CConfigValue has no setter, so every getter consults this first. Empty value clears.
namespace override {
inline std::unordered_map<std::string, std::string> values;

inline const std::string *find(const char *key) {
  if (!key)
    return nullptr;
  const auto it = values.find(key);
  return it == values.end() ? nullptr : &it->second;
}
inline void set(const char *key, const std::string &v) { values[key] = v; }
inline bool clear(const char *key) { return values.erase(key) > 0; }

// Every registered key, filled during registerAll: the override map is the thing being
// written to, so on the first set every key would look unknown.
inline std::unordered_map<std::string, char> known;
inline bool isConfigKey(const char *key) {
  return key && known.find(key) != known.end();
}
} // namespace override

// Set at registration from the spec table, which is the only place the key exists.
// A null key means "never registered", and then get() is a plain read.

class Int {
public:
  SP<Config::Values::CIntValue> v;
  int def = 0;
  const char *key = nullptr;
  int get() const {
    if (const std::string *o = override::find(key))
      return static_cast<int>(std::strtol(o->c_str(), nullptr, 10));
    return v ? static_cast<int>(v->value()) : def;
  }
  operator int() const { return get(); }
};

class Float {
public:
  SP<Config::Values::CFloatValue> v;
  float def = 0.f;
  const char *key = nullptr;
  float get() const {
    if (const std::string *o = override::find(key))
      return static_cast<float>(std::strtod(o->c_str(), nullptr));
    return v ? static_cast<float>(v->value()) : def;
  }
  operator float() const { return get(); }
};

class Str {
public:
  SP<Config::Values::CStringValue> v;
  const char *def = "";
  const char *key = nullptr;
  std::string get() const {
    if (const std::string *o = override::find(key))
      return *o;
    return v ? std::string(v->value()) : std::string(def);
  }
  operator std::string() const { return get(); }
};

// Parsed once, re-parsed only when the configured string changes; get(a) multiplies its alpha.
class Color {
public:
  SP<Config::Values::CStringValue> v;
  const char *def = "";
  const char *key = nullptr;
  CHyprColor get(float alphaMul = 1.0F) const;
};

struct grid {
  Int gap;
  Int padding;
  Int padding_top;
  Int padding_bottom;
  Float max_scale;
  Str layout;
};

inline grid grid;

// The run is a separate mechanism laid over whatever `layout` says, not a
// layout of its own — see Overview::layoutTiles for where it takes over.
struct carousel {
  Int enabled;
  Int cards;
  Int card_width;
  Float scale;
  // Wheel paging: notches * speed = cards moved.
  Float scroll_speed;
  // Carousel state when the all-workspaces view opens. The key binding is always
  // a toggle; this only decides where it starts.
  Int on_open;
  // Depth shading of the run: nearest unfocused card's tone, the far one, the multiplier
  // per step deeper.
  Float tone_near;
  Float tone_far;
  Float depth_taper;
};

inline carousel carousel;

struct strip {
  Int height;
  Int offset;
  Int margin;
  Int gap;
  Int card_round;
  // Label strip under a card, how far a card widens to fit its label, the slide-out's share
  // of the strip's thickness, and the ceiling on offset as a fraction of the cross axis.
  Float label_band;
  Float label_extra;
  Float slide_frac;
  Float offset_max;
  Str anchor;
  Str bar_position;
  Str empty_mode;
  Int all_card;
  Int wallpaper;
  // Percent. The band's own alpha comes from here and nowhere else; strip_band_color
  // supplies RGB only.
  Int band_opacity;
};

inline strip strip;

struct look {
  Int preview_round;
  Float preview_round_power;
  Int show_border;
  Int show_focus_border;
  Int border_size;
  Int hover_border_size;
  Int select_border_size;
  Float close_button_size;
  Float drag_size;
  // Drag preview offset from the pointer, so the drop zones stay visible under it, and
  // the vertical lift of a SlideVert swap.
  Float drag_offset_x;
  Float drag_offset_y;
  Float swap_lift;
  // Shadow blur at rest and lifted, the shadow alpha multiplier, title-pill padding and
  // gaps, the close glyph's fill.
  Float shadow_radius;
  Float shadow_radius_lift;
  Float shadow_alpha;
  Float title_pad_x;
  Float title_pad_y;
  Int title_font_size;
  // "bottom" hangs the whole caption off the card's lower edge, "top" off its upper one.
  Str title_position;
  // Corner radius in px. Negative = a capsule (half the height), 0 = square. Capped at half
  // the shorter side, past which it turns inside out.
  Float title_round;
  Float title_gap;
  Float title_margin;
  // The carousel's "n / total" is its own pill under the caption, not a stem: two translucent
  // shapes cannot share a silhouette without a gap or a density step, hence separate knobs.
  Float counter_pad_x;
  Float counter_pad_y;
  Int counter_font_size;
  Float counter_round; // negative = capsule, as the caption's
  // "bottom" puts the counter under the title pill, "top" over it; empty = follow
  // title_position. Order INSIDE the caption only; where it hangs is title_position's business.
  Str counter_position;
  Float counter_gap;
  Float counter_margin;
  Float close_glyph_fill;
  Str close_button_position;
  Str close_button_visibility;
  Str close_trigger;
  Str close_button_icon;
};

inline look look;

struct colors {
  Color border;
  Color hover_border;
  Color select_border;
  Color shadow;
  Color label;
  Color title_pill;
  Color title_label;
  Color counter_pill;    // the n / total pill
  Color counter_label;
  Color drop_hint;
  Color close_button;
  Color close_glyph;
  Color strip_band;
  Color strip_card;
  Color strip_active;
  Color strip_active_border;
  Color strip_hover;
  Color strip_plus;
  Color strip_all;
};

inline colors colors;

struct blur {
  Float strength;
  Int passes;
  Int size;
  Int resolution;
  Color backdrop;
};

inline blur blur;


struct keys {
  Str close;
  Str next_workspace;
  Str prev_workspace;
  Str activate;
  Str close_window;
  Str left, right, up, down;
  Str desktop;
  Str all_workspaces;
  Str workspace;
  Str carousel;
  Str browser;
  Str alt_tab_modifier;
  Int alt_tab_commit_on_release;
};

inline keys keys;

struct behavior {
  Str preview_mode;
  Str cursor_mode;
  Str workspace_key_mode;
  Str new_workspace_mode;
  Int focus_follows_mouse;
  Int scroll_switches_workspace;
  Int passthrough_keys;
  Int exit_on_click;
  // Closing a window by dragging it to the screen edge. The zone is the strip opposite the
  // strip band; disabling it leaves the edge a no-op instead of a trap.
  Int kill_zone;
  Float kill_zone_size;
  Float kill_zone_falloff;
  // How far from the screen edge the drop target reaches, in px.
  Int kill_zone_reach;
  Int click_window_ms;
  Int exit_on_switch;
  Int switch_on_drop;
  Int drag_to_swap;
  Int switch_on_new_workspace;
  Int show_all_workspaces;
  Int show_special;
  // One card per window, one per browser tab, or the window plus its tabs; until the
  // extension reports, every window falls back to a window (model/entries.hpp, checked).
  Str tile_mode;
  Int alttab_advances; // the entry is aimed at the next window
  Int hold_lift_ms;
  Str tile_layout;
};

inline behavior behavior;

struct layer {
  Int hide_top;
  Int hide_overlay;
  Str above_namespaces;
};

inline layer layer;

struct debug {
  Int logs;   // per-frame developer log
  Int trace;  // machine-readable ANIM| records only; see debug/trace.cpp
};

inline debug debug;

void registerAll(HANDLE handle);

}

#include "config.hpp"

#include "../anim.hpp"
#include "../debug/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace hyprview::cfg {

namespace {

// Hex literal: optional whitespace, `0x`/`0X`/`#`, then 6 (RRGGBB) or 8 (AARRGGBB) digits.
uint64_t parseHex(std::string s, uint64_t fallback) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  if (s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s.erase(0, 2);
  else if (!s.empty() && s[0] == '#')
    s.erase(0, 1);
  if (s.size() != 6 && s.size() != 8)
    return fallback;
  for (const char c : s)
    if (!std::isxdigit(static_cast<unsigned char>(c)))
      return fallback;
  try {
    return std::stoull(s, nullptr, 16);
  } catch (...) {
    return fallback;
  }
}

CHyprColor parseColor(std::string s, const char *fallback) {
  const uint64_t v = parseHex(std::move(s), parseHex(fallback, 0xffffffffULL));
  const auto a = static_cast<double>((v >> 24) & 0xFF) / 255.0;
  const auto r = static_cast<double>((v >> 16) & 0xFF) / 255.0;
  const auto g = static_cast<double>((v >> 8) & 0xFF) / 255.0;
  const auto b = static_cast<double>(v & 0xFF) / 255.0;
  return CHyprColor(r, g, b, a);
}

std::unordered_map<const Color *, std::pair<std::string, CHyprColor>>
    g_colorCache;

} // namespace

CHyprColor Color::get(float alphaMul) const {
  // The override is resolved BEFORE the cache lookup: the cache is keyed on the parsed string.
  const std::string *ov = override::find(key);
  const std::string raw = ov ? *ov : (v ? std::string(v->value()) : std::string(def));
  auto &c = g_colorCache[this];
  if (c.first != raw) {
    c.first  = raw;
    c.second = parseColor(raw, def);
  }
  CHyprColor out = c.second;
  out.a *= std::clamp(alphaMul, 0.0F, 1.0F);
  return out;
}

// THE SCHEMA: handle, FULL config key, default. Key MUST be a literal: it is kept as const char*.

namespace {

struct IntSpec {
  Int *h;
  const char *key;
  int def;
};
struct FloatSpec {
  Float *h;
  const char *key;
  float def;
};
struct StrSpec {
  Str *h;
  const char *key;
  const char *def;
};
struct ColorSpec {
  Color *h;
  const char *key;
  const char *def;
};

constexpr IntSpec kInts[] = {
    {&grid.gap, "plugin:hyprview:gap", 34},
    {&grid.padding, "plugin:hyprview:padding", 80},
    {&grid.padding_top, "plugin:hyprview:padding_top", 40},
    {&grid.padding_bottom, "plugin:hyprview:padding_bottom", 70},
    {&behavior.kill_zone_reach, "plugin:hyprview:kill_zone_reach", 160},
    {&look.title_font_size, "plugin:hyprview:title_font_size", 12},
    {&look.counter_font_size, "plugin:hyprview:counter_font_size", 12},
    {&carousel.enabled, "plugin:hyprview:carousel", 0},
    {&carousel.on_open, "plugin:hyprview:carousel_on_open", 1},
    {&carousel.cards, "plugin:hyprview:carousel_cards", 5},
    {&carousel.card_width, "plugin:hyprview:carousel_card_width", 0},
    {&strip.height, "plugin:hyprview:strip_height", 150},
    {&strip.offset, "plugin:hyprview:strip_offset", 0},
    {&strip.margin, "plugin:hyprview:strip_margin", 22},
    {&strip.gap, "plugin:hyprview:strip_gap", 18},
    {&strip.card_round, "plugin:hyprview:strip_card_round", 10},
    {&strip.all_card, "plugin:hyprview:strip_all_card", 0},
    {&strip.wallpaper, "plugin:hyprview:strip_wallpaper", 1},
    {&strip.band_opacity, "plugin:hyprview:strip_band_opacity", 70},
    {&look.preview_round, "plugin:hyprview:preview_round", 12},
    {&look.show_border, "plugin:hyprview:show_border", 0},
    {&look.show_focus_border, "plugin:hyprview:show_focus_border", 1},
    {&look.border_size, "plugin:hyprview:border_size", 2},
    {&look.hover_border_size, "plugin:hyprview:hover_border_size", 3},
    {&look.select_border_size, "plugin:hyprview:select_border_size", 3},
    {&blur.passes, "plugin:hyprview:blur_passes", 3},
    {&blur.size, "plugin:hyprview:blur_size", 8},
    {&blur.resolution, "plugin:hyprview:blur_resolution", 4},
    {&keys.alt_tab_commit_on_release, "plugin:hyprview:alt_tab_commit_on_release", 1},
    {&behavior.focus_follows_mouse, "plugin:hyprview:focus_follows_mouse", 1},
    {&behavior.scroll_switches_workspace, "plugin:hyprview:scroll_switches_workspace", 1},
    {&behavior.passthrough_keys, "plugin:hyprview:passthrough_keys", 1},
    {&behavior.exit_on_click, "plugin:hyprview:exit_on_click", 1},
    {&behavior.kill_zone, "plugin:hyprview:kill_zone", 1},
    {&behavior.click_window_ms, "plugin:hyprview:click_window_ms", 400},
    {&behavior.exit_on_switch, "plugin:hyprview:exit_on_switch", 0},
    {&behavior.switch_on_drop, "plugin:hyprview:switch_on_drop", 0},
    {&behavior.drag_to_swap, "plugin:hyprview:drag_to_swap", 1},
    {&behavior.switch_on_new_workspace, "plugin:hyprview:switch_on_new_workspace", 1},
    {&behavior.show_all_workspaces, "plugin:hyprview:show_all_workspaces", 0},
    {&behavior.show_special, "plugin:hyprview:show_special", 0},
    {&behavior.show_discarded_tabs, "plugin:hyprview:show_discarded_tabs", 0},
    {&behavior.alttab_advances, "plugin:hyprview:alttab_advances", 0},
    {&behavior.hold_lift_ms, "plugin:hyprview:hold_lift_ms", 400},
    {&layer.hide_top, "plugin:hyprview:hide_top_layers", 0},
    {&layer.hide_overlay, "plugin:hyprview:hide_overlay_layers", 0},
    {&debug.logs, "plugin:hyprview:debug_logs", 0},
    // Separate from debug_logs: a trace run wants every event and none of the per-frame spam.
    {&debug.trace, "plugin:hyprview:debug_trace", 0},
};

constexpr FloatSpec kFloats[] = {
    {&carousel.tone_near, "plugin:hyprview:carousel_tone_near", 0.125F},
    {&carousel.tone_far, "plugin:hyprview:carousel_tone_far", 0.25F},
    {&carousel.depth_taper, "plugin:hyprview:carousel_depth_taper", 0.8F},
    {&look.shadow_radius, "plugin:hyprview:shadow_radius", 6.0},
    {&look.shadow_radius_lift, "plugin:hyprview:shadow_radius_lift", 14.0},
    {&look.shadow_alpha, "plugin:hyprview:shadow_alpha", 0.18},
    {&look.title_pad_x, "plugin:hyprview:title_pad_x", 11.0},
    {&look.title_pad_y, "plugin:hyprview:title_pad_y", 5.0},
    {&look.title_gap, "plugin:hyprview:title_gap", 10.0},
    {&look.title_margin, "plugin:hyprview:title_margin", 6.0},
    {&look.title_round, "plugin:hyprview:title_round", -1.0},
    {&look.counter_pad_x, "plugin:hyprview:counter_pad_x", 11.0},
    {&look.counter_pad_y, "plugin:hyprview:counter_pad_y", 5.0},
    {&look.counter_gap, "plugin:hyprview:counter_gap", 8.0},
    {&look.counter_margin, "plugin:hyprview:counter_margin", 6.0},
    {&look.counter_round, "plugin:hyprview:counter_round", -1.0},
    {&look.close_glyph_fill, "plugin:hyprview:close_glyph_fill", 0.62},
    {&strip.label_band, "plugin:hyprview:strip_label_band", 26.0},
    {&strip.label_extra, "plugin:hyprview:strip_label_extra", 24.0},
    {&strip.slide_frac, "plugin:hyprview:strip_slide_frac", 0.55},
    {&strip.offset_max, "plugin:hyprview:strip_offset_max", 0.42},

    {&behavior.kill_zone_size, "plugin:hyprview:kill_zone_size", 40.0},
    {&behavior.kill_zone_falloff, "plugin:hyprview:kill_zone_falloff", 150.0},
    {&grid.max_scale, "plugin:hyprview:max_scale", 1.0F},
    {&look.preview_round_power, "plugin:hyprview:preview_round_power", 2.0F},
    {&blur.strength, "plugin:hyprview:blur", 1.0F},
    {&look.close_button_size, "plugin:hyprview:close_button_size", 1.0F},
    {&look.drag_size, "plugin:hyprview:drag_size", 0.55F},
    {&look.drag_offset_x, "plugin:hyprview:drag_offset_x", 46.0F},
    {&look.drag_offset_y, "plugin:hyprview:drag_offset_y", 64.0F},
    {&look.swap_lift, "plugin:hyprview:swap_lift", 12.0F},
    {&carousel.scale, "plugin:hyprview:carousel_scale", 1.0F},
    {&carousel.scroll_speed, "plugin:hyprview:carousel_scroll_speed", 1.0F},
};

constexpr StrSpec kStrs[] = {
    {&grid.layout, "plugin:hyprview:layout", "rows"},
    {&behavior.tile_mode, "plugin:hyprview:tile_mode", "windows"},
    {&strip.anchor, "plugin:hyprview:anchor", ""},
    {&strip.bar_position, "plugin:hyprview:bar_position", "top"}, // deprecated alias
    {&strip.empty_mode, "plugin:hyprview:strip_empty_mode", "show"},
    {&look.close_button_position, "plugin:hyprview:close_button_position", "top-right"},
    {&look.title_position, "plugin:hyprview:title_position", "bottom"},
    {&look.counter_position, "plugin:hyprview:counter_position", ""},
    {&look.close_button_visibility, "plugin:hyprview:close_button_visibility", "shift"},
    {&look.close_trigger, "plugin:hyprview:close_trigger", "button"},
    {&look.close_button_icon, "plugin:hyprview:close_button_icon", "\xe2\x9c\x95"},
    {&keys.close, "plugin:hyprview:key_close", "escape"},
    {&keys.next_workspace, "plugin:hyprview:key_next_workspace", "tab"},
    {&keys.prev_workspace, "plugin:hyprview:key_prev_workspace", "shift+tab"},
    {&keys.activate, "plugin:hyprview:key_activate", "enter"},
    {&keys.close_window, "plugin:hyprview:key_close_window", "d"},
    {&keys.left, "plugin:hyprview:key_left", "left"},
    {&keys.right, "plugin:hyprview:key_right", "right"},
    {&keys.up, "plugin:hyprview:key_up", "up"},
    {&keys.down, "plugin:hyprview:key_down", "down"},
    {&keys.desktop, "plugin:hyprview:key_desktop", "shift"},
    {&keys.all_workspaces, "plugin:hyprview:key_all_workspaces", "a"},
    {&keys.carousel, "plugin:hyprview:key_carousel", "c"},
    {&keys.browser, "plugin:hyprview:key_browser", "b"},
    {&keys.workspace, "plugin:hyprview:key_workspace", "1,2,3,4,5,6,7,8,9,0"},
    {&keys.alt_tab_modifier, "plugin:hyprview:alt_tab_modifier", "alt"},
    {&behavior.preview_mode, "plugin:hyprview:preview_mode", "live"},
    {&behavior.tile_layout,  "plugin:hyprview:tile_layout",  "none"},
    {&behavior.cursor_mode, "plugin:hyprview:cursor_mode", "auto"},
    {&behavior.workspace_key_mode, "plugin:hyprview:key_workspace_mode", "switch"},
    {&behavior.new_workspace_mode, "plugin:hyprview:new_workspace_mode", "fill"},
    {&layer.above_namespaces, "plugin:hyprview:above_namespaces", ""},
};

constexpr ColorSpec kColors[] = {
    {&blur.backdrop, "plugin:hyprview:backdrop_color", "0x73070a10"},
    {&colors.surface, "plugin:hyprview:surface_color", "0x1e1e26"},
    {&colors.border, "plugin:hyprview:border_color", "0x50ffffff"},
    {&colors.close_button, "plugin:hyprview:close_button_color", "0xe6e23b3b"},
    {&colors.close_glyph, "plugin:hyprview:close_glyph_color", "0xffffffff"},
    {&colors.drop_hint, "plugin:hyprview:drop_hint_color", "0x38ffffff"},
    {&colors.hover_border, "plugin:hyprview:hover_border", "0xf0ffffff"},
    {&colors.label, "plugin:hyprview:label_color", "0xf2ffffff"},
    // The text in the two caption pills, separate from label_color and from each other.
    {&colors.title_label, "plugin:hyprview:title_label_color", "0xf2ffffff"},
    {&colors.counter_label, "plugin:hyprview:counter_label_color", "0xf2ffffff"},
    {&colors.select_border, "plugin:hyprview:select_border", "0xf066ccff"},
    {&colors.shadow, "plugin:hyprview:shadow_color", "0x70000000"},
    {&colors.strip_active, "plugin:hyprview:strip_active_color", "0x4d1c2c44"},
    {&colors.strip_active_border, "plugin:hyprview:strip_active_border", "0xf0ffffff"},
    {&colors.strip_all, "plugin:hyprview:strip_all_color", "0xd0eef4ff"},
    {&colors.strip_band, "plugin:hyprview:strip_band_color", "0x24ffffff"},
    {&colors.strip_card, "plugin:hyprview:strip_card_color", "0x3a0e131c"},
    {&colors.strip_hover, "plugin:hyprview:strip_hover_border", "0x80ffffff"},
    {&colors.strip_plus, "plugin:hyprview:strip_plus_color", "0xd0eef4ff"},
    {&colors.title_pill, "plugin:hyprview:title_pill_color", "0xcc11151c"},
    {&colors.counter_pill, "plugin:hyprview:counter_pill_color", "0xcc11151c"},
};

} // namespace

void registerAll(HANDLE handle) {
  // Every handle learns its own key here: the spec tables carry it, the value objects do not.
  for (auto &s : kInts) {
    s.h->v = makeShared<Config::Values::CIntValue>(s.key, "",
                                                     static_cast<Config::INTEGER>(s.def));
    s.h->def = s.def;
    s.h->key = s.key;
    override::known[s.key] = 1;
    HyprlandAPI::addConfigValueV2(handle, s.h->v);
  }
  for (auto &s : kFloats) {
    s.h->v = makeShared<Config::Values::CFloatValue>(s.key, "", s.def);
    s.h->def = s.def;
    s.h->key = s.key;
    override::known[s.key] = 1;
    HyprlandAPI::addConfigValueV2(handle, s.h->v);
  }
  for (auto &s : kStrs) {
    s.h->v = makeShared<Config::Values::CStringValue>(s.key, "", s.def);
    s.h->def = s.def;
    s.h->key = s.key;
    override::known[s.key] = 1;
    HyprlandAPI::addConfigValueV2(handle, s.h->v);
  }
  for (auto &s : kColors) {
    s.h->v = makeShared<Config::Values::CStringValue>(s.key, "", s.def);
    s.h->def = s.def;
    s.h->key = s.key;
    override::known[s.key] = 1;
    HyprlandAPI::addConfigValueV2(handle, s.h->v);
  }
  anim::registerAll(handle);
}

} // namespace hyprview

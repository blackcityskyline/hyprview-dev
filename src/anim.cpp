#include "anim.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "debug/log.hpp"

namespace hyprview::anim {

struct Row {
  const char *name;
  cfg::Int    enabled;
  cfg::Float  speed;
  cfg::Str    curve;
  cfg::Str    style;
  cfg::Int    ms;
  float       base;
  const char *curveDef;
  bool        hasStyle;
  const char *styleDefault;
};

static std::array<Row, static_cast<std::size_t>(Id::Count)> kRows = {{
{"open", {}, {}, {}, {}, {}, 500, "easeout", false, ""},
{"close", {}, {}, {}, {}, {}, 500, "easeout", false, ""},
{"glide", {}, {}, {}, {}, {}, 300, "easeout", false, ""},
{"appear", {}, {}, {}, {}, {}, 250, "easeout", false, ""},
{"card", {}, {}, {}, {}, {}, 250, "back", false, ""},
{"pulse", {}, {}, {}, {}, {}, 180, "back", false, ""},
{"strip_step", {}, {}, {}, {}, {}, 200, "easeinout", false, ""},
{"ws_enter", {}, {}, {}, {}, {}, 250, "easeout", true, "slide"},
{"ws_exit", {}, {}, {}, {}, {}, 250, "easeout", true, "slide"},
{"expo_in", {}, {}, {}, {}, {}, 250, "easeout", false, ""},
{"expo_out", {}, {}, {}, {}, {}, 250, "easeout", false, ""},
{"jump_in", {}, {}, {}, {}, {}, 180, "easeout", false, ""},
{"jump_out", {}, {}, {}, {}, {}, 220, "easeout", false, ""},
{"swap_main", {}, {}, {}, {}, {}, 320, "easeinout", false, ""},
{"swap_partner", {}, {}, {}, {}, {}, 320, "easeinout", false, ""},
  // One animation per ACTION: one _speed must not move a landing, a fly and a raise at once.
{"drop", {}, {}, {}, {}, {}, 400, "easeout", false, ""},
{"swap_fly", {}, {}, {}, {}, {}, 400, "easeout", false, ""},
{"lift", {}, {}, {}, {}, {}, 400, "easeout", false, ""},
{"carousel_step", {}, {}, {}, {}, {}, 300, "easeout", false, ""},
{"new_card", {}, {}, {}, {}, {}, 250, "easeout", false, ""},
}};

static cfg::Int g_globallyEnabled;
static const std::string s_jump = "instant";
static const std::string s_gridSwap = "horizontal", s_stripSwap = "horizontal";
static cfg::Str g_gridSwapStyle, g_stripSwapStyle, g_jumpStyle;

// Shape parameters, in the same order as anim::Param.
static cfg::Float g_params[static_cast<std::size_t>(Param::Count)];
namespace {
std::unordered_map<std::string, Rule> g_rules;

const Row *find(const char *n) {
  if (!n)
    return nullptr;
  for (const auto &r : kRows)
    if (std::strcmp(r.name, n) == 0)
      return &r;
  return nullptr;
}

const Rule *ruleFor(const char *n) {
  const auto it = g_rules.find(n);
  return it == g_rules.end() ? nullptr : &it->second;
}
}

static std::unordered_map<std::string, std::string> g_override;

static const char *overridden(const char *key) {
  const auto it = g_override.find(key);
  return it == g_override.end() ? nullptr : it->second.c_str();
}

static double num(const char *s) { return s ? std::atof(s) : 0.0; }

Cfg cfg(Id id) { return cfg(kRows[static_cast<std::size_t>(id)].name); }

Cfg cfg(const char *n) {
  const Row *r = find(n);
  if (!r)
    return Cfg{};

  Inputs in;
  const std::string k = r->name;
// `.v` is the HANDLE, not the value: switchOn() keeps the two apart (anim_resolve_check).
  in.globalEnabled = switchOn(static_cast<bool>(g_globallyEnabled.v), g_globallyEnabled);
  in.enabled       = switchOn(static_cast<bool>(r->enabled.v), r->enabled.get());
  in.hasMs         = static_cast<bool>(r->ms.v);
  in.ms            = in.hasMs ? r->ms.get() : 0;
  in.speed         = r->speed.v ? static_cast<double>(r->speed.get()) : 1.0;
  in.curve         = static_cast<bool>(r->curve.v) ? r->curve.get() : std::string();
  in.rule          = ruleFor(r->name);

  // A command override beats a config rule; the rule is applied LAST with the field cleared.
  Rule adjusted = in.rule ? *in.rule : Rule{};
  if (in.rule)
    in.rule = &adjusted;
  if (const char *o = overridden((k + "_ms").c_str())) {
    in.hasMs = true;
    in.ms = num(o);
    adjusted.ms.reset();
  }
  if (const char *o = overridden((k + "_speed").c_str())) {
    in.speed = num(o);
    adjusted.speed.reset();
  }
  if (const char *o = overridden((k + "_curve").c_str())) {
    in.curve = o;
    adjusted.curve.reset();
  }
  if (const char *o = overridden((k + "_enabled").c_str())) {
    in.enabled = num(o) != 0;
    adjusted.enabled.reset();
  }

  return resolve(in, r->base, r->curveDef);
}

std::string style(const char *n) {
  // Not rows (no "grid_swap" in kRows): by name here so a Lua style("grid_swap_style") agrees.
  if (std::string(n) == "grid_swap_style")
    return gridSwapStyle();
  if (std::string(n) == "strip_swap_style")
    return stripSwapStyle();
  if (std::string(n) == "jump_style")
    return jumpStyle();
  const Row *r = find(n);
  if (!r || !r->hasStyle)
    return {};
  if (const auto *rule = ruleFor(r->name); rule && rule->style)
    return *rule->style;
  if (const char *o = overridden((std::string(r->name) + "_style").c_str()))
    return o;
  return r->style.v ? r->style.get() : std::string();
}

void setRule(const char *n, const Rule &rule) {
  if (n)
    g_rules[n] = rule;
}

double param(Param p) {
  const auto i = static_cast<std::size_t>(p);
  auto &h = g_params[i];
  return resolveParam(static_cast<bool>(h.v), h.get(), kParamDefaults[i]);
}

// By value: Str::get() returns std::string by value, so a const& return would dangle.
static std::string keyedStyle(const char *key, cfg::Str &h, const std::string &fallback) {
  if (const char *o = overridden(key))
    return o;
  return h.v ? h.get() : fallback;
}

std::string jumpStyle() { return keyedStyle("jump_style", g_jumpStyle, s_jump); }

std::string gridSwapStyle() {
  return keyedStyle("grid_swap_style", g_gridSwapStyle, s_gridSwap);
}

std::string stripSwapStyle() {
  return keyedStyle("strip_swap_style", g_stripSwapStyle, s_stripSwap);
}

// Consulted BEFORE the config value, only when explicitly set (hl.config rejects plugin: keys).
bool setTuning(const std::string &rawKey, const std::string &value) {
  // Keys are stored by SHORT name ("<leaf>_ms"); a bare name is expanded first.
  const std::string full = rawKey.rfind("plugin:", 0) == 0
                               ? rawKey
                               : ("plugin:hyprview:" + rawKey);
  constexpr std::size_t kPrefix = sizeof("plugin:hyprview:") - 1;
  const std::string leaf = full.rfind("plugin:hyprview:", 0) == 0
                               ? full.substr(kPrefix)
                               : rawKey;

  // A bare pair of quotes means CLEAR, not a zero (`set open_ms ""` reaches num() as 0).
  const std::string_view v = stripQuotes(value);
  const bool clearing = v.empty();
  if (leaf == "grid_swap_style" || leaf == "strip_swap_style" ||
      leaf == "jump_style") {
    if (clearing)
      return g_override.erase(leaf) > 0;
    g_override[leaf] = std::string(v);
    return true;
  }
  if (cfg::override::isConfigKey(full.c_str())) {
    if (clearing)
      return cfg::override::clear(full.c_str());
    cfg::override::set(full.c_str(), std::string(v));
    return true;
  }
  const auto suffix = leaf.rfind('_');
  if (suffix == std::string::npos || !find(leaf.substr(0, suffix).c_str()))
    return false;
  // `_style` joins the suffix list only for rows that HAVE a style.
  if (leaf.compare(suffix, 6, "_style") == 0) {
    const Row *r = find(leaf.substr(0, suffix).c_str());
    if (!r || !r->hasStyle)
      return false;
  }
  if (leaf.compare(suffix, 4, "_ms") == 0 || leaf.compare(suffix, 6, "_speed") == 0 ||
      leaf.compare(suffix, 6, "_curve") == 0 ||
      leaf.compare(suffix, 8, "_enabled") == 0) {
    if (clearing)
      return g_override.erase(leaf) > 0;
    g_override[leaf] = std::string(v);
    return true;
  }
  return false;
}

std::vector<std::string> tuningKeys() {
  std::vector<std::string> out;
  for (const auto &[k, v] : g_override)
    out.push_back(k + "=" + v);
  return out;
}

void registerAll(HANDLE handle) {
  // deque: the keys must outlive registration (CIntValue keeps them as const char*).
  static std::deque<std::string> keys;
  const auto key = [&](const char *stem, const char *suffix) {
    keys.emplace_back(std::string("plugin:hyprview:") + stem + suffix);
    return keys.back().c_str();
  };

  g_globallyEnabled.v = makeShared<Config::Values::CIntValue>(
      key("animations", "_enabled"), "", static_cast<Config::INTEGER>(1));
  g_globallyEnabled.def = 1;
  HyprlandAPI::addConfigValueV2(handle, g_globallyEnabled.v);

  const auto plain = [&](cfg::Str &h, const char *stem, const char *def) {
    h.v = makeShared<Config::Values::CStringValue>(key("", stem), "", def);
    h.def = def;
    HyprlandAPI::addConfigValueV2(handle, h.v);
  };
  plain(g_gridSwapStyle, "grid_swap_style", "horizontal");
  plain(g_stripSwapStyle, "strip_swap_style", "horizontal");
  plain(g_jumpStyle, "jump_style", "instant");

  for (std::size_t i = 0; i < static_cast<std::size_t>(Param::Count); ++i) {
    g_params[i].v = makeShared<Config::Values::CFloatValue>(
        key("", kParamKeys[i]), "", static_cast<float>(kParamDefaults[i]));
    g_params[i].def = kParamDefaults[i];
    HyprlandAPI::addConfigValueV2(handle, g_params[i].v);
  }

  for (auto &r : kRows) {
    r.enabled.v = makeShared<Config::Values::CIntValue>(key(r.name, "_enabled"), "",
                                                       static_cast<Config::INTEGER>(1));
    r.enabled.def = 1;
    HyprlandAPI::addConfigValueV2(handle, r.enabled.v);
    r.speed.v = makeShared<Config::Values::CFloatValue>(key(r.name, "_speed"), "", 1.0F);
    r.speed.def = 1.0F;
    HyprlandAPI::addConfigValueV2(handle, r.speed.v);
    r.ms.v = makeShared<Config::Values::CIntValue>(key(r.name, "_ms"), "",
                                                  static_cast<Config::INTEGER>(r.base));
    r.ms.def = static_cast<int>(r.base);
    HyprlandAPI::addConfigValueV2(handle, r.ms.v);
    r.curve.v = makeShared<Config::Values::CStringValue>(key(r.name, "_curve"), "",
                                                         r.curveDef);
    r.curve.def = r.curveDef;
    HyprlandAPI::addConfigValueV2(handle, r.curve.v);
    if (r.hasStyle) {
      r.style.v = makeShared<Config::Values::CStringValue>(key(r.name, "_style"), "",
                                                          r.styleDefault);
      r.style.def = r.styleDefault;
      HyprlandAPI::addConfigValueV2(handle, r.style.v);
    }
  }
}

int luaSetRule(::lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 1);
  lua_getfield(L, 1, "leaf");
  const char *leaf = luaL_checkstring(L, -1);
  lua_pop(L, 1);
  Rule r;
  auto num = [&](const char *k) -> std::optional<double> {
    lua_getfield(L, 1, k);
    const bool ok = lua_isnumber(L, -1);
    const double v = ok ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    return ok ? std::optional(v) : std::nullopt;
  };
  auto str = [&](const char *k) -> std::optional<std::string> {
    lua_getfield(L, 1, k);
    const bool ok = lua_isstring(L, -1);
    const std::string v = ok ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return ok ? std::optional(v) : std::nullopt;
  };
  lua_getfield(L, 1, "enabled");
  if (lua_isboolean(L, -1))
    r.enabled = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (const auto v = num("speed"))
    r.speed = *v;
  if (const auto v = num("ms"))
    r.ms = static_cast<int>(*v);
  r.curve = str("curve");
  r.style = str("style");
  setRule(leaf, r);
  return 0;
}
} // namespace hyprview::anim

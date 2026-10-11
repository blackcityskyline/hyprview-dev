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

namespace gloview::anim {

// One row per animation: id, config key stem, defaults, and whether it has a style. This
// array IS the registry -- indexed by Id, so an id cannot exist without a row.
struct Row {
  const char *name;
  cfg::Int    enabled;
  cfg::Float  speed;
  cfg::Str    curve;
  cfg::Str    style;
  cfg::Int    ms;      // absolute duration; _speed still scales it
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
  // One animation per ACTION, not one per feeling. These three shared "drag" while meaning
  // a landing, a fly-between-slots and a raise; config could not tell them apart, and a
  // single _speed therefore moved all three at once.
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
} // namespace

static std::unordered_map<std::string, std::string> g_override;

static const char *overridden(const char *key) {
  const auto it = g_override.find(key);
  return it == g_override.end() ? nullptr : it->second.c_str();
}

static double num(const char *s) { return s ? std::atof(s) : 0.0; }

Cfg cfg(Id id) { return cfg(kRows[static_cast<std::size_t>(id)].name); }

Cfg cfg(const char *n) {
  // Not an animation: no motion, and crucially no invented duration.
  const Row *r = find(n);
  if (!r)
    return Cfg{};

  Inputs in;
  const std::string k = r->name;
  in.globalEnabled = g_globallyEnabled ? (g_globallyEnabled != 0) : true;
  in.enabled       = !(r->enabled && r->enabled.get() == 0);
  in.hasMs         = static_cast<bool>(r->ms.v);
  in.ms            = in.hasMs ? r->ms.get() : 0;
  in.speed         = r->speed ? static_cast<double>(r->speed.get()) : 1.0;
  in.curve         = static_cast<bool>(r->curve.v) ? r->curve.get() : std::string();
  in.rule          = ruleFor(r->name);

  // An explicit command beats a config-file rule, and the rule is what the user's
  // `gloview.animation({leaf=..., speed=...})` installs. resolve() applies the rule
  // last, so setting in.speed alone is not enough -- a rule carrying speed=1.2 kept
  // dividing a requested 320 down to 266 and the matrix could not test a duration.
  // The rule is therefore copied with the overridden fields cleared: everything the
  // command did not touch keeps the user's rule, and what it did touch is absolute.
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
  // grid_swap_style / strip_swap_style / jump_style are NOT rows -- there is no
  // "grid_swap" in kRows -- and go through their own accessors. Reached by name here
  // only so a Lua caller asking style("grid_swap_style") gets the same answer.
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

// By value, not by reference: Str::get() returns std::string by value, so the ternary
// produces a temporary and a const& return would dangle. gridSwapStyle/stripSwapStyle
// had the same shape and no callers at all -- style() reaches those directly -- so they
// are gone rather than fixed.
// The three style keys that are not rows. Each reads its own handle and defers to an
// explicit command override -- the harness found the omission the hard way: every
// style except the default came back as horizontal, because these accessors bypassed
// style() and so bypassed the override with it.
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

// Runtime tuning by key name: "<leaf>_ms", "<leaf>_speed", "<leaf>_curve",
// "<leaf>_enabled", plus the two swap styles.
//
// There is a layer here because the plugin has no other way to be told: hl.config
// rejects every `plugin:gloview:` key at runtime, and hyprctl keyword refuses
// outright under the Lua parser. A CConfigValue exposes no setter either -- the write
// path belongs to ConfigManager, which plugins cannot reach. So the override is ours,
// consulted BEFORE the config value and empty unless a command set it.
//
// Deliberately not a shadow copy of the config: cfg()/style() still read the config
// every frame, and only an explicitly-set key is diverted. A reload clears it.

bool setTuning(const std::string &rawKey, const std::string &value) {
  // Three namespaces, tried in order, and anim keys are stored by their SHORT name --
  // cfg() looks them up as "<leaf>_ms", so a full-key spelling here would silently
  // write something nothing ever reads.
  //
  // A bare name is expanded to the plugin key space first, so "layout" and
  // "plugin:gloview:layout" are the same command; hl.config speaks in full keys and a
  // harness copying one out of a log should not have to strip it.
  const std::string full = rawKey.rfind("plugin:", 0) == 0
                               ? rawKey
                               : ("plugin:gloview:" + rawKey);
  constexpr std::size_t kPrefix = sizeof("plugin:gloview:") - 1; // 15, counted once
  const std::string leaf = full.rfind("plugin:gloview:", 0) == 0
                               ? full.substr(kPrefix)
                               : rawKey;

  // A bare pair of quotes means CLEAR, not a zero: the tokenizer splits on spaces only,
  // so `set open_ms ""` arrives as two literal quote characters. Stored as-is they reach
  // num() as 0 and every animation silently becomes one frame long, while the erasing
  // branch below sat unreachable from the command -- the command rejects a two-token call
  // before setTuning is even entered.
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
  // `_style` joins the list because every row that HAS a style reads one, and until now
  // only the three names above could be written -- so `set ws_enter_style pop` answered
  // "unknown key" while style("ws_enter") sat there reading a key nothing could produce.
  // The grid_swap_style shape again, one level down: a documented key no command reaches.
  // Guarded on hasStyle, or this would accept `_style` for a row that has none and
  // style() would still return {} for it.
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
    keys.emplace_back(std::string("plugin:gloview:") + stem + suffix);
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
  // Read by gridSwapStyle()/stripSwapStyle(), not by style(): style() resolves a
  // name against kRows and neither of these is a row. See anim.hpp.
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
} // namespace gloview::anim

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <hyprland/src/plugins/PluginAPI.hpp>

#include "config/config.hpp"
#include "anim_types.hpp"

// THE animation registry. Every animation is declared EXACTLY ONCE, as one row of kRows in
// anim.cpp carrying its enum id, config keys and defaults, so they cannot drift apart. The
// previous design kept them in three places and drifted for real: leaf("new_card") named an
// unregistered animation and silently answered 250ms/easeout.
//
// Nothing substitutes a default for an animation that is not there: an unknown or unbound id
// resolves to Cfg{on=false}, so the motion does not happen rather than happening at some
// duration nobody asked for.
namespace gloview::anim {

enum class Id : std::uint8_t {
  Open, Close, Glide, Appear, Card, Pulse, StripStep,
  WsEnter, WsExit, ExpoIn, ExpoOut, JumpIn, JumpOut,
  SwapMain, SwapPartner, Drop, SwapFly, Lift, CarouselStep, NewCard,
  Count
};

// Cfg, Rule, Inputs and resolve() live in anim_types.hpp, free of hyprland/ includes so a
// test can reach the resolver.
[[nodiscard]] Cfg cfg(Id id);

// Name-keyed, for the Lua layer and config lookups. An unknown name is not an animation.
[[nodiscard]] Cfg cfg(const char *name);
[[nodiscard]] std::string style(const char *name);

[[nodiscard]] double param(Param p);

// Not animation styles: behaviour selectors sharing the config section.
[[nodiscard]] std::string jumpStyle();
// grid_swap_style / strip_swap_style are NOT kRows entries, so style() cannot reach them:
// asking returned {} silently, and {} parses to Horizontal, so the documented key did
// nothing. These read the registered handles directly.
[[nodiscard]] std::string gridSwapStyle();
[[nodiscard]] std::string stripSwapStyle();

void setRule(const char *name, const Rule &r);

// Runtime tuning by key name: "<leaf>_ms" / "_speed" / "_curve" / "_enabled", plus the three
// styles. An empty value clears one. Everything is still read from config every frame; only a
// key set here is diverted.
bool setTuning(const std::string &key, const std::string &value);

// Currently-active overrides, "key=value", so a harness can confirm what it set.
[[nodiscard]] std::vector<std::string> tuningKeys();

int  luaSetRule(lua_State *L);
void registerAll(HANDLE handle);

} // namespace gloview::anim

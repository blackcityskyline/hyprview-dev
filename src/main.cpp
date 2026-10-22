#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "debug/log.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "clock/curves.hpp"
#include "anim.hpp"
#include "config/config.hpp"
#include "overview.hpp"

inline std::unique_ptr<hyprview::Overview> g_overviewOwned;

namespace {

// null name field = that path is skipped. hyprviewunload is UNLOAD-safe (hardClose); hyprviewclose only starts close.
struct Action {
    const char* dispatch;
    const char* hyprctl;
    const char* lua;
    void (*invoke)();
};

constexpr std::array<Action, 9> kActions{{
    {"toggle", "hyprview", "toggle",
     [] { if (g_overview) g_overview->toggle(); }},
    {"open", nullptr, "open",
     [] { if (g_overview) g_overview->open(); }},
    {"close", "hyprviewclose", "close",
     [] { if (g_overview) g_overview->close(); }},
    {"desktop", "hyprviewdesktop", "desktop",
     [] { if (g_overview) g_overview->toggleDesktop(); }},
    {"allworkspaces", "hyprviewall", "allworkspaces",
     [] { if (g_overview) g_overview->toggleAllWorkspaces(); }},
    {"togglecarousel", "hyprviewcarousel", "togglecarousel",
     [] { if (g_overview) g_overview->toggleCarousel(); }},
    {"alttab", "hyprviewalttab", "alttab",
     [] { if (g_overview) g_overview->altTabInvoke(false); }},
    {"alttabback", "hyprviewalttabback", "alttabback",
     [] { if (g_overview) g_overview->altTabInvoke(true); }},
    {nullptr, "hyprviewunload", nullptr,
     [] { if (g_overview) g_overview->hardClose(); }},
}};

template <size_t I>
int luaInvoke(lua_State*) {
    kActions[I].invoke();
    return 0;
}

int luaCmd(lua_State *L) {
    const char *args = luaL_optstring(L, 1, "");
    std::string err = g_overview ? g_overview->runCommand(args)
                                 : std::string("no overview");
    // hyprctl eval does NOT print a Lua return value: every command acknowledges itself in the trace log.
    hyprview::debug::trace::emitState(err.empty() ? std::string("CMD|ok|") + args
                                                 : std::string("CMD|err|") + args +
                                                       "|" + err);
    lua_pushstring(L, err.empty() ? "ok" : err.c_str());
    return 1;
}

}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    // build stamp first: grep the log to confirm a hot-reload loaded the new binary
    hyprview::debug::dbg("[hyprview] PLUGIN_INIT build=" __DATE__ " " __TIME__);

    hyprview::cfg::registerAll(handle);
    hyprview::curves::registerBuiltins();

    g_overviewOwned = std::make_unique<hyprview::Overview>(handle);
    g_overview      = g_overviewOwned.get();
    g_overview->startFeedPoll();
    if (!g_overview->initialize()) {
        HyprlandAPI::addNotification(handle, "[hyprview] initialization failed", CHyprColor(1.0, 0.2, 0.2, 1.0), 6000);
        g_overview = nullptr;
        g_overviewOwned.reset();
        // throw so Hyprland ejects us; a half-alive instance blocks later loads
        throw std::runtime_error("[hyprview] initialization failed");
    }

    const bool isLua = Config::mgr() && Config::mgr()->type() == Config::CONFIG_LUA;

    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((kActions[Is].dispatch &&
          HyprlandAPI::addDispatcherV2(
              handle, std::string("hyprview:") + kActions[Is].dispatch,
              [fn = kActions[Is].invoke](std::string) -> SDispatchResult {
                  fn();
                  return {.success = true};
              })),
         ...);

        ((kActions[Is].hyprctl &&
          HyprlandAPI::registerHyprCtlCommand(
              handle,
              SHyprCtlCommand{
                  .name  = kActions[Is].hyprctl,
                  .exact = true,
                  .fn    = [fn = kActions[Is].invoke](eHyprCtlOutputFormat, std::string) -> std::string {
                      fn();
                      return "ok\n";
                  }})),
         ...);

        // Registered outside the kActions fold: it takes arguments, and a failure must reach the caller as text.
        HyprlandAPI::addDispatcherV2(handle, "hyprview:cmd",
                                     [](std::string a) -> SDispatchResult {
                                         if (!g_overview)
                                             return {.error = "no overview"};
                                         const std::string err = g_overview->runCommand(a);
                                         if (!err.empty())
                                             return {.error = err};
                                         return {.success = true};
                                     });

        if (isLua) {
            ((kActions[Is].lua &&
              HyprlandAPI::addLuaFunction(handle, "hyprview", kActions[Is].lua,
                                          &luaInvoke<Is>)),
             ...);
            HyprlandAPI::addLuaFunction(handle, "hyprview", "cmd", &luaCmd);
            HyprlandAPI::addLuaFunction(handle, "hyprview", "curve",
                                        &hyprview::curves::luaRegister);
            HyprlandAPI::addLuaFunction(handle, "hyprview", "animation",
                                        &hyprview::anim::luaSetRule);
        }
    }(std::make_index_sequence<kActions.size()>{});

    HyprlandAPI::reloadConfig();

    return {
        .name        = "HyprView",
        .description = "macOS Mission Control-style overview",
        .author      = "Vergil",
        .version     = "0.3.1-test",
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_overview = nullptr;
    g_overviewOwned.reset();
}

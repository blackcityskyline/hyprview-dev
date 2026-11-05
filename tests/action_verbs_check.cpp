// The plugin's public verbs: one, all, desktop, browser, switcher.
//
// Five, and named for the view they open, because the old names described the mechanism
// (toggle/allworkspaces/alttab) and the user had to remember which was which. Each one opens
// the view it names, so a config that starts in the expo cannot decide what "one" means.
//
// The switcher owns its run: the binding opens it and Tab/Shift+Tab step inside the plugin.
// A second "back" verb would be a second thing to bind and a second way to disagree with the
// step the wheel and the arrows already share.

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int fail(const char *what) {
  std::printf("FAIL: %s\n", what);
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const std::string main = slurp(root + "/src/main.cpp");
  const std::string keys = slurp(root + "/src/input/keys.cpp");
  const std::string life = slurp(root + "/src/session/lifecycle.cpp");
  if (main.empty())
    return fail("src/main.cpp not found");
  if (keys.empty())
    return fail("src/input/keys.cpp not found");
  if (life.empty())
    return fail("src/session/lifecycle.cpp not found");

  // Every verb is registered with a Lua name, a dispatcher and a hyprctl command: a verb the
  // Lua config cannot reach is one the user cannot bind.
  for (const char *verb : {"one", "all", "desktop", "browser", "switcher"}) {
    const std::string row = std::string("{\"") + verb + "\", \"hyprview" + verb + "\", \"" + verb + "\"";
    if (main.find(row) == std::string::npos) {
      std::printf("FAIL: the `%s` verb is not registered as dispatcher + hyprctl + Lua\n", verb);
      return 1;
    }
  }

  // The back verb is gone from the public surface; the step stays inside.
  if (main.find("alttabback") != std::string::npos)
    return fail("`alttabback` is still registered -- the run must step itself");

  // Backwards is a modifier on the key the run already uses, not a separate binding.
  if (keys.find("m_altTabbing && k == KEY_TAB") == std::string::npos)
    return fail("Tab no longer steps the run -- Shift+Tab has nowhere to land");
  if (keys.find("mods & HL_MODIFIER_SHIFT") == std::string::npos)
    return fail("Shift+Tab does not step backwards");

  // "one" is the single-workspace view whatever the config says, and it must put the override
  // back when the open is refused, or a refused open leaves the plugin stuck in expo.
  if (life.find("void Overview::toggleOne()") == std::string::npos)
    return fail("toggleOne is gone");
  const auto one = life.find("void Overview::toggleOne()");
  const auto oneBody = life.substr(one, life.find("void Overview::toggleBrowser()", one) - one);
  if (oneBody.find("setAllOverride(0)") == std::string::npos)
    return fail("`one` does not force the single-workspace view");
  if (oneBody.find("setAllOverride(-1)") == std::string::npos)
    return fail("`one` never restores the override after a refused open");

  // "browser" picks the domain and tells open() not to re-read the config over it.
  if (life.find("m_view.setDomain(model::Domain::Tabs);") == std::string::npos)
    return fail("`browser` does not aim the open at the tab view");
  if (life.find("if (!keepDomain)") == std::string::npos)
    return fail("open() overwrites the domain -- `browser` opens into the config's view");

  std::printf("PASS: five verbs, and the switcher steps itself\n");
  return 0;
}

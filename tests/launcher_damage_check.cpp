// Typing in the launcher must repaint the monitor.
//
// The overview draws over the composited frame, so a frame that arrives for someone else's
// 32x32 damage patch repaints only that patch. A query edit that raises no damage of its own
// therefore updates the panel in patches: one part shows the new hits, the rest keeps the old
// ones. The App and Action rows return early from syncLauncherSelection(), which is exactly
// where the damage used to live -- so the query field and the app results never repainted.

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

  const std::string src = slurp(root + "/src/session/launcher.cpp");
  if (src.empty())
    return fail("src/session/launcher.cpp not found");

  const auto at = src.find("void Overview::syncLauncherSelection()");
  if (at == std::string::npos)
    return fail("syncLauncherSelection() is gone");
  const auto end = src.find("\nvoid Overview::", at + 1);
  if (end == std::string::npos)
    return fail("syncLauncherSelection() body not found");
  const auto body = src.substr(at, end - at);

  const auto damage = body.find("damage();");
  const auto guard = body.find("LauncherKind::App");
  if (damage == std::string::npos)
    return fail("syncLauncherSelection() raises no damage: a query edit repaints only the "
                "patches another client happened to damage");
  if (guard != std::string::npos && damage > guard)
    return fail("the damage sits after the App/Action early return: app results and the "
                "query field stay stale");

  std::printf("PASS: every launcher selection sync repaints the monitor before bailing out\n");
  return 0;
}

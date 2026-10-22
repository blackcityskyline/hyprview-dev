// The badge's schedule and the icon's shadow are one rule each, and a unit test cannot see
// the call site (AGENTS.md 7.1.5). The rule lives in render::badgeK and render::iconShadowSpec;
// a card that computes its own schedule is the bug this is written against.

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

  const std::string tile = slurp(root + "/src/render/tile_view.cpp");
  if (tile.empty())
    return fail("src/render/tile_view.cpp not found");

  if (tile.find("render::badgeK(") == std::string::npos)
    return fail("the card does not ask render::badgeK for its schedule -- the badge's window is "
                "computed in one place, and this is not it");

  if (tile.find("std::clamp(openT()") != std::string::npos)
    return fail("the card computes the badge schedule itself");

  if (tile.find("render::iconShadowSpec(") == std::string::npos)
    return fail("the card does not ask render::iconShadowSpec for the shadow -- the icon's shadow "
                "is a share of its side, and only the builder knows that");

  std::printf("PASS: the badge schedule and the icon shadow come from one rule each\n");
  return 0;
}

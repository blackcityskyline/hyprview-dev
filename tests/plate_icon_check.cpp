// The plate's icon and its shadow are one rule each, and a unit test cannot see the call site
// (AGENTS.md 7.1.5). The rules live in render::iconPx and render::iconShadowSpec; a card that
// asks for an arbitrary texture size, or builds its own shadow, is the bug this is written
// against. The icon is drawn by the caption, so the caption's body is where both are checked.

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

  const auto plate = tile.find("void Overview::drawTileCaption");
  if (plate == std::string::npos)
    return fail("the caption is gone from tile_view.cpp");
  const auto next = tile.find("\nvoid Overview::", plate + 1);
  const auto body = tile.substr(plate, next == std::string::npos ? tile.size() - plate
                                                                : next - plate);

  if (body.find("render::iconPx(") == std::string::npos)
    return fail("the plate's icon is not sized by render::iconPx -- a size the cache does not "
                "have is a decode per frame");

  if (body.find("render::iconShadowSpec(") == std::string::npos)
    return fail("the plate's icon shadow does not come from render::iconShadowSpec -- the "
                "shadow is a share of the icon's side, and only the builder knows that");

  std::printf("PASS: the plate's icon is sized and shadowed by one rule each\n");
  return 0;
}

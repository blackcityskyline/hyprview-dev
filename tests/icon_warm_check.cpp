// A cold class icon costs ~35 ms: the XDG scan plus a PNG decode. Paid inside paint it
// dropped the first frames of the entry; the scene build pays it once, before the clock.

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

  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string tile = slurp(root + "/src/render/tile_view.cpp");
  if (build.empty())
    return fail("src/build/build.cpp not found");
  if (tile.empty())
    return fail("src/render/tile_view.cpp not found");

  if (build.find("warmAppIcon(") == std::string::npos)
    return fail("the tile build no longer warms the class icon -- the first entry after load "
                "pays the decode in paint");

  if (tile.find("appIcon(") != std::string::npos || tile.find("appSurface(") != std::string::npos)
    return fail("paint resolves the app icon itself -- a cold class decode lands inside the frame");

  std::printf("PASS: icons are warmed while the scene is built\n");
  return 0;
}

// The caption's icon shadow is the plugin's own. Hyprland's shader divides by the shape it is
// given, and a caption that pops (a card that has the caption outright takes it away) hands it
// a shape of nothing: the compositor divides by zero and the session dies with it. Twice.

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

  // The favicon's shadow, in the caption. Scope the scan to that function: later code in the
  // file may draw its own shadow for its own reason, and the hazard is the caption's icon.
  const auto caption = tile.find("void Overview::drawTileCaption");
  if (caption == std::string::npos)
    return fail("the caption is gone from tile_view.cpp");
  const auto next = tile.find("\nvoid Overview::", caption + 1);
  const auto body = tile.substr(caption, next == std::string::npos ? tile.size() - caption
                                                                  : next - caption);
  if (body.find("dropShadow(") != std::string::npos)
    return fail("the caption's icon shadow is Hyprland's again -- a popping caption hands the "
                "shader a shape of nothing, and the compositor dies");

  std::printf("PASS: the caption's icon shadow is the plugin's own\n");
  return 0;
}

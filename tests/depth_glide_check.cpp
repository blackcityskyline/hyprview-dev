// The deck's scale (depth) used to be a raw view value: the boxes of the surviving cards
// glided into the freed slot by the tile clock, while the scale jumped in one frame --
// most visibly when the card that died was the focused one and the next card leapt
// from the far rank into the centre. The depth is a tile-carried pair now, lerped by
// the same clock as the box; this check keeps all three sites on that rule.

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

  const std::string model = slurp(root + "/src/model/model.hpp");
  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string view = slurp(root + "/src/render/carousel_view.cpp");
  if (model.empty())
    return fail("src/model/model.hpp not found");
  if (build.empty())
    return fail("src/build/build.cpp not found");
  if (view.empty())
    return fail("src/render/carousel_view.cpp not found");

  if (model.find("depthFrom") == std::string::npos ||
      model.find("depthTo") == std::string::npos)
    return fail("the tile no longer carries a depth pair -- the deck's scale snapped again");

  if (build.find("t.depthFrom") == std::string::npos ||
      build.find("t.depthTo") == std::string::npos)
    return fail("the resolve loop no longer seats the depth on the tile clock");

  if (view.find("runLook(box, card.depth") != std::string::npos)
    return fail("the run feeds the look a raw instant depth instead of the lerped one");

  if (view.find("depthFrom") == std::string::npos || view.find("depthTo") == std::string::npos)
    return fail("the run's depth does not ride the tile clock with the box");

  std::printf("PASS: the deck's depth rides the tile clock like the box\n");
  return 0;
}

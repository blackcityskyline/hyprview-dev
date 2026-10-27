// A card returns to the place it BELONGS to. Its natural is where it was, and a step overwrites
// that with the slot the card just left -- so the exit has to read the card's home, or the tab
// cards of a run have nowhere to fly and vanish in one frame. The exit also plays the entry
// FORWARD: eased on a falling progress, the card stood still while the glass was already there,
// then snapped home.

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

  const std::string placement = slurp(root + "/src/clock/placement.cpp");
  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string model = slurp(root + "/src/model/model.hpp");
  if (placement.empty())
    return fail("src/clock/placement.cpp not found");
  if (build.empty())
    return fail("src/build/build.cpp not found");
  if (model.empty())
    return fail("src/model/model.hpp not found");

  if (model.find("LRect home;") == std::string::npos)
    return fail("the tile has no home -- a card has to know where it belongs");

  if (build.find("t.home = box;") == std::string::npos)
    return fail("the build no longer records the card's home -- the window's box, or the browser "
                "window a tab is in");

  if (placement.find("t.home") == std::string::npos)
    return fail("the placement does not read the card's home -- the exit returns to where the "
                "card was, and a step overwrote that with the slot");

  if (placement.find("1.0 - curves::eval(curve, 1.0 - p)") == std::string::npos)
    return fail("the exit eases a falling progress -- the card stands still and then snaps home "
                "while the glass and the strip are already gone");

  std::printf("PASS: a card returns to the place it belongs to\n");
  return 0;
}

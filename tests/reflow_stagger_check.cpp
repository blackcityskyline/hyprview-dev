// The deck reflow that follows a killed card staggers in the run (carousel) branch too:
// the delay rank is the SLOT DISTANCE to the focused card, not the tile index. The grid
// keeps its index stagger; only the run derives the rank from view.slotOf().

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
  if (placement.empty())
    return fail("src/clock/placement.cpp not found");

  if (placement.find("view.slotOf(") == std::string::npos)
    return fail("the run branch no longer staggers by slot distance -- the deck repositions in lock-step again");

  if (placement.find("slotOf(static_cast<std::size_t>(scene.focused()))") == std::string::npos)
    return fail("the run branch does not anchor the stagger rank at the focused card's slot");

  std::printf("PASS: the run's reflow stagger reads the rank from the slot distance\n");
  return 0;
}

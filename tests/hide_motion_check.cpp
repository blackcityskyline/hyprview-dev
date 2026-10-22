// The card's hide motion is written ONCE. It used to be in two: the placement's box for the
// grid, and a second copy in the carousel for the run -- the same rule, two homes, and the run
// could drift from the grid without anything saying so. The placement owns it now, and the
// run's depth reaches it through the view.

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
  const std::string carousel = slurp(root + "/src/render/carousel_view.cpp");
  if (placement.empty())
    return fail("src/clock/placement.cpp not found");
  if (carousel.empty())
    return fail("src/render/carousel_view.cpp not found");

  if (placement.find("anim::hideShrink(") == std::string::npos)
    return fail("the placement no longer shrinks a card that cannot land -- the rule has no home");

  if (carousel.find("anim::hideShrink(") != std::string::npos)
    return fail("the run shrinks a card on its own again -- the hide motion is the placement's");

  if (carousel.find("anim::cardHides(") != std::string::npos)
    return fail("the run asks whether a card hides on its own -- the placement asks that now");

  std::printf("PASS: the hide motion is written once, in the placement\n");
  return 0;
}

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

  // A window the layout scrolled past has its box off the monitor. Flying the card into that
  // box reads as the card vanishing sideways; it must shrink where it is instead, exactly as a
  // cross-workspace card does. Dwindle and master are unaffected because their boxes are visible.
  if (placement.find("canLand ? t.home : t.target") == std::string::npos)
    return fail("the exit always flies to the home -- an off-screen window takes the card with it");
  if (placement.find("f.lands(t)") == std::string::npos)
    return fail("the exit does not ask whether the home is visible -- the close is not per-layout");
  const std::string frame = slurp(root + "/src/clock/placement.hpp");
  if (frame.find("render::landsOnScreen(t.home, monW, monH)") == std::string::npos)
    return fail("Frame::lands no longer tests the landing box against the monitor");

  // close() writes the landing box; a second writer left the home at a stale slot.
  const std::string lifecycle = slurp(root + "/src/session/lifecycle.cpp");
  if (lifecycle.empty())
    return fail("src/session/lifecycle.cpp not found");
  if (lifecycle.find("t.natural = t.target;") != std::string::npos)
    return fail("close() still writes the natural -- box() reads home, so the write is dead and "
                "the scrolled card keeps a stale landing");
  if (lifecycle.find("t.home = t.target;") == std::string::npos)
    return fail("close() no longer marks a card with no landing as dissolving in place");

  // The landing group belongs to ONE pick: left over, it hides the workspace fallback.
  const auto openAt = lifecycle.find("void Overview::open(");
  const auto groupAt = lifecycle.find("m_landingGroup = model::GroupRef{};");
  if (openAt == std::string::npos || groupAt == std::string::npos || groupAt < openAt ||
      groupAt > lifecycle.find("void Overview::close()"))
    return fail("open() no longer clears the landing group -- the next jump lands nowhere");

  std::printf("PASS: a card returns to the place it belongs to\n");
  return 0;
}

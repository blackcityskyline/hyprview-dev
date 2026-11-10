// A card dropped on empty space belongs to the layout, not to the cursor.
//
// The canvas (desktop) view is a MAP of the real desktop: its cells are workspaces, and the
// windows inside a cell keep the places they really have. A tile whose box the drop handler
// owned therefore said something untrue about the screen -- and the box outlived the view:
// the grid and the carousel branches write EVERY target, so the parked flag survived a mode
// switch holding whichever box the other view had just assigned, and the card came back to
// the canvas sitting on top of its neighbours.
//
// So there is one writer of a tile's box: layoutTiles(). The check is a grep because the
// defect is the existence of a second writer, which no compositor-free test can simulate.

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

  const std::string mouse = slurp(root + "/src/input/mouse.cpp");
  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string model = slurp(root + "/src/model/model.hpp");
  const std::string scene = slurp(root + "/src/model/scene.hpp");
  if (mouse.empty() || build.empty() || model.empty() || scene.empty())
    return fail("src/input/mouse.cpp, src/build/build.cpp or the model headers not found");

  if (model.find("parked") != std::string::npos)
    return fail("Tile carries a parked flag again: a box the drop handler owns outlives the "
                "view that produced it, and the next layout leaves the card wherever the "
                "previous view put it");
  if (scene.find("void park(") != std::string::npos)
    return fail("Scene::park() is back: a second writer of a tile's box");
  if (mouse.find("m_scene.park(") != std::string::npos ||
      mouse.find("desktopPark") != std::string::npos)
    return fail("the drop path free-places in the canvas again: cards stay where they are "
                "released, overlap their neighbours and never return to their workspace cell");
  if (build.find("t.parked") != std::string::npos)
    return fail("layoutTiles() skips a tile again while the other layout branches still write "
                "its target -- a mode switch then hands the canvas a card in the wrong box");
  if (build.find("void Overview::buildTiles()") != std::string::npos &&
      build.find("priorTargets") != std::string::npos)
    return fail("buildTiles() carries drop-time boxes across a rebuild again");

  // Deleting the branch must leave a drop that still goes somewhere: the flyback is what
  // pulls the card home instead of freezing it under the cursor.
  if (mouse.find("Tile->flyback") == std::string::npos)
    return fail("the tile drop has no flyback branch: a release on empty space now does "
                "nothing at all");

  std::printf("PASS: a tile's box comes from layoutTiles() alone, the canvas included\n");
  return 0;
}

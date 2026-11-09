// App Exposé rebuilds the deck on every frame when the class filter is known to only one
// side: buildTiles() dropped the other classes, syncTiles() counted them and saw a mismatch,
// so the cards restarted their glide 60 times a second -- the stutter that looked like
// "the grid keeps finishing itself" while the cursor moved.
//
// The rule has to live in the ONE predicate both callers share (tileBelongs). Two copies of
// it drift, and the drift is invisible until it is a frame-rate bug.

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

  const std::string lifecycle = slurp(root + "/src/session/lifecycle.cpp");
  const std::string build = slurp(root + "/src/build/build.cpp");
  if (lifecycle.empty() || build.empty())
    return fail("src/session/lifecycle.cpp or src/build/build.cpp not found");

  const auto shared = lifecycle.find("bool Overview::tileBelongs");
  if (shared == std::string::npos)
    return fail("tileBelongs() is gone -- buildTiles() and syncTiles() have no shared rule");

  const auto at = lifecycle.find("exposeClass", shared);
  if (at == std::string::npos || at - shared > 2000)
    return fail("tileBelongs() no longer filters by the expose class: syncTiles() counts "
                "every window while the scene holds only the expo's class, so the deck "
                "rebuilds and restarts its glide every frame");

  const auto sync = build.find("void Overview::syncTiles()");
  if (sync == std::string::npos)
    return fail("syncTiles() not found");
  if (build.find("tileBelongs", sync) == std::string::npos)
    return fail("syncTiles() no longer counts with tileBelongs()");

  // One rule, one place: a second COPY OF THE COMPARISON in buildTiles() is what drifted.
  const auto bt = build.find("void Overview::buildTiles()");
  const auto btEnd = build.find("\nvoid Overview::", bt + 1);
  if (bt == std::string::npos || btEnd == std::string::npos)
    return fail("buildTiles() not found");
  if (build.substr(bt, btEnd - bt).find("m_class != m_view.exposeClass()") != std::string::npos)
    return fail("buildTiles() compares the window class itself again -- the comparison "
                "belongs in tileBelongs(), where syncTiles() also reads it");

  std::printf("PASS: the expose class filter lives in the predicate both callers share\n");
  return 0;
}

// One kill path drives a window card and a tab card.
//
// The two used to part ways after the drop: a window entered `m_pendingKill`, left the scene
// in the drop frame and was held out until its close; a tab painted its flight, stayed in the
// scene, and was rebuilt away on the next feed tick -- with a ghost at the slot, which read as
// the card landing back home for one frame.
//
// The pending state is a SubjectRef, and the three places that ask "is this card dying" ask it
// by subject. Reverting any of them to a window handle reddens this check.

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

size_t countOf(const std::string &s, const std::string &needle) {
  size_t n = 0;
  for (size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1))
    ++n;
  return n;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const std::string hpp = slurp(root + "/src/overview.hpp");
  const std::string actions = slurp(root + "/src/actions/actions.cpp");
  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string clocks = slurp(root + "/src/clock/clocks.cpp");
  if (hpp.empty() || actions.empty() || build.empty() || clocks.empty())
    return fail("sources not found");

  if (hpp.find("model::SubjectRef m_pendingKill;") == std::string::npos)
    return fail("the pending kill is not a subject -- a window handle cannot name a tab");

  if (hpp.find("m_pendingKill.lock()") != std::string::npos)
    return fail("the pending kill is a window handle again");

  // Both branches of killTile enter the same pending lifecycle.
  if (countOf(actions, "m_pendingKill = t.subj;") != 2)
    return fail("the tab does not join the window in the pending kill");

  // The scene drops a pending card of either kind, in the drop frame.
  if (build.find("if (t.subj == m_pendingKill)") == std::string::npos)
    return fail("the build does not skip a pending card by subject");

  if (build.find("holdsPending") == std::string::npos ||
      build.find("captureCurrentBoxes(m_pendingKill)") == std::string::npos)
    return fail("the tabs deck waits for the feed tick instead of leaving in the drop frame");

  // The pending is held until the tab leaves the feed, exactly as for a window until it dies.
  if (clocks.find("m_tabs.groupOf(m_pendingKill).id == 0") == std::string::npos)
    return fail("the pending tab is never released");

  std::printf("PASS: one kill path drives window and tab cards\n");
  return 0;
}

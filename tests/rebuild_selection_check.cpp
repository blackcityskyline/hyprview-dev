// Entering all-workspaces from one is an all-open, and a rebuild must not lose the selection.
//
// Two rules, both of which shipped broken:
//   * replayReflow() reorders the tile vector, so an index kept across it selects a DIFFERENT
//     window. The selection is a subject and has to be re-found by subject.
//   * 'a' rebuilds in place, so the all_advances step that a fresh open() applies was skipped,
//     and the crate stayed on whatever card the rebuild happened to leave selected.
//
// The bodies live in Hyprland-linked files, so this checks the source text and the order of the
// calls that matters: re-seat the selection BEFORE layout, request the step BEFORE the rebuild.

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

std::string bodyOf(const std::string &src, const std::string &signature) {
  const auto at = src.find(signature);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  bool seen = false;
  for (std::size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
      seen = true;
    } else if (src[i] == '}') {
      --depth;
      if (seen && depth == 0)
        return src.substr(at, i - at + 1);
    }
  }
  return {};
}

bool before(const std::string &body, const std::string &first, const std::string &second) {
  const auto a = body.find(first);
  const auto b = body.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  int failed = 0;

  const std::string BUILD = slurp(root + "/src/build/build.cpp");
  const std::string LIFE = slurp(root + "/src/session/lifecycle.cpp");
  if (BUILD.empty() || LIFE.empty()) {
    std::printf("FAIL: cannot read the sources\n");
    return 1;
  }

  const std::string REFLOW = bodyOf(BUILD, "void Overview::replayReflow(");
  if (REFLOW.empty()) {
    std::printf("FAIL: no replayReflow body\n");
    failed = 1;
  } else if (REFLOW.find("selectSubject(") == std::string::npos) {
    std::printf("FAIL: replayReflow keeps the selection INDEX across a rebuild; it must re-find "
                "the selected subject\n");
    failed = 1;
  } else if (!before(REFLOW, "selectSubject(", "layoutTiles(")) {
    std::printf("FAIL: replayReflow lays out before re-seating the selection\n");
    failed = 1;
  }

  const std::string TOGGLE = bodyOf(LIFE, "void Overview::toggleAllWorkspaces(");
  if (TOGGLE.empty()) {
    std::printf("FAIL: no toggleAllWorkspaces body\n");
    failed = 1;
  } else if (TOGGLE.find("all_advances") == std::string::npos) {
    std::printf("FAIL: entering all from one applies no all_advances step\n");
    failed = 1;
  } else if (!before(TOGGLE, "m_pendingStep", "setAllWorkspaces(")) {
    std::printf("FAIL: the step is requested after the rebuild, so the rebuild never sees it\n");
    failed = 1;
  }

  if (!failed)
    std::printf("rebuild selection: re-seated by subject, step before rebuild\n");
  return failed;
}

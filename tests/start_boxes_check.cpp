// The start boxes of a rebuilt deck come from one pure function, and they are asked
// for every tile.
//
// The rule the pure function states -- a card that joins a shown scene starts at its
// slot -- was in the model, but the CALLER skipped the whole loop when the capture
// was empty. So the cards kept their desktop boxes: a window that joined an empty
// workspace flew in from full screen and shrank into its slot, while the same
// switch between two non-empty workspaces was clean.
//
// The model test covers the rule. This covers the caller: the start boxes are not
// behind a guard on an empty capture, and they are not decided twice.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto src = slurp(root + "/src/build/build.cpp");
  if (src.empty()) {
    std::printf("FAIL: cannot read src/build/build.cpp under %s\n", root.c_str());
    return 1;
  }
  const auto body = bodyOf(src, "void Overview::startTileGlide(");
  if (body.empty()) {
    std::printf("FAIL: startTileGlide not found\n");
    return 1;
  }

  const auto call = body.find("model::startBoxes(");
  if (call == std::string::npos) {
    std::printf("FAIL: the start boxes do not come from model::startBoxes -- the rule "
                "is decided somewhere else, and the model cannot be tested\n");
    return 1;
  }

  // The guard that hid the bug: the start boxes were asked for only when the capture
  // was NOT empty. The ghosts may still be behind that guard; the start boxes may not.
  const auto guard = body.find("if (!oldBoxes.empty())");
  if (guard != std::string::npos && guard < call) {
    std::printf("FAIL: the start boxes are behind a guard on an empty capture -- a card "
                "joining an empty scene keeps its desktop box\n");
    return 1;
  }

  // Decided twice is decided once per rule change, and the two copies drift.
  if (body.find("model::outgoingBox(") != std::string::npos) {
    std::printf("FAIL: the start boxes are decided inline as well -- one place decides "
                "them\n");
    return 1;
  }

  std::printf("start boxes: one pure function, asked for every tile\n");
  return 0;
}

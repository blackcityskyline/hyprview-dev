// AGENTS.md 2.15: the geometry layer takes its settings as arguments and never reads the
// config. "The geometry layer" is the PURE headers -- render/geometry.hpp, render/tile_look.hpp
// -- because that is the boundary the rule names: "the Overview:: wrapper gathers the inputs,
// render::geometry does the arithmetic".
//
// SCOPE, and getting it wrong is how this check would have pushed the codebase the wrong way.
// A first version counted every cfg:: read in src/render/ and found 111, which reads like a
// large violation and is not one: a read inside an Overview:: method is the wrapper doing
// exactly what the rule tells it to do. Demanding those be passed as arguments would push 45
// values through drawTile's signature to satisfy a rule the rule does not make -- more
// plumbing, same behaviour, and every future knob a reason to cheat. So the count is over the
// pure headers only, where a read has no excuse: nothing there can reach a monitor, a window
// or a clock, so a config read there is a pure function that stopped being pure.
//
// This is a boundary, not a budget. It has no baseline and cannot be traded against: zero is
// the only value that means the geometry layer is reachable from ctest without a compositor,
// which is the only reason any of it can be tested at all.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

// The pure half of render/. A cfg:: read here makes the header depend on the compositor's
// config, which takes it out of reach of every header-only test in CMakeLists.txt and puts its
// arithmetic back under test-by-screenshot.
const char *kPure[] = {"render/geometry.hpp", "render/tile_look.hpp"};

// A read is `cfg::something`. Comments and literals are prose about the rule, not the rule, and
// are stripped before the count.
const std::regex kRead(R"(cfg::[a-zA-Z_][a-zA-Z0-9_]*)");

int countReads(const std::string &src) {
  // Comments and string literals are prose about the rule, not the rule. Stripped first so a
  // long explanation of why this file reads nothing does not read as a violation.
  static const std::regex kComment(R"(//[^\n]*|/\*[\s\S]*?\*/)");
  static const std::regex kString(R"("(?:[^"\\]|\\.)*")");
  std::string s = std::regex_replace(src, kComment, " ");
  s = std::regex_replace(s, kString, " \"\" ");
  int n = 0;
  for (auto it = std::sregex_iterator(s.begin(), s.end(), kRead); it != std::sregex_iterator(); ++it)
    ++n;
  return n;
}

int countFile(const std::filesystem::path &p) {
  std::ifstream f(p);
  if (!f)
    return 0;
  return countReads(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()));
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1 ? argv[1] : "src";

  int total = 0;
  for (const char *rel : kPure) {
    const std::filesystem::path p = root / rel;
    if (!std::filesystem::exists(p)) {
      std::fprintf(stderr, "  pure geometry header missing: %s\n", rel);
      return 2;
    }
    const int n = countFile(p);
    if (n) {
      std::fprintf(stderr, "  %s reads the config %d time(s)\n", rel, n);
      total += n;
    }
  }

  if (total) {
    std::fprintf(stderr,
                 "  the geometry layer stays reachable from the header-only tests only while it\n"
                 "  reads nothing but arithmetic (AGENTS.md 2.15). Pass the value in as an\n"
                 "  argument; the wrapper gathers it.\n");
    return 1;
  }
  std::printf("render geometry reads no config: the pure layer is still testable\n");
  return 0;
}
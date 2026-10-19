// The backdrop's base: the blur, the dim, or -- only with the blur off -- the sharp
// source.
//
// A transparent window shows whatever is behind it in the framebuffer. While the
// blur was pending, the backdrop drew its own sharp copy of the desktop under the
// tiles, so for one frame the desktop was visible WITHOUT blur, sharp, and then the
// blur arrived in a single frame. The rule that ends it: with the blur on, the sharp
// source is not a base.
//
// The rule is one pure function, so this checks the two things a function cannot
// enforce: that the base is decided by it, and that the sharp source is drawn in
// exactly one place -- the branch that says so.

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

int countOf(const std::string &hay, const std::string &needle) {
  int n = 0;
  for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
    ++n;
  return n;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto src = slurp(root + "/src/render/backdrop.cpp");
  if (src.empty()) {
    std::printf("FAIL: cannot read src/render/backdrop.cpp under %s\n", root.c_str());
    return 1;
  }

  // The base is decided by the rule, not by a condition of its own.
  if (src.find("backdropBase(") == std::string::npos) {
    std::printf("FAIL: the base is not decided by backdropBase() -- the rule that the "
                "sharp source is never a base under the blur is not applied\n");
    return 1;
  }
  if (countOf(src, "case BackdropBase::Source:") != 1) {
    std::printf("FAIL: the sharp source must be drawn in exactly one branch, the one "
                "that says so\n");
    return 1;
  }

  // The source is drawn there, and nowhere else.
  const auto at = src.find("case BackdropBase::Source:");
  int depth = 0;
  std::size_t end = at;
  for (std::size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
    } else if (src[i] == '}') {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  const auto branch = src.substr(at, end - at);
  if (branch.find("renderTexture(") == std::string::npos) {
    std::printf("FAIL: the Source branch draws nothing -- with the blur off there is "
                "no backdrop\n");
    return 1;
  }
  if (countOf(src, "renderTexture(comp") != 1) {
    std::printf("FAIL: the sharp source is drawn outside the branch that names it -- "
                "that is the one-frame flash of the desktop\n");
    return 1;
  }

  // The parked phase primes the blur: the first VISIBLE frame must find one ready.
  const auto prime = bodyOf(src, "void Overview::primeBackdrop()");
  if (prime.empty()) {
    std::printf("FAIL: primeBackdrop not found\n");
    return 1;
  }
  if (prime.find("ensureBlur(") == std::string::npos) {
    std::printf("FAIL: the parked phase does not prime the blur -- the first visible "
                "frame finds no blur and shows the sharp desktop\n");
    return 1;
  }

  std::printf("backdrop base: the blur or the dim, and the sharp source only with the "
              "blur off\n");
  return 0;
}

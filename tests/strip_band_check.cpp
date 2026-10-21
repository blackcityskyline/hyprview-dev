// The band leaves on the same number the cards leave on. It used to read exitT(), which
// rises on a close: the band dropped to zero on the first exit frame and then darkened
// back to full opacity while the strip slid away.

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

  const std::string strip = slurp(root + "/src/render/strip_view.cpp");
  if (strip.empty())
    return fail("src/render/strip_view.cpp not found");

  if (strip.find("bandE = opening() ? stripE : e;") == std::string::npos)
    return fail("the band no longer fades on the cards' eased value -- it must leave with them");

  if (strip.find("exitT(") != std::string::npos)
    return fail("the band reads exitT(), which rises on a close -- an exit-progress number is not a fade");

  std::printf("PASS: the strip band fades on the cards' own number\n");
  return 0;
}

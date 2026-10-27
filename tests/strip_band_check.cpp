// The band leaves on the same number the cards leave on: one curtain, one value. It read
// exitT(), which rises on a close, and later ran on a second clock while the backdrop ran
// on its own -- both left the band and the desktop as two separate actions.

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

  if (strip.find("bandE") != std::string::npos)
    return fail("the band carries a fade value of its own -- the curtain leaves on the cards' e");

  if (strip.find("cfg::colors.strip_band.get(e)") == std::string::npos ||
      strip.find("stripBandAlpha(cfg::strip.band_opacity.get(), e)") == std::string::npos)
    return fail("the band's colour and alpha no longer read the cards' eased value");

  if (strip.find("exitT(") != std::string::npos)
    return fail("the band reads exitT(), which rises on a close -- an exit-progress number is not a fade");

  std::printf("PASS: the strip band fades on the cards' own number\n");
  return 0;
}

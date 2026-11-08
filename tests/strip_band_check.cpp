// The band leaves on the same number the cards leave on: one curtain, one value. It read
// exitT(), which rises on a close, and later ran on a second clock while the backdrop ran
// on its own -- both left the band and the desktop as two separate actions. The alpha is the
// curtain's own fade: the slide is shorter than the band, and a ramp on the slide alone left
// the strip invisible while it was still half on screen.

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

  if (strip.find("cfg::colors.strip_band.get(fade)") == std::string::npos ||
      strip.find("stripBandAlpha(cfg::strip.band_opacity.get(), fade)") == std::string::npos)
    return fail("the band's colour and alpha no longer read the curtain's fade");

  // The fade is for an EMPTY desktop, where the desktop sharpens under the strip and the band
  // may dissolve. Over windows the overlay hands back to the compositor and the strip must be
  // gone by then: it travels all the way out instead of fading mid-screen.
  if (strip.find("m_landingEmpty ? render::stripFade(e) : 1.0") == std::string::npos)
    return fail("the strip fades over an occupied desktop -- there it has to slide all the way out");

  const std::string geo = slurp(root + "/src/session/geometry.cpp");
  if (geo.empty())
    return fail("src/session/geometry.cpp not found");
  if (geo.find("m_landingEmpty ? cfg::strip.slide_frac : 1.0") == std::string::npos)
    return fail("an occupied close stops the slide short of the edge -- the band would hang on screen");

  if (strip.find("exitT(") != std::string::npos)
    return fail("the band reads exitT(), which rises on a close -- an exit-progress number is not a fade");

  std::printf("PASS: the strip band fades on the cards' own number\n");
  return 0;
}

// The reported box must be the DRAWN one: Hyprland clips damage to getTexBox(), which
// ignores renderModif, so a modif'ed preview kept only the band over the window's spot.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "render/geometry.hpp"

namespace {

bool near(double got, double want) {
  return std::abs(got - want) < 0.01;
}

int fail(const char *what) {
  std::printf("FAIL: %s\n", what);
  return 1;
}

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv) {
  using hyprview::render::previewBox;
  using hyprview::render::windowFit;
  using hyprview::render::WindowFitInput;

  const std::string root = argc > 1 ? argv[1] : ".";

  // The artifact's scene: a 331x374 window at (687,388) in a 579x325 card 293px away.
  {
    const WindowFitInput in{394, 263, 579, 325, 687, 388, 331, 374, 0, 0, 1.0,
                            true, true, 0.0};
    const auto fit = windowFit(in);
    const auto box = previewBox(in, fit);
    const double w = 331 * fit.scale, h = 374 * fit.scale;
    if (!near(box.w, w) || !near(box.h, h))
      return fail("the box is not the fitted size -- the element would report the window's own box");
    if (!near(box.x, 394 + 579 / 2.0 - w / 2.0))
      return fail("the box is not centred on the card -- damage clips the preview to the window");
    if (!near(box.y, 263 + 325 / 2.0 - h / 2.0))
      return fail("the box is not centred on the card -- damage clips the preview to the window");
  }

  // Cover (no contain): the drawn box is the overlarge window, centred on the card.
  {
    const WindowFitInput in{100, 100, 400, 200, 0, 0, 800, 200, 0, 0, 1.0, true, false, 0.0};
    const auto fit = windowFit(in);
    const auto box = previewBox(in, fit);
    if (!near(box.w, 800) || !near(box.h, 200))
      return fail("a cover fit must report the over-covered window, not the card");
    if (!near(box.x, -100) || !near(box.y, 100))
      return fail("the cover box is not centred on the card");
  }

  // A scaled monitor and a monitor that does not start at 0,0: pos is global logical, so
  // the pixel box is ((box - monitor) * scale).
  {
    const WindowFitInput in{50, 20, 875, 500, 2500, 30, 700, 400, 1920, 0, 1.5, true, false, 0.0};
    const auto fit = windowFit(in);
    const auto box = previewBox(in, fit);
    const double px = (box.x - 1920) * 1.5, py = (box.y - 0) * 1.5;
    if (!near(px, 50) || !near(py, 20))
      return fail("the box does not land on the card under a fractional monitor scale");
    if (!near(box.w * 1.5, 875) || !near(box.h * 1.5, 500))
      return fail("the box size is not in logical units");
  }

  // The draw path must not put a render modif back.
  const auto src = slurp(root + "/src/render/window_content.cpp");
  if (src.empty())
    return fail("cannot read src/render/window_content.cpp");
  if (src.find("previewBox(") == std::string::npos)
    return fail("the preview does not report the drawn box -- see previewBox()");
  if (src.find("RMOD_TYPE_") != std::string::npos ||
      src.find("renderModif =") != std::string::npos)
    return fail("the preview draws through a renderModif again -- damage then clips it "
                "to the window's real spot");

  std::printf("preview box: the drawn box, centred on the card\n");
  return 0;
}

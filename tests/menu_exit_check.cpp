// The button row leaves with the overview, over the edge it hugs. It used to pin its own
// clock on close, so it vanished in one frame, and the per-frame hover then re-opened it on
// the way out: the bar blinked before the overlay left. The slide is the fix, so it is the
// rule this file pins -- a fade or a shrink in place is the regression.

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

  const std::string life = slurp(root + "/src/session/lifecycle.cpp");
  if (life.empty())
    return fail("src/session/lifecycle.cpp not found");

  const auto menuAt = life.find("double Overview::menuProgress() const");
  if (menuAt == std::string::npos)
    return fail("menuProgress() is gone");
  if (life.substr(menuAt, 320).find("m_menuExit * std::clamp(m_place.progress.raw()") ==
      std::string::npos)
    return fail("the exit is not on the closing timeline -- its own clock pins it shut");

  const auto visAt = life.find("void Overview::updateMenuVisibility()");
  if (visAt == std::string::npos)
    return fail("updateMenuVisibility() is gone");
  if (life.substr(visAt, 160).find("!opening()") == std::string::npos)
    return fail("hover still re-opens the row during the close -- it blinks back before leaving");

  const auto closeAt = life.find("void Overview::close()");
  const auto hardAt = life.find("void Overview::hardClose()", closeAt);
  if (closeAt == std::string::npos || hardAt == std::string::npos)
    return fail("close() or hardClose() is gone");
  const auto close = life.substr(closeAt, hardAt - closeAt);
  const auto record = close.find("m_menuExit = std::clamp(menuProgress()");
  if (record == std::string::npos)
    return fail("close() does not record how much of the row was up");
  if (record > close.find("m_life.endEntry()"))
    return fail("close() records the row after endEntry -- it reads the exit, not the bar");

  const std::string paint = slurp(root + "/src/render/painter.cpp");
  if (paint.empty())
    return fail("src/render/painter.cpp not found");
  if (paint.find("render::menuSlide(box.panel") == std::string::npos)
    return fail("the row no longer slides -- it fades in place again");

  std::printf("PASS: the button row slides off its edge with the close\n");
  return 0;
}

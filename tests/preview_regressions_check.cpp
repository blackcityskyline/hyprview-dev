// Two regressions that both looked like "the preview is missing" and had nothing to do with
// each other. Both are a deleted line that has to stay deleted-in-reverse, which is exactly the
// kind of thing no unit test reaches and a grep does.
//
// 1. Windows were held in the source's alive list only if they got a TILE. The strip draws every
//    workspace's windows, and a thumbnail there resolves through the same list, so every card
//    but the current workspace's showed bare wallpaper. "Belongs to the run" and "is held so it
//    can be drawn" are two different questions and the list was answering the first where it was
//    asked the second.
//
// 2. The window's own opacity was pinned to 1 by bab6eb3 ("show windows opaque"), on the
//    argument that depth comes from size and overlap instead. That is a different claim from
//    "the user never set an opacity rule", and a window with active_opacity or inactive_opacity
//    below 1 stopped looking like itself on every card. windowRealAlpha() went with it.
//
// Both are greps on purpose: the first lives in a file ctest cannot compile without a
// compositor, and the second is the absence of a line.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string slurp(const std::string &p) {
  std::ifstream f(p);
  assert(f && ("cannot open " + p).c_str());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int failures = 0;

void want(bool ok, const char *what) {
  if (ok)
    return;
  ++failures;
  std::printf("FAIL %s\n", what);
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto src = slurp(root + "/src/model/window_source.cpp");
  const auto content = slurp(root + "/src/render/window_content.cpp");

  // --- 1: every window is held, only the belonging ones are returned ------------------------
  // Order is the whole rule. Holding after the filter is what it was, and the filter is what
  // it must not be: the two lines have to read "hold, then maybe skip".
  {
    const auto hold = src.find("m_alive.push_back(w);");
    const auto gate = src.find("if (!m_host.belongs(w))");
    want(hold != std::string::npos && gate != std::string::npos,
         "enumerate() must hold a window and then decide whether it belongs");
    want(hold < gate, "the window is HELD BEFORE the membership test, not after it");
  }

  // --- 2: a window's own opacity reaches the surface pass -------------------------------------
  {
    want(content.find("windowRealAlpha") != std::string::npos,
         "the real-opacity helper is gone again");
    want(content.find("data.fadeAlpha = windowRealAlpha(w, mon);") != std::string::npos,
         "fadeAlpha is the window's own opacity, not a constant");
    want(content.find("data.fadeAlpha = 1.F") == std::string::npos &&
             content.find("data.fadeAlpha = 1.F;") == std::string::npos,
         "fadeAlpha is pinned to opaque");
  }

  if (failures) {
    std::printf("preview_regressions: %d violation(s)\n", failures);
    return 1;
  }
  std::puts("preview_regressions: previews are held and they keep their opacity");
  return 0;
}
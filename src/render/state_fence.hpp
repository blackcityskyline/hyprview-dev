#pragma once

// The GL-state fence, split out so it can be unit-tested without a GL header.
//
// What the fence guards is "the state we change here is put back": the read of live GL state
// needs a current context and a driver sync, so it stays in gl_util.hpp. But the COMPARISON
// -- "what changed between two readings" -- is pure arithmetic on POD, so it lives here and
// has no GL dependency at all, which is the whole point: the invariant is testable in
// isolation, observation is not.

#include <string>

namespace gloview::render {

struct GLState {
  bool blend = false;
  // The scissor ENABLE bit, not just its box. Hyprland's scissor(nullptr) does not reset the
  // box to the full screen -- it DISABLES the test (OpenGL.cpp:2631, setCapStatus), so the
  // box keeps whatever value the last region left. Reading the box alone therefore reports
  // a leak where there is none, and misses the one that matters: a pass that leaves the test
  // ENABLED on a small box clips everything the compositor draws after it.
  bool scissorTest = false;
  // The RGB pair only. GLES has no GL_BLEND_SRC/DST -- only the _RGB and _ALPHA variants --
  // and Hyprland sets all four at once with glBlendFunc (OpenGL.cpp:984, premultiplied), so
  // reading the RGB pair is the reading that matches what anyone actually calls.
  int srcRgb = 0, dstRgb = 0;
  int scissor[4] = {0, 0, 0, 0};
  int fbo = 0;
};

// What changed between two readings, as a comma-separated list, empty when nothing did.
// PURE so a regression in one call site reddens the whole test file. The bug class this
// targets -- "left blend enabled, or called glDisable instead of going through the helper" --
// is invariant under the exact numbers, so this is tested by shape, not by value.
[[nodiscard]] inline std::string glStateDelta(const GLState &a, const GLState &b) {
  std::string out;
  const auto add = [&out](const char *what) {
    if (!out.empty())
      out += ",";
    out += what;
  };
  if (a.blend != b.blend)
    add("blend");
  else if (a.srcRgb != b.srcRgb || a.dstRgb != b.dstRgb)
    add("blend_func");
  // An enable change is the leak; a box change only counts while the test is ON, because a
  // disabled scissor clips nothing however narrow its box still reads.
  if (a.scissorTest != b.scissorTest)
    add("scissor_test");
  else if (a.scissorTest)
    for (int i = 0; i < 4; ++i)
      if (a.scissor[i] != b.scissor[i]) {
        add("scissor_box");
        break;
      }
  if (a.fbo != b.fbo)
    add("fbo");
  return out;
}

} // namespace gloview::render

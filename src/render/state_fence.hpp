#pragma once

// The GL-state fence: the COMPARISON is pure arithmetic on POD and lives here so a check can
// test it; the observation needs a current context and stays in gl_util.hpp.

#include <string>

namespace gloview::render {

struct GLState {
  bool blend = false;
  // The scissor ENABLE bit, not just its box: scissor(nullptr) DISABLES the test and does not
  // reset the box (OpenGL.cpp:2631), so the box alone reports leaks that are not there.
  bool scissorTest = false;
  // The RGB pair only: GLES has no GL_BLEND_SRC/DST, and Hyprland sets all four (OpenGL.cpp:984).
  int srcRgb = 0, dstRgb = 0;
  int scissor[4] = {0, 0, 0, 0};
  int fbo = 0;
};

// What changed between two readings, comma-separated, empty when nothing did. PURE: one call
// site regressing reddens the whole test file.
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
  // An enable change is the leak; a box change only counts while the test is ON.
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

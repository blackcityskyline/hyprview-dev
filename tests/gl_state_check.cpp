// The GL-state fence's comparison, and the half of GL discipline no test and no trace
// covered: that the state a pass changes is put back.
//
// The READ needs a live context, so it cannot be tested here. The COMPARISON can, and it is
// the half that carries the invariant -- "these two readings of the same state are equal" is
// a claim about arithmetic, and arithmetic is what this file is for.
//
// The bug class is invariant under the specific numbers: a pass that left blend enabled, or
// that drove scissor through a raw glDisable instead of the helper, shows up here as a
// non-empty delta no matter what the values were. So the test is about WHICH field is
// named, not about any particular state.

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/render/state_fence.hpp"

using hyprview::render::GLState;
using hyprview::render::glStateDelta;

namespace {

// A plausible mid-pass state. The exact numbers are irrelevant on purpose: the fence has to
// work for whatever the compositor handed us, so the test picks one and moves on.
GLState baseline() {
  GLState s;
  s.blend = true;
  s.srcRgb = 1;     // GL_ONE
  s.dstRgb = 771;   // GL_ONE_MINUS_SRC_ALPHA -- what Hyprland sets (OpenGL.cpp:984)
  s.scissorTest = true;
  s.scissor[0] = 0;
  s.scissor[1] = 0;
  s.scissor[2] = 1354;
  s.scissor[3] = 756;
  s.fbo = 17;
  return s;
}

void eq(const std::string &got, const char *want, const char *what) {
  if (got != want)
    std::fprintf(stderr, "  %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want);
  assert(got == want);
}

} // namespace

int main() {
  // An unchanged pass is silent. This is the assertion that matters most, because it is the
  // one whose failure mode is invisible: a fence that always fired would train everyone to
  // ignore it, and a fence that never fired would look identical to a clean pass.
  eq(glStateDelta(baseline(), baseline()), "", "a pass that restores everything reports nothing");

  // Each leak, named after the state it leaks, so the log line says which one to look for.
  {
    auto s = baseline();
    s.blend = false;
    eq(glStateDelta(baseline(), s), "blend", "blend left off");
  }
  {
    auto s = baseline();
    s.blend = true;
    s.srcRgb = 770;
    eq(glStateDelta(baseline(), s), "blend_func", "the blend factors changed under it");
  }
  {
    auto s = baseline();
    s.scissorTest = false;
    eq(glStateDelta(baseline(), s), "scissor_test", "scissor left disabled");
  }
  {
    auto s = baseline();
    s.scissor[3] = 0;
    eq(glStateDelta(baseline(), s), "scissor_box", "scissor left on a narrower box");
  }
  // THE case this arm exists for. Hyprland's scissor(nullptr) DISABLES the test and leaves
  // the box reading whatever the last region left (OpenGL.cpp:2631, setCapStatus), so the
  // fence saw a "changed box" on a scissor that clips nothing and called it a leak -- 700 of
  // them, on a frame that was fine. A disabled scissor is not a narrow scissor.
  {
    auto s = baseline();
    s.scissorTest = false;
    s.scissor[2] = 167;
    s.scissor[3] = 94;
    eq(glStateDelta(baseline(), s), "scissor_test",
       "disabled with a stale box: the enable bit is the leak, not the box");
  }
  // And the mirror: turned back ON over a narrow box, with the test on both sides. Only
  // then does the box matter.
  {
    auto narrow = baseline();
    narrow.scissor[2] = 167;
    narrow.scissor[3] = 94;
    auto off = narrow;
    off.scissorTest = false;
    eq(glStateDelta(off, narrow), "scissor_test", "enabling over a narrow box is still the test");
    eq(glStateDelta(narrow, narrow), "", "and two identical narrow boxes are not a leak");
  }
  {
    auto s = baseline();
    s.fbo = 0;
    eq(glStateDelta(baseline(), s), "fbo", "a private framebuffer left bound");
  }

  // Symmetry: comparing the wrong way round must find the same leaks. An asymmetric
  // comparison is the classic way this kind of check ends up green while broken, because it
  // reports nothing in exactly the direction the caller happens to use.
  {
    auto s = baseline();
    s.blend = false;
    eq(glStateDelta(s, baseline()), "blend", "the same leak, read the other way round");
  }

  // Two at once, named together -- and in a fixed order, so the field the fix touched is the
  // first thing a reader sees. A pass that both unbound an FBO and left scissor off is a
  // different bug from either alone.
  {
    auto s = baseline();
    s.blend = false;
    s.scissorTest = false;
    eq(glStateDelta(baseline(), s), "blend,scissor_test", "two leaks at once, in order");
  }

  // Blend and its factors are one fact, not two: blend off already subsumes them, and
  // reporting both would say the same thing twice.
  {
    auto s = baseline();
    s.blend = false;
    s.srcRgb = 770;
    s.dstRgb = 770;
    eq(glStateDelta(baseline(), s), "blend", "blend off subsumes its factors");
  }

  // A default-constructed state against another default-constructed one: the fence runs
  // before the context exists too, and two zeroes must read as "nothing changed" rather
  // than as a scissor leak at the origin.
  eq(glStateDelta(GLState{}, GLState{}), "", "two empty readings compare equal");

  std::puts("glStateDelta: silent when restored, names each leak, scissor reads the test not just the box");
  return 0;
}

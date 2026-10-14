#pragma once

// in: LOGICAL, except projectBoxToTarget: PIXEL, so pre-scale
#include <algorithm>
#include <cmath>

#include <GLES3/gl32.h>
#include <hyprlang.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include "../debug/log.hpp"
#include "../layout.hpp"
#include "state_fence.hpp"

namespace gloview {

// round(): pixman truncates the border's scissor
inline CBox pxb(const CBox &b, double s) {
  return CBox{b.x * s, b.y * s, b.w * s, b.h * s}.round();
}
inline CBox pxb(const LRect &r, double s) {
  return CBox{r.x * s, r.y * s, r.w * s, r.h * s}.round();
}
// truncate, never round: must match the shader's radius
inline int pxr(double round, double s) { return static_cast<int>(round * s); }

inline int outerRoundPx(double round, double borderSize, double roundingPower,
                        double scale) {
  const double correction =
      borderSize * (M_SQRT2 - 1.0) * std::max(2.0 - roundingPower, 0.0);
  return static_cast<int>((round + borderSize - correction) * scale);
}

inline int clampRound(int round, double w, double h) {
  return static_cast<int>(
      std::clamp(static_cast<double>(round), 0.0, std::min(w, h) * 0.5));
}

// --- GL state fence ---------------------------------------------------------------------
//
// The half of GL discipline no test and no trace covered: that the state we change is put
// back. `scan_draw_sites` catches a raw draw call outside the helpers, and the static side
// of the rule; nothing caught the other half, and two real defects were found by READING
// CODE (backdrop leaving blend enabled, blur calling raw glDisable) rather than by any
// check. The comment in the render audit that said "verified" was, for these two, a claim
// with nothing behind it.
//
// A mock cannot help here: it proves which functions were called, not what the driver ended
// up in. So read it back instead -- glGet is observation, not substitution. The cost is a
// pipeline sync, which is why every call is behind debug_trace and why paint takes this
// twice rather than at every site.
//
// GLState and glStateDelta are split into state_fence.hpp so the comparison -- the part
// that carries the invariant -- can be unit-tested without pulling in a GL header.

// Read the live state. Needs a current context; a reader outside a pass sees whatever the
// last pass left, which is exactly the point -- the delta is read against a fence taken
// inside the same frame.
[[nodiscard]] inline render::GLState readGLState() {
  render::GLState s;
  GLboolean on = GL_FALSE;
  glGetBooleanv(GL_BLEND, &on);
  s.blend = on == GL_TRUE;
  glGetIntegerv(GL_BLEND_SRC_RGB, &s.srcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &s.dstRgb);
  glGetBooleanv(GL_SCISSOR_TEST, &on);
  s.scissorTest = on == GL_TRUE;
  glGetIntegerv(GL_SCISSOR_BOX, s.scissor);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
  return s;
}

// A named reading, once per frame like every RING| site. The volume is the norm here under
// an explicit debug flag, and it is what makes the check mean anything: a once-per-process
// heartbeat cannot tell a reader that the fence is running, and "silence" then has to mean
// either "clean" or "not looking" with nothing to tell them apart.
inline void glStateFence(const char *where) {
  if (!debug::trace::enabled())
    return;
  const auto s = readGLState();
  const auto box = [](const render::GLState &st) {
    return std::to_string(st.scissor[0]) + "," + std::to_string(st.scissor[1]) + "," +
           std::to_string(st.scissor[2]) + "," + std::to_string(st.scissor[3]);
  };
  debug::trace::emitState(
      std::string("GL|at=") + where + "|blend=" + (s.blend ? "1" : "0") +
      "|src=" + std::to_string(s.srcRgb) + "|dst=" + std::to_string(s.dstRgb) +
      "|scissorTest=" + (s.scissorTest ? "1" : "0") + "|scissor=" + box(s) +
      "|fbo=" + std::to_string(s.fbo));
}

// The other end of the pair paint() brackets itself with: what came back different is the
// leak. Reported ONLY when something differs -- the entry line above already says a frame
// happened, so silence here means the frame was clean.
inline void glStateReport(const render::GLState &before, const char *where) {
  if (!debug::trace::enabled())
    return;
  const render::GLState after = readGLState();
  const auto delta = render::glStateDelta(before, after);
  if (!delta.empty()) {
    // The VALUES, not just the names. "scissor" alone says a field changed and leaves the
    // reader to re-add instrumentation to find which call site did it; the before/after pair
    // usually identifies the offender on sight -- a scissor left at one card's box is that
    // card's draw, and the leak is somewhere in it.
    const auto box = [](const render::GLState &s) {
      return std::to_string(s.scissor[0]) + "," + std::to_string(s.scissor[1]) + "," +
             std::to_string(s.scissor[2]) + "," + std::to_string(s.scissor[3]);
    };
    debug::trace::emitState(std::string("GL|leak|at=") + where + "|what=" + delta +
                            "|blend=" + (before.blend ? "1" : "0") + "->" +
                            (after.blend ? "1" : "0") + "|scissorTest=" +
                            (before.scissorTest ? "1" : "0") + "->" +
                            (after.scissorTest ? "1" : "0") + "|scissor=" + box(before) +
                            "->" + box(after) + "|fbo=" + std::to_string(before.fbo) +
                            "->" + std::to_string(after.fbo));
    return;
  }
}

inline CHyprColor argb(Hyprlang::INT raw, double alphaMul = 1.0) {
  const auto a = static_cast<double>((raw >> 24) & 0xFF) / 255.0;
  const auto r = static_cast<double>((raw >> 16) & 0xFF) / 255.0;
  const auto g = static_cast<double>((raw >> 8) & 0xFF) / 255.0;
  const auto b = static_cast<double>(raw & 0xFF) / 255.0;
  return CHyprColor(r, g, b, a * std::clamp(alphaMul, 0.0, 1.0));
}

// Hyprland asserts on a non-positive box in every immediate-mode render entry point,
// and that assert is a crash inside the compositor -- the whole session goes, it is
// not a no-op:
//   OpenGL.cpp:1071  renderRect (via renderRectWithDamageInternal)
//   OpenGL.cpp:2163  renderBorder
//   OpenGL.cpp:2341  renderRoundedShadow
// pxb() rounds, so a box under half a pixel becomes zero even while non-empty.
//
// ONE predicate, in the shared header, instead of a guard per call site. strip_view
// had its own and it tested `width < 0`, which hands a zero-sized box straight to the
// assert the helper exists to prevent; the flight's shadow had no guard at all and
// cost a session on 2026-10-10 (crash report 45823).
inline bool drawable(const CBox &b) { return b.w > 0 && b.h > 0; }
inline bool drawable(const LRect &r, double s) { return drawable(pxb(r, s)); }

// Logs instead of dropping silently: "why is there no rect here" is unanswerable
// after the fact without the numbers.
inline void safeRenderRect(const char *tag, const CBox &b, const CHyprColor &col,
                           Render::GL::CHyprOpenGLImpl::SRectRenderData opts) {
  if (!drawable(b)) {
    debug::dbg(std::string("NEG box @") + tag + ": " + std::to_string(b.w) + "x" +
               std::to_string(b.h) + " at " + std::to_string(b.x) + "," +
               std::to_string(b.y));
    return;
  }
  Render::GL::g_pHyprOpenGL->renderRect(b, col, opts);
}

// STROKE, never a filled rect grown by borderSize
//
// renderBorder scissors to each region it draws and NEVER clears it afterwards
// (OpenGL.cpp:2233-2239) -- renderRect ends in scissor(nullptr) (OpenGL.cpp:1144
// region block) and renderBorder does not. So the last ring drawn in a pass leaves the
// scissor at its own box, and everything the compositor draws next is clipped to it. The
// fence found this with the value in hand: scissor=0,0,1366,768 -> 785,41,167,94, which
// is a strip card's box.
//
// Restored through the API, never raw GL, for the reason backdrop.cpp already writes down:
// a raw glEnable puts GL back on while Hyprland's cap cache still says off, and the next
// scissor(nullptr) then early-returns (OpenGL.cpp:2620) and clips the rest of the frame.
inline void restoreScissorAfterBorder() {
  Render::GL::g_pHyprOpenGL->scissor(nullptr);
}

inline void strokeRing(const LRect &lb, double s, const CHyprColor &col,
                       int size, int round, float roundPow) {
  if (size < 1 || !drawable(lb, s))
    return;
  Render::GL::g_pHyprOpenGL->renderBorder(
      pxb(lb, s), Config::CGradientValueData(col),
      {.round = pxr(round, s),
       .roundingPower = roundPow,
       .borderSize = size,
       .a = 1.0F,
       .outerRound = outerRoundPx(round, size, roundPow, s)});
  restoreScissorAfterBorder();
}

// renderWindowLive over-covers by this much so sub-pixel snapping cannot open a
// seam; the content radius follows it out. PIXELS, not logical: window_content.cpp adds it
// straight to a pixel width, so scaling it here gave the radius 3px of cover against a box
// over-covering 1.5, and the content arc sat half that inside the ring's outer edge.
inline constexpr float kSeamPadPx = 1.5F;

// The corner radius a tile's CONTENT must carry so its arc lands on the ring's
// outer edge, from the ring's OWN inputs. +1: Hyprland subtracts one.
// overCovers false = a contained window, whose arc does not walk outward.
inline int contentRadiusPx(int round, int width, float roundPow, double scale,
                           bool overCovers = true) {
  return pxr(round, scale) + static_cast<int>(std::lround(width * scale)) +
         (overCovers ? static_cast<int>(kSeamPadPx) : 0) + 1;
}

inline void strokeRingPx(const CBox &b, const CHyprColor &col, float alpha,
                         int round, float roundPow) {
  if (!drawable(b))
    return;
  Render::GL::g_pHyprOpenGL->renderBorder(
      b, Config::CGradientValueData(col),
      {.round = round,
       .roundingPower = roundPow,
       .borderSize = 2,
       .a = alpha,
       .outerRound = outerRoundPx(round, 2, roundPow, 1.0)});
  restoreScissorAfterBorder();  // renderBorder leaves the scissor on its own last rect
}

inline CHyprColor argb(const CHyprColor &c, double alphaMul = 1.0) {
  return CHyprColor(c.r, c.g, c.b, c.a * std::clamp(alphaMul, 0.0, 1.0));
}

inline void dropShadow(const LRect &lb, double s, const CHyprColor &col,
                       double dy, double range, float alpha, int round,
                       float roundPow) {
  if (!drawable(lb, s))
    return;
  Render::GL::g_pHyprOpenGL->renderRoundedShadow(
      pxb(LRect{lb.x, lb.y + dy, lb.w, lb.h}, s), pxr(round, s), roundPow,
      static_cast<int>(range * s), Config::CGradientValueData(col), alpha);
}

}

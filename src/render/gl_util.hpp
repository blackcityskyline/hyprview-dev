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
// The half of GL discipline nothing covered: that the state we change is put back.
// scan_draw_sites catches a raw draw call outside the helpers, the static half; two real
// defects (backdrop leaving blend enabled, blur calling raw glDisable) were found by reading
// code instead. A mock proves which functions were called, not what the driver ended in, so
// read it back: glGet is observation, not substitution. Cost is a pipeline sync, hence every
// call is behind debug_trace and paint brackets itself twice rather than at every site.

// Needs a current context. A reader outside a pass sees whatever the last left; the delta is
// read against a fence taken inside the same frame, which is the point.
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

// A named reading, once per frame like every RING| site. Volume is the norm under an explicit
// debug flag: a once-per-process heartbeat cannot tell a reader the fence is running, and
/// silence then means either "clean" or "not looking" with nothing to tell them apart.
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
// leak. Reported only when something differs, so silence here means the frame was clean.
inline void glStateReport(const render::GLState &before, const char *where) {
  if (!debug::trace::enabled())
    return;
  const render::GLState after = readGLState();
  const auto delta = render::glStateDelta(before, after);
  if (!delta.empty()) {
    // The VALUES, not just the names: a scissor left at one card's box is that card's draw.
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

// Hyprland asserts on a non-positive box in every immediate-mode render entry point, and that
// assert is a crash inside the compositor -- the session goes, it is not a no-op:
//   OpenGL.cpp:1071 renderRect   OpenGL.cpp:2163 renderBorder   OpenGL.cpp:2341 shadow
// pxb() rounds, so a box under half a pixel becomes zero even while non-empty. ONE predicate
// here rather than a guard per call site: strip_view's own tested `width < 0`, handing a
// zero box straight to the assert the helper exists to prevent, and the flight's shadow had
// no guard at all and cost a session on 2026-10-10.
inline bool drawable(const CBox &b) { return b.w > 0 && b.h > 0; }
inline bool drawable(const LRect &r, double s) { return drawable(pxb(r, s)); }

// Logs instead of dropping silently: after the fact the numbers are gone.
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

// STROKE, never a filled rect grown by borderSize. renderBorder scissors to each region and
// NEVER clears it (OpenGL.cpp:2233-2239) while renderRect ends in scissor(nullptr), so the
// last ring drawn leaves its own scissor and clips everything composited after it.
// Restored through the API, never raw GL: a raw glEnable puts GL back on while Hyprland's cap
// cache still says off, and the next scissor(nullptr) early-returns (OpenGL.cpp:2620).
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

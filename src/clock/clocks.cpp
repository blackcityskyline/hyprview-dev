#include <algorithm>
#include <chrono>

#include "../clock/curves.hpp"
#include "../anim.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"

namespace gloview {

// Every clock here is pure linear progress; shaping lives in curves.cpp and
// durations are re-read from config on every call.
// The ladder spread is the user's, capped at 1: Stagger is the ceiling,
// StaggerPerTile grows it with the tile count and must not push past it.
double staggered(double base, int i, int n) {
  return anim::stagger(base, i, n,
                       std::min(anim::param(anim::Param::Stagger),
                                anim::param(anim::Param::StaggerPerTile) * n));
}

// imperative rule > schema key > default; off -> 1ms, speed divides base
anim::Id Overview::entryLeaf() const {
  if (m_view.expoFlip() != 0)
    return m_view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return m_view.wsSlideDir() != 0 ? anim::Id::WsEnter : anim::Id::Appear;
}

anim::Id Overview::ghostLeaf() const {
  if (m_view.expoFlip() != 0)
    return m_view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return m_view.wsSlideDir() != 0 ? anim::Id::WsExit : anim::Id::Appear;
}

anim::Id Overview::glideLeaf() const {
  if (m_view.jumpMode()) return opening() ? anim::Id::JumpIn : anim::Id::JumpOut;
  if (m_view.expoFlip() != 0)
    return m_view.expoFlip() > 0 ? anim::Id::ExpoIn : anim::Id::ExpoOut;
  return carouselMode() ? anim::Id::CarouselStep : anim::Id::Glide;
}

// How far the close has run, 0 -> 1. m_place.progress.raw() runs DOWN on a close (1 open -> 0 closed);
// read forwards it looks exactly like no animation. Every exit fraction reads it here so
// the shrink, the fade and the clock cannot disagree about where the close is.
double Overview::exitT() const { return m_place.progress.exit(opening()); }

// WINDOWED exit: hold the backdrop until our own tiles are done AND Hyprland has spent the
// forced full frames close() asked for -- those are the frames it rebuilds its blur in.
// Asking when the windows reached their goal geometry does NOT work: in the plain
// open/close the real windows never move (the plugin draws previews and hides them), so
// that test is true on frame 0 and holds nothing.
bool Overview::backdropSettled() const {
  const auto m = m_monitor.lock();
  return m_place.clocks.tiles.done(animDuration()) && (!m || m->m_forceFullFrames <= 0);
}

// The backdrop is part of the SAME gesture as the tiles, the strip and the chrome: it
// rides the open/close animation itself. That is the whole fix for the queued feel --
// separate clocks made the strip arrive first and then the backdrop set about arriving,
// with nothing joining them up. The gap a private clock was introduced for is handled by
// the BASE, not by a timeline: with windows present the base is the blurred cache, so the
// screen is blurred on frame 0 and the ramp has nothing to reveal anyway.
//
// WINDOWED exit is the one exception: the backdrop must stay until Hyprland has rebuilt
// its blur. Fading out before that is what exposed a sharp desktop mid-flight.
double Overview::blurAlpha() const {
  if (!anim::backdropFades(opening(), m_landingEmpty))
    return 1.0;
  return std::clamp(eased(), 0.0, 1.0);
}

double Overview::eased() const { return m_place.eased(); }

double Overview::animDuration() const { return m_place.masterDuration(); }

// CLOSE rides m_place.progress.raw() DOWN: tiles then match the collapsing chrome.
// Read m_place.clocks.tileMs, not m_place.recipes.glide.ms: a glideLeaf() flip jumps progress.
double Overview::tileProgress(int i) const {
  return m_place.tileProgress(i, m_view, opening(), m_scene);
}

double Overview::entryProgress(int i) const {
  return m_place.entryProgress(i, m_scene);
}

double Overview::entryNow(const model::Tile &t, int i) const {
  return m_place.entryNow(t, i, m_scene);
}

// The one call into Placement for "where is tile i". Everything Placement needs that it
// cannot know -- the monitor it is laid out in, and whether this tile has somewhere to
// land -- arrives in the frame, because a clock cannot answer either on its own.
clock::Placement::Frame Overview::frame() const {
  const auto m = m_monitor.lock();
  const PHLWORKSPACE land = m_workspace.lock();
  return {m_view, m_scene, m_windows,
          land ? model::GroupRef{reinterpret_cast<std::uintptr_t>(land.get()), 0}
               : model::GroupRef{},
          opening(), m ? m->m_size.x : 0.0, m ? m->m_size.y : 0.0};
}

LRect Overview::currentBox(const model::Tile &t, int i) const {
  return m_place.box(t, i, frame());
}

void Overview::updateAnimation() {
  // Placement::resolve, NOT recipes.resolve: this is also where the frame's eased()
  // and master duration are computed. Calling the recipe resolver here leaves them
  // at zero for the whole frame -- an overlay that draws everything at alpha 0.
  m_place.resolve(m_view, opening(), carouselMode());

  const double dur = animDuration();
  // stall guard: rewind every clock past a render hole
  const auto now = std::chrono::steady_clock::now();
  if (m_lastAnimTick.time_since_epoch().count() != 0) {
    const double gapMs =
        std::chrono::duration<double, std::milli>(now - m_lastAnimTick).count();
    if (gapMs > 100.0) {
      const double glideMs = glideDur();
      m_place.clocks.master.compensateStall(gapMs, dur);
      m_place.clocks.tiles.compensateStall(gapMs, glideMs);
      m_place.clocks.rebuild.compensateStall(gapMs, std::max(m_place.recipes.entry.ms, m_place.recipes.ghost.ms));
      m_place.clocks.strip.compensateStall(gapMs, anim::cfg(anim::Id::StripStep).ms);
      if (m_newCardAnim)
        m_place.clocks.newCard.compensateStall(gapMs, newCardDur());
      if (m_drag.lifted())
        m_place.clocks.dragLift.compensateStall(gapMs, anim::cfg(anim::Id::Lift).ms);
      m_fx.eachSwap([&](auto &fx) { fx.clock.compensateStall(gapMs, fx.ms); });
    }
  }
  m_lastAnimTick = now;

  if (m_drag.armed() && !m_drag.lifted()) {
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        now - m_drag.holdFrom()).count();
    if (elapsedMs >= static_cast<double>(cfg::behavior.hold_lift_ms.get())) {
      m_drag.setLifted(true);
      m_place.clocks.dragLift.begin();
      debug::trace::animStart("drag_lift", "lift", "", m_place.recipes.lift.ms, m_place.recipes.lift.curve.c_str());
      damage();
    } else {
      static auto lastHoldDamage = std::chrono::steady_clock::time_point{};
      const auto sinceLast = std::chrono::duration<double, std::milli>(
          now - lastHoldDamage).count();
      if (sinceLast >= 50.0) {
        damage();
        lastHoldDamage = now;
      }
    }
  }

  const auto st = anim::cfg(anim::Id::StripStep);
  if (!m_place.clocks.strip.done(st.ms))
    m_scroll.advance(curves::eval(st.curve, m_place.clocks.strip.raw(st.ms)));
  else
    m_scroll.advance(1.0);

  if (!m_fx.ghosts().empty() && m_place.clocks.rebuild.done(m_place.recipes.ghost.ms))
    m_fx.clearGhosts();
  // entries must settle explicitly or they keep evaluating curves forever
  if (m_place.clocks.rebuild.done(m_place.recipes.entry.ms) &&
      std::any_of(m_scene.tiles().begin(), m_scene.tiles().end(),
                  [](const model::Tile &t) { return t.landed < 1.0; }))
    m_scene.settleEntry();

  const auto pulse = anim::cfg(anim::Id::Pulse);
  m_fx.eachPulse([&](auto &p) {
    if (p.w.expired())
      return;
    p.p += std::min(34.0, std::chrono::duration<double, std::milli>(now - p.last)
                              .count()) /
           pulse.ms;
    p.last = now;
  });
  // Pruned on each pulse's own accumulated fraction: a live config change must not
  // shorten a ring that is already running.
  m_fx.dropPulse(
      [](const model::WinPulse &p) { return p.w.expired() || p.p >= 1.0; });

  if (m_place.clocks.rebuild.done(m_place.recipes.ghost.ms) && m_place.clocks.rebuild.done(m_place.recipes.entry.ms)) {
    m_view.setWsSlideDir( 0);
    m_view.setExpoFlip( 0);
    if (m_life.consumeJumpClosing()) {
      close();
    }
  }

  // prune on each flight's own clock; live config would cut landings short
  const auto flightEnd = std::chrono::steady_clock::now();
  m_fx.dropSwap([&](const model::SwapFX &fx) {
    const bool done = fx.win.expired() || fx.clock.raw(fx.ms) >= 1.0;
    // Measured, not configured: the harness compares the two, and a flight that ran
    // long or short is exactly what it is looking for.
    if (done && fx.traceSeq)
      debug::trace::animEnd(fx.traceSeq, std::chrono::duration<double, std::milli>(
                                          flightEnd - fx.traceStart)
                                          .count());
    return done;
  });

  const double t = m_place.clocks.master.raw(dur);
  m_place.progress.fromMaster(t, opening());
  if (m_newCardAnim && m_place.clocks.newCard.done(newCardDur())) {
    m_newCardAnim = false;
    m_newCardId = 0;
  }
  // NEVER zero m_place.progress.raw(): a transparent frame before deactivate() flashes.
  // Clamp as soon as the flight ends, NOT together with the deactivate: the exit ramp
  // outlives the close animation, and at exactly 0 the overlay paints nothing, Hyprland
  // has no damage to serve, and the ramp runs on with no frames to show it (measured:
  // 769ms of ticks with no render).
  if (!opening() && t >= 1.0 && m_place.progress.raw() <= 0.0)
    m_place.progress.firstFrame();
  // The overlay outlives the close animation by exactly the windowed-exit hold: that is
  // what keeps the backdrop covering the screen until Hyprland's own blur has taken over
  // on the windows that have just landed. Lingering past that would leave a transparent
  // overlay over a sharp desktop, which is the gap all over again.
  if (!opening() && t >= 1.0 && m_place.clocks.tiles.done(dur) && backdropSettled()) {
    m_life.requestDeactivate();
    debug::trace::life("requestDeactivate", m_life.describe());
  }
}

bool Overview::animBusy() const {
  return active() &&
         (secondaryAnimsActive() || !m_place.clocks.tiles.done(glideDur()) ||
          m_newCardAnim || m_drag.lifted() || !m_fx.swapfx().empty() ||
          m_life.jumpClosing() || m_life.deactivating() ||
          (opening() && m_place.progress.raw() < 1.0) || (!opening() && m_place.progress.raw() > 0.0));
}

double Overview::newCardScale() const {
  if (!m_newCardAnim)
    return 1.0;
  return curves::eval(anim::cfg(anim::Id::Card).curve, m_place.clocks.newCard.raw(newCardDur()));
}

void Overview::animateStripTo(double from, double to) {
  // The range clamp and the sub-pixel threshold live in the type now, so a third caller
  // cannot arrive here with an endpoint outside [0, max], or with a 0.3px move to animate.
  const auto st = anim::cfg(anim::Id::StripStep);
  if (!st.on || !m_scroll.beginGlide(from, to)) {
    m_scroll.snap(to);
    debug::trace::scroll("snap", m_scroll.describe());
    return;
  }
  debug::trace::scroll("glide", m_scroll.describe());
  // A glide already in flight retargets from where the group actually is, not from the
  // caller's `from`: that value is the previous TARGET on the rebuild path and the current
  // position on the wheel path, and neither is what should be left behind.
  if (!m_place.clocks.strip.done(st.ms))
    m_scroll.beginGlide(m_scroll.current(), m_scroll.target());
  m_place.clocks.strip.begin();
  debug::trace::animStart("strip_scroll", "strip_step", "", st.ms, st.curve.c_str());
  ensureAnimPump();
}

}

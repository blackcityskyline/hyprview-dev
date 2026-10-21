#include <algorithm>
#include <chrono>

#include "../clock/curves.hpp"
#include "../anim.hpp"
#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"

namespace hyprview {

double staggered(double base, int i, int n) {
  return anim::stagger(base, i, n,
                       std::min(anim::param(anim::Param::Stagger),
                                anim::param(anim::Param::StaggerPerTile) * n));
}

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

// progress.raw() runs DOWN on a close, so every exit fraction reads it here, not on its own.
double Overview::exitT() const { return m_place.progress.exit(opening()); }

// The same number on the way in, so one schedule can be played in both directions.
double Overview::openT() const { return opening() ? m_place.progress.raw() : 1.0; }

// Hold the backdrop until our tiles are done AND Hyprland has spent its forced full frames.
bool Overview::backdropSettled() const {
  const auto m = m_monitor.lock();
  return m_place.clocks.tiles.done(animDuration()) && (!m || m->m_forceFullFrames <= 0);
}

// The backdrop rides the open/close animation; WINDOWED exit is the one exception.
double Overview::blurAlpha() const {
  if (!anim::backdropFades(opening(), m_landingEmpty))
    return 1.0;
  return std::clamp(eased(), 0.0, 1.0);
}

double Overview::eased() const { return m_place.eased(); }

double Overview::animDuration() const { return m_place.masterDuration(); }

double Overview::tileProgress(int i) const {
  return m_place.tileProgress(i, m_view, opening(), m_scene);
}

double Overview::entryProgress(int i) const {
  return m_place.entryProgress(i, m_scene);
}

double Overview::entryNow(const model::Tile &t, int i) const {
  return m_place.entryNow(t, i, m_scene);
}

// What Placement cannot know -- the monitor, the landing -- arrives in the frame.
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
    // Placement::resolve, NOT recipes.resolve: it also computes eased() and master duration.
  m_place.resolve(m_view, opening(), carouselMode());

  const double dur = animDuration();
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
  // Entries must settle explicitly or they keep evaluating curves forever.
  if (m_place.clocks.rebuild.done(m_place.recipes.entry.ms) &&
      std::any_of(m_scene.tiles().begin(), m_scene.tiles().end(),
                  [](const model::Tile &t) { return t.landed < 1.0; }))
    m_scene.settleEntry();

  const auto pulse = anim::cfg(anim::Id::Pulse);
  m_fx.eachPulse([&](auto &p) {
    if (!m_windows.windowOf(p.subj))
      return;
    p.p += std::min(34.0, std::chrono::duration<double, std::milli>(now - p.last)
                              .count()) /
           pulse.ms;
    p.last = now;
  });
  // Pruned on each pulse's own fraction: a live config change must not shorten a running ring.
  m_fx.dropPulse([&](const model::WinPulse &p) {
    return !m_windows.windowOf(p.subj) || p.p >= 1.0;
  });

  if (m_place.clocks.rebuild.done(m_place.recipes.ghost.ms) && m_place.clocks.rebuild.done(m_place.recipes.entry.ms)) {
    m_view.setWsSlideDir( 0);
    m_view.setExpoFlip( 0);
    if (m_life.consumeJumpClosing()) {
      close();
    }
  }

  // Pruned on each flight's own clock: a live config would cut landings short.
  const auto flightEnd = std::chrono::steady_clock::now();
  m_fx.dropSwap([&](const model::SwapFX &fx) {
    const bool done = m_windows.windowOf(fx.subj) == nullptr || fx.clock.raw(fx.ms) >= 1.0;
    // Measured, not configured: the harness compares the two.
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
    // NEVER zero progress.raw(): a transparent frame before deactivate() flashes. Clamp as soon
    // as the flight ends, NOT together with the deactivate -- the exit ramp outlives the close.
  if (!opening() && t >= 1.0 && m_place.progress.raw() <= 0.0)
    m_place.progress.firstFrame();
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
  const auto st = anim::cfg(anim::Id::StripStep);
  if (!st.on || !m_scroll.beginGlide(from, to)) {
    m_scroll.snap(to);
    debug::trace::scroll("snap", m_scroll.describe());
    return;
  }
  debug::trace::scroll("glide", m_scroll.describe());
    // A glide in flight retargets from where the group actually is: the caller's `from` is
    // the previous TARGET on the rebuild path and the old position on the wheel path.
  if (!m_place.clocks.strip.done(st.ms))
    m_scroll.beginGlide(m_scroll.current(), m_scroll.target());
  m_place.clocks.strip.begin();
  debug::trace::animStart("strip_scroll", "strip_step", "", st.ms, st.curve.c_str());
  ensureAnimPump();
}

}

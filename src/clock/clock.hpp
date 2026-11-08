#pragma once

#include "clocks.hpp"

#include <string>

namespace hyprview::clock {

class Clock {
  public:
    void begin() { m_t.begin(); }

    void reset() { m_t = anim::Tween{}; }
    void seek(double frac, double durMs) { m_t.seek(frac, durMs); }
    void pinEnd(double durMs) { m_t.pinEnd(durMs); }
    bool done(double durMs) const { return m_t.done(durMs); }
    double raw(double durMs) const { return m_t.raw(durMs); }

    void compensateStall(double gapMs, double durMs) {
      m_t.compensateStall(gapMs, durMs);
    }

  private:
    anim::Tween m_t;
};

// Every tween lives here: "is anything still moving" is one question of one object.
class Clocks {
  public:
    Clock master;
    Clock tiles;    // the glide every tile rides; retargeted on any layout change
    Clock rebuild;
    Clock strip;
    Clock dragLift;
    Clock newCard;
    Clock menu;

    // Snapshotted at the glide's start: a config edit mid-flight must not change a running
    // animation's duration.
    double tileMs = 0.0;
    std::string tileCurve;

    [[nodiscard]] bool anyRunning(double rebuildEntryMs, double rebuildGhostMs,
                                  double stripMs) const;
};

} // namespace hyprview::clock

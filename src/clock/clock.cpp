#include "clock.hpp"

namespace hyprview::clock {

bool Clocks::anyRunning(double rebuildEntryMs, double rebuildGhostMs,
                        double stripMs) const {
  return !rebuild.done(rebuildEntryMs) || !rebuild.done(rebuildGhostMs) ||
         !strip.done(stripMs);
}

} // namespace hyprview::clock
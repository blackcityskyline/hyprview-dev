#pragma once

// How far the run is open: 0 closed, 1 fully open. The transitions are named so a site
// cannot invent another meaning for the same number.
namespace gloview::clock {

class Progress {
  public:
    [[nodiscard]] double raw() const { return m_v; }

        // A swap or layout change landed on its final state.
    void settled() { m_v = 1.0; }

        // The exit ran to the end.
    void closed() { m_v = 0.0; }

            // Inverted on the way out: the master counts up to 1 while the run counts down from 1.
    void fromMaster(double masterRaw, bool opening) {
      m_v = opening ? masterRaw : 1.0 - masterRaw;
    }

        // Not zero: zero is the closed state, and the entry animation is evaluated against this.
    void firstFrame() { m_v = 1e-6; }

        // Every exit fraction reads the close through here, so there is no second formula.
    [[nodiscard]] double exit(bool opening) const {
      return opening ? 0.0 : 1.0 - m_v;
    }

  private:
    double m_v = 0.0;
};

} // namespace gloview::clock
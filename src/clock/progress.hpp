#pragma once

// How far the run is open: 0 closed, 1 fully open.
//
// One double that seven files read and four wrote, with the writes spelled as bare
// assignments. Two of them were `= 1.0` for "a swap has landed on screen" and three were
// `= 0.0` for "the exit finished", plus one `= 1e-6` that exists only to make the very first
// open frame non-zero. None of those said what they meant, and nothing checked the range.
//
// The transitions are named so each of those has a name, and so that a site cannot invent a
// fifth meaning for the same number.
namespace gloview::clock {

class Progress {
  public:
    [[nodiscard]] double raw() const { return m_v; }

    // A swap or a layout change landed and the run is showing its final state.
    void settled() { m_v = 1.0; }

    // The exit ran to the end.
    void closed() { m_v = 0.0; }

    // Driven from the master clock. close() rides the master DOWN, so the value is
    // inverted on the way out: the master counts up to 1 and the run counts down from 1.
    // Flipping this put every card on its natural box on the first close frame.
    void fromMaster(double masterRaw, bool opening) {
      m_v = opening ? masterRaw : 1.0 - masterRaw;
    }

    // The first frame of an open. Not zero: zero is the closed state, and the entry
    // animation is evaluated against this, so a card would start one frame behind.
    void firstFrame() { m_v = 1e-6; }

    // How far the CLOSE has run, 0 -> 1. Every exit fraction reads it through here, which
    // is why there is no second formula for it anywhere.
    [[nodiscard]] double exit(bool opening) const {
      return opening ? 0.0 : 1.0 - m_v;
    }

  private:
    double m_v = 0.0;
};

} // namespace gloview::clock
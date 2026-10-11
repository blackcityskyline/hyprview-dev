#pragma once

// The drag FSM, as a type that owns its state.
//
// It was a 14-field struct with public members, written in one place (input/mouse.cpp)
// and read from seven. Every reader could also have been a writer, and nothing said which
// fields a given press kind is allowed to have: a Press::Tile with idx == -1, a
// Press::StripWin with winIdx == -1, a stale grab offset carried across a reset -- all
// compile, all render as something. The FSM shape was in the type system nowhere.
//
// So the fields are private and the only way in is a transition. Each one sets the whole
// press in one place, which is what makes the pairing facts below structural instead of
// remembered: there is no way to set press = Tile without setting the idx that goes with
// it, because beginTile() is the only call that reaches that state.
//
// Templated on the window handle so the FSM has no hyprland/ include and can be unit
// tested; drag.hpp is what tests/drag_check.cpp includes. model::Drag is the
// instantiation the plugin uses. PHLWINDOWREF is a WP<CWindow>, so WinRef only has to
// be assignable and default-constructible here -- lock() is the reader's business.

#include <chrono>
#include <string>
#include <utility>

#include "../layout.hpp"

namespace gloview::model {

template <typename WinRef> class DragT {
public:
  // What the pointer went down on. Consumed is "the press was ours to handle and was",
  // which is why it exists alongside Empty rather than replacing it: a press that the
  // overview swallowed and a press that hit nothing lead to different code on release.
  enum class Press : int { Tile, StripWin, Empty, Consumed };

  // --- transitions: the only writes ------------------------------------------------
  // begin* is a complete press: it sets press and every field that press owns, so no
  // field can survive from the drag before it.

  // A press that armed on a grid card. grab is the pointer's offset inside the card, so
  // the card keeps its point under the cursor while it travels.
  void beginTile(int idx, double x, double y, int button, double grabX, double grabY,
                 LRect from, std::chrono::steady_clock::time_point at) {
    m_press  = Press::Tile;
    m_idx    = idx;
    m_winIdx = -1;
    m_win    = {};
    // press and current are set together: travelSq() measures from here, and a press with
    // no press point measures from (0,0) instead of from where the pointer went down.
    m_x = m_pressX = x;
    m_y = m_pressY = y;
    m_button   = button;
    m_grabX    = grabX;
    m_grabY    = grabY;
    m_fromBox  = from;
    m_lifted   = false;
    m_holdFrom = at;
  }

  // A press that armed on a window inside a strip card.
  void beginStripWin(int idx, int winIdx, double x, double y, int button,
                     double grabX, double grabY, LRect from, WinRef win,
                     std::chrono::steady_clock::time_point at) {
    m_press  = Press::StripWin;
    m_idx    = idx;
    m_winIdx = winIdx;
    m_win    = std::move(win);
    m_x = m_pressX = x;
    m_y = m_pressY = y;
    m_button   = button;
    m_grabX    = grabX;
    m_grabY    = grabY;
    m_fromBox  = from;
    m_lifted   = false;
    m_holdFrom = at;
  }

  // The overview took this press and did not arm on anything. Carries the button because
  // release still has to tell left from right.
  void consume(int button) {
    m_press  = Press::Consumed;
    m_button = button;
  }

  // A press that hit nothing at all.
  void miss(int button) {
    m_press  = Press::Empty;
    m_button = button;
  }

  // Cursor motion. Sets the current point only -- press point is fixed at begin*, which is
  // what makes travelSq() the distance since the press rather than since the last frame.
  void moveTo(double x, double y) {
    m_x = x;
    m_y = y;
  }

  void setLifted(bool on) { m_lifted = on; }

  // Back to a virgin drag. Every call site used to write `m_drag = {}` inline, which is
  // the one operation that must clear everything -- it is what stops a previous drag's
  // grab offset or fromBox from being read by the next one.
  void clear() { *this = DragT{}; }

  // --- queries ----------------------------------------------------------------------
  // idx() and winIdx() are -1 unless armed, and that is now a property of construction
  // rather than of call-site discipline.
  [[nodiscard]] Press press() const { return m_press; }
  [[nodiscard]] bool armed() const {
    return m_press == Press::Tile || m_press == Press::StripWin;
  }
  [[nodiscard]] bool onTile() const { return m_press == Press::Tile; }
  [[nodiscard]] bool onStripWin() const { return m_press == Press::StripWin; }
  [[nodiscard]] bool consumed() const { return m_press == Press::Consumed; }
  [[nodiscard]] int idx() const { return armed() ? m_idx : -1; }
  [[nodiscard]] int winIdx() const { return armed() ? m_winIdx : -1; }
  [[nodiscard]] int button() const { return m_button; }
  [[nodiscard]] double x() const { return m_x; }
  [[nodiscard]] double y() const { return m_y; }
  [[nodiscard]] double grabX() const { return m_grabX; }
  [[nodiscard]] double grabY() const { return m_grabY; }
  [[nodiscard]] bool lifted() const { return m_lifted; }
  [[nodiscard]] LRect fromBox() const { return m_fromBox; }
  [[nodiscard]] const WinRef &win() const { return m_win; }
  [[nodiscard]] std::chrono::steady_clock::time_point holdFrom() const {
    return m_holdFrom;
  }

  // Squared distance travelled since the press. Squared because every caller compares it
  // against a squared threshold -- 64 is 8px of slop (mouse.cpp:148), not 64px.
  [[nodiscard]] double travelSq() const {
    const double dx = m_x - m_pressX, dy = m_y - m_pressY;
    return dx * dx + dy * dy;
  }

  // One line of state: key=value, pipe-separated, no spaces in any value. Same shape as
  // the ANIM| records in debug/log.hpp, so `grep '^DRAG|'` and `grep '^ANIM|'` can be
  // read with the same parser, and a harness does not have to know which subsystem a
  // line came from before it can split it.
  [[nodiscard]] std::string describe() const {
    std::string s = "press=";
    // A switch, not an array indexed by the enum: the two were written separately once and
    // the array's order did not match Press, which named every state wrong.
    switch (m_press) {
    case Press::Tile:     s += "tile";     break;
    case Press::StripWin: s += "stripwin"; break;
    case Press::Consumed: s += "consumed"; break;
    case Press::Empty:    s += "empty";    break;
    }
    s += "|button=" + std::to_string(m_button);
    if (!armed())
      return s;
    s += "|idx=" + std::to_string(m_idx);
    if (m_press == Press::StripWin)
      s += "|winIdx=" + std::to_string(m_winIdx);
    return s + "|lifted=" + std::to_string(m_lifted ? 1 : 0) + "|x=" + std::to_string(m_x) +
           "|y=" + std::to_string(m_y) + "|travelSq=" + std::to_string(travelSq());
  }

private:
  Press    m_press = Press::Empty;
  int      m_idx = -1, m_winIdx = -1;
  WinRef   m_win{};
  int      m_button = 0;
  double   m_pressX = 0, m_pressY = 0;
  double   m_grabX = 0, m_grabY = 0;
  double   m_x = 0, m_y = 0;
  bool     m_lifted = false;
  std::chrono::steady_clock::time_point m_holdFrom{};
  LRect    m_fromBox{};
};

} // namespace gloview::model
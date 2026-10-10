#pragma once

#include "model.hpp"

#include <utility>
#include <vector>

namespace gloview::model {

// The cards in the main area, which one is focused, what the cursor is over.
//
// Writes go through named intents -- armAppear, settleAppear, park, pinNatural --
// plus tileAt() for the one caller that computes per tile. There is no
// vector-issuing accessor: recency ordering once went wrong by sorting this array
// inside a layout pass, and handing out the container is what allowed it.
class Scene {
  public:
    [[nodiscard]] const std::vector<Tile> &tiles() const { return m_tiles; }
    [[nodiscard]] std::size_t size() const { return m_tiles.size(); }
    [[nodiscard]] bool empty() const { return m_tiles.empty(); }

    void clear() { m_tiles.clear(); }
    // The builder appends; ordering is buildTiles' business and setTargets' output.
    void add(Tile &&t) { m_tiles.push_back(std::move(t)); }

    // The entry ladder, written from six files and forced back to 1 in two of them:
    // 0 arms the scale-in, 1 means "already on screen". Naming both ends means a
    // caller cannot set 0.5 by accident, and there is exactly one place that knows
    // what each value means.
    void armAppear() {
      for (auto &t : m_tiles)
        t.appear = 0.0;
    }
    void settleAppear() {
      for (auto &t : m_tiles)
        t.appear = 1.0;
    }
    // The desktop's parked cards: the natural box becomes the drop point, so a
    // window released onto the desktop has somewhere to come back to.
    void park(size_t i, const LRect &at) {
      if (i >= m_tiles.size())
        return;
      m_tiles[i].target = at;
      m_tiles[i].natural = at;
      m_tiles[i].parked = true;
    }
    // A window swapped back to where it was: the old box becomes its destination
    // and the entry animation is cancelled with it.
    void pinNatural(const void *win, const LRect &box) {
      for (auto &t : m_tiles)
        if (t.win.lock().get() == win) {
          t.natural = box;
          t.appear = 1.0;
          return;
        }
    }
    // One tile, writable. Indexed and bounds-checked: a caller that needs to
    // compute something per tile still can, but it cannot resize or reorder the
    // array the way the vector-issuing edit() allowed.
    Tile &tileAt(size_t i) { return m_tiles[i]; }
    // A layout pass hands back one box per tile, in order.
    void setTargets(const std::vector<LRect> &boxes) {
      for (size_t i = 0; i < m_tiles.size() && i < boxes.size(); ++i)
        m_tiles[i].target = boxes[i];
    }

    [[nodiscard]] int selected() const { return m_selected; }
    [[nodiscard]] int hoveredTile() const { return m_hoveredTile; }
    [[nodiscard]] int hoveredStrip() const { return m_hoveredStrip; }

    // Selection is an index every reader assumed was in range.
    [[nodiscard]] bool hasSelection() const {
      return m_selected >= 0 &&
             static_cast<std::size_t>(m_selected) < m_tiles.size();
    }
    void select(int i) { m_selected = i; }

    // The one answer to "which tile is focused", for paint AND input alike.
    //
    // The array can hold tiles while nothing is selected: the overview opened on an empty
    // workspace (no tiles, select(-1)), the user switched to all-workspaces, and the
    // rebuild produced tiles without re-selecting. layoutTiles already answered "none ->
    // tile 0" for that case and drew it centred, so the user had a focused-looking card on
    // screen while every input path asked selected(), read -1, and acted on nothing --
    // which is why Enter did nothing at all and the exit fell through to a bare close.
    [[nodiscard]] int focused() const {
      return hasSelection() ? m_selected : (m_tiles.empty() ? -1 : 0);
    }

    void stepSelection(int dir) {
      if (m_tiles.empty())
        return;
      const int n = static_cast<int>(m_tiles.size());
      m_selected = ((m_selected < 0 ? 0 : m_selected) + dir % n + n) % n;
    }

    void hover(int tile, int strip) {
      m_hoveredTile = tile;
      m_hoveredStrip = strip;
    }
    void clearHover() {
      m_hoveredTile = -1;
      m_hoveredStrip = -1;
    }

  private:
    std::vector<Tile> m_tiles;
    int m_selected = -1;
    int m_hoveredTile = -1;
    int m_hoveredStrip = -1;
};

} // namespace gloview::model
#pragma once

#include "model.hpp"

#include <utility>
#include <vector>

namespace gloview::model {

// The cards in the main area, which one is focused, what the cursor is over.
//
// Writes go through named intents -- armEntry, settleEntry, park, pinNatural --
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
    void armEntry() {
      for (auto &t : m_tiles)
        t.landed = 0.0;
    }
    void settleEntry() {
      for (auto &t : m_tiles)
        t.landed = 1.0;
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
    // Two windows swapped places: exchange which tile carries which window.
//
// Not a reordering for its own sake -- a swap Hyprland cannot express is invisible.
// switchTargets exchanges two layout TARGETS, but when both windows already sit at the
// same geometry (one behind the other, both tiled full) there is nothing for it to
// move: goal() returns the same box for both before and after, the rebuild produces
// the same layout from the same inputs, and the cards change place with nothing
// travelling between the places.
//
// The overlay lays cards out by INDEX, so trading the two TARGETS is what makes the
// exchange visible. Each window keeps its own natural -- the box startTileGlide
// captured for it by window identity, i.e. where it actually was -- and is handed the
// other's card slot, so it flies from where it was to where the other one was.
//
// Trading the whole tiles instead would be a no-op here: natural == target for both
// (the rebuild reproduced the same layout), so exchanging the pairs exchanges identical
// boxes and nothing moves.
//
// When the geometry DOES differ the two agree: Hyprland moves the windows for real, and
// the next rebuild puts each window where this swap already drew it.
void swapTiles(size_t a, size_t b) {
  if (a >= m_tiles.size() || b >= m_tiles.size() || a == b)
    return;
  std::swap(m_tiles[a].target, m_tiles[b].target);
}
    // Did the rebuild leave this tile with nowhere to go? natural == target means the
    // layout handed the window back the box it already had, so the glide has no distance
    // to cover. That is the signal that the layout could not tell two windows apart, and
    // the caller has to express the exchange in the card slots itself.
    // Point a tile's glide at a different source box -- where a window was actually
    // released, as opposed to the card slot it was picked up from.
    void setNatural(size_t i, const LRect &box) {
      if (i < m_tiles.size())
        m_tiles[i].natural = box;
    }
    [[nodiscard]] bool tileAtRest(size_t i) const {
  if (i >= m_tiles.size())
    return false;
  const LRect &a = m_tiles[i].natural, &b = m_tiles[i].target;
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
    // Which card slot carries this window, or -1. A caller swapping two windows needs
    // the index to say WHICH cards to exchange, not just that the windows swapped.
    [[nodiscard]] int tileIndexOf(const void *win) const {
  for (std::size_t i = 0; i < m_tiles.size(); ++i)
    if (m_tiles[i].win.lock().get() == win)
      return static_cast<int>(i);
  return -1;
}
    // A window swapped back to where it was: the old box becomes its destination
    // and the entry animation is cancelled with it.
    void pinNatural(const void *win, const LRect &box) {
      for (auto &t : m_tiles)
        if (t.win.lock().get() == win) {
          t.natural = box;
          t.landed = 1.0;
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
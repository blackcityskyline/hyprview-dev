#pragma once

#include "model.hpp"

#include <utility>
#include <vector>

namespace gloview::model {

// The cards in the main area, which one is focused, what the cursor is over. Writes go through
// named intents plus tileAt(). There is no vector-issuing accessor: recency ordering once
// went wrong by sorting this array inside a layout pass.
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
    // Exchange which tile carries which window. A swap Hyprland cannot express is invisible:
    // two windows already at one geometry give goal() the same box before and after, so the
    // rebuild reproduces the layout. The overlay lays cards out by INDEX, so trading the two
    // TARGETS is what makes the exchange visible: each window keeps its own natural and is
    // handed the other's card slot.
void swapTiles(size_t a, size_t b) {
  if (a >= m_tiles.size() || b >= m_tiles.size() || a == b)
    return;
  std::swap(m_tiles[a].target, m_tiles[b].target);
}
    // Point a tile's glide at a different source box -- where a window was actually
    // released, as opposed to the card slot it was picked up from.
    void setNatural(size_t i, const LRect &box) {
      if (i < m_tiles.size())
        m_tiles[i].natural = box;
    }
    // natural == target: the layout handed the window back the box it already had and could
    // not tell two windows apart, so the caller has to express the exchange in card slots.
    [[nodiscard]] bool tileAtRest(size_t i) const {
  if (i >= m_tiles.size())
    return false;
  const LRect &a = m_tiles[i].natural, &b = m_tiles[i].target;
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
    // Or -1. A caller swapping two needs the index to say WHICH cards, not just that they
    // exchanged. A SUBJECT rather than a window: a model owns none and cannot resolve one.
    [[nodiscard]] int tileIndexOf(SubjectRef subj) const {
  for (std::size_t i = 0; i < m_tiles.size(); ++i)
    if (m_tiles[i].subj == subj)
      return static_cast<int>(i);
  return -1;
}
    // A window swapped back to where it was: the old box becomes its destination
    // and the entry animation is cancelled with it.
    void pinNatural(SubjectRef subj, const LRect &box) {
      for (auto &t : m_tiles)
        if (t.subj == subj) {
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

    // The one answer to "which tile is focused", for paint and input alike, or null. Not
    // selected(): the deck can hold tiles while nothing is selected -- an empty workspace
    // switched to all-workspaces rebuilds into tiles without re-selecting, and the raw index
    // was -1 there, so Enter activated nothing.
    [[nodiscard]] SubjectRef focusedSubject() const {
      const int f = focused();
      return f >= 0 ? m_tiles[static_cast<std::size_t>(f)].subj : SubjectRef{};
    }

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
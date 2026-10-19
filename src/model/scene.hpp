#pragma once

#include "model.hpp"

#include <utility>
#include <vector>

namespace hyprview::model {

// The cards in the main area, which one is focused, what the cursor is over. Writes go
// through named intents plus tileAt(); there is no vector-issuing accessor, because recency
// ordering once went wrong by sorting this array inside a layout pass.
class Scene {
  public:
    [[nodiscard]] const std::vector<Tile> &tiles() const { return m_tiles; }
    [[nodiscard]] std::size_t size() const { return m_tiles.size(); }
    [[nodiscard]] bool empty() const { return m_tiles.empty(); }

    void clear() { m_tiles.clear(); }
    void add(Tile &&t) { m_tiles.push_back(std::move(t)); }

    // landed: 0 arms the scale-in, 1 means already on screen. Both ends are named, so no
    // caller sets 0.5 by accident.
    void armEntry() {
      for (auto &t : m_tiles)
        t.landed = 0.0;
    }
    void settleEntry() {
      for (auto &t : m_tiles)
        t.landed = 1.0;
    }
    void park(size_t i, const LRect &at) {
      if (i >= m_tiles.size())
        return;
      m_tiles[i].target = at;
      m_tiles[i].natural = at;
      m_tiles[i].parked = true;
    }
    // Hyprland cannot express this swap, and the overlay lays cards out by INDEX: trading the
    // two TARGETS is what makes the exchange visible -- each window keeps its own natural and
    // is handed the other's card slot.
void swapTiles(size_t a, size_t b) {
  if (a >= m_tiles.size() || b >= m_tiles.size() || a == b)
    return;
  std::swap(m_tiles[a].target, m_tiles[b].target);
}
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
    // A SUBJECT, not a window: a model owns none and cannot resolve one.
    [[nodiscard]] int tileIndexOf(SubjectRef subj) const {
  for (std::size_t i = 0; i < m_tiles.size(); ++i)
    if (m_tiles[i].subj == subj)
      return static_cast<int>(i);
  return -1;
}
    void pinNatural(SubjectRef subj, const LRect &box) {
      for (auto &t : m_tiles)
        if (t.subj == subj) {
          t.natural = box;
          t.landed = 1.0;
          return;
        }
    }
    Tile &tileAt(size_t i) { return m_tiles[i]; }
    void setTargets(const std::vector<LRect> &boxes) {
      for (size_t i = 0; i < m_tiles.size() && i < boxes.size(); ++i)
        m_tiles[i].target = boxes[i];
    }

    [[nodiscard]] int selected() const { return m_selected; }
    [[nodiscard]] int hoveredTile() const { return m_hoveredTile; }
    [[nodiscard]] int hoveredStrip() const { return m_hoveredStrip; }

    [[nodiscard]] bool hasSelection() const {
      return m_selected >= 0 &&
             static_cast<std::size_t>(m_selected) < m_tiles.size();
    }
    void select(int i) { m_selected = i; }

    // The one answer to "which tile is focused", or null. Not selected(): the deck can hold
    // tiles while nothing is selected -- then the raw index was -1 and Enter activated nothing.
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

} // namespace hyprview::model

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "model.hpp"

namespace gloview::model {

// The workspace strip: one card per workspace, plus the optional leading "All" card. Rebuilt
// from scratch by buildStrip() and read as a snapshot by the painters, so construction is not
// open on purpose -- "append to whatever is there" is never the right move.
class Strip {
  public:
    [[nodiscard]] const std::vector<StripItem> &items() const { return m_items; }
    [[nodiscard]] std::size_t size() const { return m_items.size(); }
    [[nodiscard]] bool empty() const { return m_items.empty(); }

    void add(StripItem &&item) { m_items.push_back(std::move(item)); }

    using const_iterator = std::vector<StripItem>::const_iterator;
    [[nodiscard]] const_iterator begin() const { return m_items.begin(); }
    [[nodiscard]] const_iterator end() const { return m_items.end(); }

    // Non-const iteration is how the layout pass assigns each card its box: geometry only,
    // nothing here may add, remove or reorder a card.
    using iterator = std::vector<StripItem>::iterator;
    [[nodiscard]] iterator begin() { return m_items.begin(); }
    [[nodiscard]] iterator end() { return m_items.end(); }

    [[nodiscard]] const StripItem &operator[](std::size_t i) const {
      return m_items[i];
    }

    // Its laid-out box plus the band's slide and the group's scroll. Out of range is an empty
    // box rather than a throw -- callers hit-test a moving strip.
    [[nodiscard]] LRect cardAt(std::size_t i, LVec2 slide, LVec2 scroll) const {
      if (i >= m_items.size())
        return LRect{0, 0, 0, 0};
      const auto &c = m_items[i].card;
      return LRect{c.x + slide.x + scroll.x, c.y + slide.y + scroll.y, c.w, c.h};
    }

    // buildStrip() hoists the special cards to the front by moving them out wholesale.
    [[nodiscard]] std::vector<StripItem> takeAll() {
      return std::exchange(m_items, {});
    }

    void clear() { m_items.clear(); }

  private:
    std::vector<StripItem> m_items;
};

} // namespace gloview::model

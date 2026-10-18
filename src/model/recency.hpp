#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace gloview::model {

// Recency order over windows, most recent first. Not owned by the carousel: the run, the strip
// and alt-tab all want it and a private copy per consumer would diverge. Pure -- addresses in,
// indices out. promote() drops dead entries, puts a new window at the front and ROTATES an
// already-ranked one there rather than erasing and reinserting.
class Recency {
  public:
    void promote(const void *window);

    [[nodiscard]] std::size_t rank(const void *window) const;

    // Indices of `keys` into `out`, most-recent first. Stable among equals.
    void order(const std::vector<const void *> &keys,
               std::vector<std::size_t> &out) const;

    // Drops entries not in `live`.
    void forgetDead(const std::vector<const void *> &live);

    [[nodiscard]] std::size_t size() const { return m_front.size(); }

  private:
    // Front is the most recent. Addresses, like the reference.
    std::deque<const void *> m_front;
};

} // namespace gloview::model
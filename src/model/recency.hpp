#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace hyprview::model {

// Recency order over windows, most recent first: the run, the strip and alt-tab all want it,
// and a private copy per consumer would diverge. Pure -- addresses in, indices out.
class Recency {
  public:
    void promote(const void *window);

    [[nodiscard]] std::size_t rank(const void *window) const;

    // Indices of `keys` into `out`, most-recent first. Stable among equals.
    void order(const std::vector<const void *> &keys,
               std::vector<std::size_t> &out) const;

    void forgetDead(const std::vector<const void *> &live);

    [[nodiscard]] std::size_t size() const { return m_front.size(); }

  private:
    std::deque<const void *> m_front;
};

} // namespace hyprview::model

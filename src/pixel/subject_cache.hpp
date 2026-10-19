#pragma once

// Cached pictures of a subject, keyed by SubjectRef{id, epoch}: the epoch is the connection
// that issued the id, standing in for the owner a window keeps beside its address
// (AGENTS.md 2.9, 2.22). Templated on the stored type so a check can build it without an
// hyprland/ include (AGENTS.md 2.8). find() returns a pointer, so "absent" is representable.

#include <cstdint>
#include <unordered_map>

#include "../model/subject.hpp"

namespace gloview::pixel {

template <class Tex> class SubjectCache {
  public:
    // A null texture is not stored: "nothing" and "not looked" must stay distinguishable.
    void put(model::SubjectRef s, const Tex &tex) {

      // The generation moved: everything cached came from a dead connection and its ids are
      // being reused right now. Drop the lot (AGENTS.md 2.22).
      if (m_generation != s.epoch) {
        m_items.clear();
        m_generation = s.epoch;
      }
      if (!tex)
        return;
      m_items.insert_or_assign(s, tex);
    }

    // How a cached entry is UPDATED, separate from put(): put() skips anything falsy, which
    // is wrong for the empty entry a label starts life as.
    [[nodiscard]] Tex *slot(model::SubjectRef s) {
      if (m_generation != s.epoch) {
        m_items.clear();
        m_generation = s.epoch;
      }
      return &m_items[s];
    }

    [[nodiscard]] const Tex *find(model::SubjectRef s) const {
      const auto it = m_items.find(s);
      return it == m_items.end() ? nullptr : &it->second;
    }

    [[nodiscard]] bool has(model::SubjectRef s) const {
      return m_items.find(s) != m_items.end();
    }

    [[nodiscard]] std::uint32_t generation() const { return m_generation; }
    [[nodiscard]] std::size_t size() const { return m_items.size(); }

    void clear() {
      m_items.clear();
      m_generation = 0;
    }

  private:
    std::unordered_map<model::SubjectRef, Tex> m_items;
    std::uint32_t m_generation = 0;
};

} // namespace gloview::pixel

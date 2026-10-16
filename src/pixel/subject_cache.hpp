#pragma once

// Cached pictures of subjects: a tab's screenshot, a tab's icon.
//
// A window caches its own texture under its address, and PixelCache keeps the OWNER beside
// it because an address gets reused (AGENTS.md 2.9). A tab has neither an address nor an
// owner. What it has is the connection that issued its id, and that plays exactly the same
// role -- so the epoch in the key is the owner, and an entry loaded under one generation is
// never handed out under another.
//
// Templated on the stored type, like PixelCache's own label entries already are. That is not
// generality for its own sake: ITexture lives behind hyprland/ and AGENTS.md 2.8 forbids a
// check from compiling against hyprland/, so a non-template cache would put its staleness
// rule -- the whole point of it -- out of reach of any check.
//
// find() returns a pointer rather than a value so that "absent" is representable for any
// stored type without inventing a null for it. Callers ask one question and get one answer.

#include <cstdint>
#include <unordered_map>

#include "../model/subject.hpp"

namespace gloview::pixel {

template <class Tex> class SubjectCache {
  public:
    // A null texture is not stored. "We looked and there was nothing" and "we have not
    // looked" have the same correct answer -- draw the next thing down -- and storing the
    // null would collapse them into one lookup that can no longer say which happened.
    void put(model::SubjectRef s, const Tex &tex) {

      // The generation moved. Everything cached was produced by a connection that no longer
      // exists, and the ids in it are being reused right now -- serving one of those is
      // showing one browser's page under another browser's tab. Drop the lot instead.
      if (m_generation != s.epoch) {
        m_items.clear();
        m_generation = s.epoch;
      }
      if (!tex)
        return;
      m_items.insert_or_assign(s, tex);
    }

    // The entry for this subject, empty if there is none yet -- and everything dropped first
    // if the connection moved. This is how a cached entry is UPDATED, and it is separate from
    // put() because put() skips anything falsy, which is right for a picture you already hold
    // and wrong for the empty entry a label starts life as. Two entry points because there are
    // two jobs: put a value you have, or fill one in.
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

    // Same lookup without dereferencing, so a check can ask "is it cached?" without a GPU
    // and without a texture it could not fabricate anyway.
    [[nodiscard]] bool has(model::SubjectRef s) const {
      return m_items.find(s) != m_items.end();
    }

    // Nothing is cached until something is, and what is cached belongs to one connection.
    [[nodiscard]] std::uint32_t generation() const { return m_generation; }
    [[nodiscard]] std::size_t size() const { return m_items.size(); }

    // Forgets everything. Called when the run ends, so a tab's texture does not outlive the
    // browser window it was a picture of.
    void clear() {
      m_items.clear();
      m_generation = 0;
    }

  private:
    std::unordered_map<model::SubjectRef, Tex> m_items;
    std::uint32_t m_generation = 0;
};

} // namespace gloview::pixel
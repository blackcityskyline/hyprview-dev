#pragma once

#include "../layout.hpp"
#include "../model/subject.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gloview::view {

// Which view is on screen, which way it is moving, the monitor-to-preview transform, and what
// layoutTiles last resolved. The resolved layout lives here because the renderer must ask what
// was PRODUCED, never the config: free-arrange, the all/one flip and the carousel switch each
// override what `layout` says. Kept with the selection it was resolved for, so one question
// answers about one object instead of four fields compared by hand.
class State {
  public:
    [[nodiscard]] bool desktop() const { return m_desktop; }
    void setDesktop(bool on) { m_desktop = on; }

    [[nodiscard]] int allOverride() const { return m_allOverride; }
    void setAllOverride(int v) { m_allOverride = v; }

        // Whether the run shows every workspace -- a question about the run's mode, not the config:
        // an override the user set for this run outranks the key, and -1 is the only "no override".
        // Six files asked it by reaching into the coordinator for the same two-term precedence.
    [[nodiscard]] bool allWorkspaces(bool configured) const {
      return m_allOverride < 0 ? configured : m_allOverride != 0;
    }

    [[nodiscard]] bool run() const { return m_run; }
    void setRun(bool on) { m_run = on; }
        // The width of the CENTRED card, the reference every other card's chrome is measured
        // against. The run's cards shrink in steps with distance, so a caption that does not
        // follow is the same size on a card half the width -- worst on the outermost card of a
        // five-card run. Written once per layout, read at paint.
    void        setCardRefW(double w) { m_cardRefW = w > 0.0 ? w : 0.0; }
    [[nodiscard]] double cardRefW() const { return m_cardRefW; }

    [[nodiscard]] bool carouselOn() const { return m_carouselOn; }
    void setCarouselOn(bool on) { m_carouselOn = on; }

        // Which subjects this run shows: windows, tabs, or both. Resolved once when the run opens,
        // like m_engine, because a run that changed its mind halfway through would lay out two
        // vocabularies at once. Config is read at the point of resolution and never cached in the
        // field above (AGENTS.md 2.16), so a live change lands on the NEXT run rather than never.
    [[nodiscard]] model::Domain domain() const { return m_domain; }
    void setDomain(model::Domain d) { m_domain = d; }

    [[nodiscard]] int wsSlideDir() const { return m_wsSlideDir; }
    void setWsSlideDir(int d) { m_wsSlideDir = d; }

    [[nodiscard]] int expoFlip() const { return m_expoFlip; }
    void setExpoFlip(int d) { m_expoFlip = d; }

    [[nodiscard]] bool jumpMode() const { return m_jumpMode; }
    void setJumpMode(bool on) { m_jumpMode = on; }

    [[nodiscard]] double ox() const { return m_ox; }
    [[nodiscard]] double oy() const { return m_oy; }
    void setTransform(double s, double ox, double oy) {
      m_s = s;
      m_ox = ox;
      m_oy = oy;
    }

    [[nodiscard]] Engine engine() const { return m_engine; }
    [[nodiscard]] const CarouselLayout &carousel() const { return m_carousel; }
    [[nodiscard]] int carouselFor() const { return m_carouselFor; }

        // slot[k] = the card index slot k shows. The renderer needs it with the geometry it was
        // resolved with: card k is NOT tile k, and assuming otherwise draws the wrong window.
    [[nodiscard]] const std::vector<std::size_t> &slot() const { return m_slot; }


        // The tile the carousel's k-th slot holds, or -1. The carousel orders tiles by DEPTH, not
        // index, so stepping selection has to go through the slot. -1 marks a stale slot (it is a
        // cache that outlives a shrink of the tile array), and a caller falling back rather than
        // selecting an unrelated tile is the point.
    [[nodiscard]] int tileInSlot(std::size_t k) const {
      return k < m_slot.size() ? static_cast<int>(m_slot[k]) : -1;
    }

    [[nodiscard]] int slotOf(std::size_t tile) const {
      for (std::size_t k = 0; k < m_slot.size(); ++k)
        if (m_slot[k] == tile)
          return static_cast<int>(k);
      return -1;
    }

        // Hand card k_a's tile to card k_b and the other way round. Scene::swapTiles exchanges two
        // tiles' TARGETS, which is what makes the exchange move, but a tile's depth is not its own:
        // the carousel reads it as cards[slotOf(tile)].depth. Swap the targets alone and every tile
        // keeps the card it used to occupy, so the card that travelled to the periphery still says
        // depth 0 and the run paints the periphery over its own centre. Swapping the slot entries
        // travels with the geometry.
    void swapSlots(std::size_t a, std::size_t b) {
      const auto pos = [this](std::size_t tile) {
        for (std::size_t k = 0; k < m_slot.size(); ++k)
          if (m_slot[k] == tile)
            return k;
        return m_slot.size();
      };
      const std::size_t ka = pos(a), kb = pos(b);
      if (ka >= m_slot.size() || kb >= m_slot.size() || ka == kb)
        return;
      std::swap(m_slot[ka], m_slot[kb]);
    }

    void setEngine(Engine e) { m_engine = e; }
    void clearCards() {
      m_carousel = {};
      m_carouselFor = -1;
      m_slot.clear();
    }
    void resolve(Engine engine, const CarouselLayout &carousel, int selected,
                 std::vector<std::size_t> slot) {
      m_engine = engine;
      m_carousel = carousel;
      m_carouselFor = selected;
      m_slot = std::move(slot);
    }

  private:
    bool m_desktop = false;
    int  m_allOverride = -1;
    bool   m_run      = false;
    double m_cardRefW = 0.0;
    bool m_carouselOn = true;

    int  m_wsSlideDir = 0;
    int  m_expoFlip = 0;
    bool m_jumpMode = false;

    double m_s = 1.0, m_ox = 0.0, m_oy = 0.0;

    Engine m_engine = Engine::Rows;
    model::Domain m_domain = model::Domain::Windows;
    CarouselLayout m_carousel;
    std::vector<std::size_t> m_slot;
    int m_carouselFor = -1;
};

} // namespace gloview::view
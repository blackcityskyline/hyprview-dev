#pragma once

#include "../layout.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gloview::view {

// Which view is on screen, which way it is moving, the monitor-to-preview
// transform, and what layoutTiles last resolved.
//
// The resolved layout lives here because the renderer must ask what was produced,
// never the config: free-arrange, the all/one flip and the carousel switch each
// override what `layout` says. Keeping it with the selection it was resolved for
// makes that one question of one object; it was four fields compared by hand.
class State {
  public:
    [[nodiscard]] bool desktop() const { return m_desktop; }
    void setDesktop(bool on) { m_desktop = on; }

    [[nodiscard]] int allOverride() const { return m_allOverride; }
    void setAllOverride(int v) { m_allOverride = v; }

    // Whether the run is showing every workspace, which is a question about the run's mode
    // and not about the config: an override the user set for this run outranks the key, and
    // -1 is the only value that means "no override".
    //
    // Six files ask this, and they were all reaching into the coordinator for a two-term
    // expression of the same precedence rule.
    [[nodiscard]] bool allWorkspaces(bool configured) const {
      return m_allOverride < 0 ? configured : m_allOverride != 0;
    }

    [[nodiscard]] bool run() const { return m_run; }
    void setRun(bool on) { m_run = on; }

    [[nodiscard]] bool carouselOn() const { return m_carouselOn; }
    void setCarouselOn(bool on) { m_carouselOn = on; }

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

    // slot[k] = the card index that card slot k shows. The renderer needs this too,
    // so it belongs with the geometry it was resolved with: card k is NOT tile k,
    // and a renderer assuming otherwise draws the wrong window in every slot.
    [[nodiscard]] const std::vector<std::size_t> &slot() const { return m_slot; }


    // The tile the carousel's k-th slot holds, or -1.
    //
    // The carousel orders tiles by DEPTH, not index, so stepping selection has to go
    // through the slot: the next card on screen is not the next index. Returns -1 when the
    // slot is stale -- it is a cache that outlives a shrink of the tile array -- and a
    // caller that gets -1 falls back rather than selecting an unrelated tile.
    [[nodiscard]] int tileInSlot(std::size_t k) const {
      return k < m_slot.size() ? static_cast<int>(m_slot[k]) : -1;
    }

    [[nodiscard]] int slotOf(std::size_t tile) const {
      for (std::size_t k = 0; k < m_slot.size(); ++k)
        if (m_slot[k] == tile)
          return static_cast<int>(k);
      return -1;
    }

    // Hand card k_a's tile to card k_b and the other way round.
    //
    // Scene::swapTiles exchanges two tiles' TARGETS, which is what makes the exchange
    // move. But a tile's depth is not its own: the carousel reads it as
    // cards[slotOf(tile)].depth, and slotOf is the inverse of slot. Swap the targets
    // alone and every tile keeps the card it used to occupy -- so the card that just
    // travelled to the periphery still says depth 0, the one sitting in the middle says
    // depth 1, and the run paints the periphery over its own centre.
    //
    // Swapping the slot entries travels with the geometry: a tile is now drawn from the
    // card whose box it was given.
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
    bool m_run = false;
    bool m_carouselOn = true;

    int  m_wsSlideDir = 0;
    int  m_expoFlip = 0;
    bool m_jumpMode = false;

    double m_s = 1.0, m_ox = 0.0, m_oy = 0.0;

    Engine m_engine = Engine::Rows;
    CarouselLayout m_carousel;
    std::vector<std::size_t> m_slot;
    int m_carouselFor = -1;
};

} // namespace gloview::view
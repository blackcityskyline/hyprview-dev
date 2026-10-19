#pragma once

#include "../layout.hpp"
#include "../model/subject.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gloview::view {

// Which view is on screen, its direction, the monitor-to-preview transform, and what
// layoutTiles last resolved: the renderer asks what was PRODUCED, never the config.
class State {
  public:
    [[nodiscard]] bool desktop() const { return m_desktop; }
    void setDesktop(bool on) { m_desktop = on; }

    [[nodiscard]] int allOverride() const { return m_allOverride; }
    void setAllOverride(int v) { m_allOverride = v; }

        // A question about the run's mode, not the config: an override set for this run
        // outranks the key, and -1 is the only "no override".
    [[nodiscard]] bool allWorkspaces(bool configured) const {
      return m_allOverride < 0 ? configured : m_allOverride != 0;
    }

    [[nodiscard]] bool run() const { return m_run; }
    void setRun(bool on) { m_run = on; }
        // The width of the CENTRED card, the reference every other card's chrome is measured
        // against -- run cards shrink with distance. Written once per layout, read at paint.
    void        setCardRefW(double w) { m_cardRefW = w > 0.0 ? w : 0.0; }
    [[nodiscard]] double cardRefW() const { return m_cardRefW; }

    [[nodiscard]] bool carouselOn() const { return m_carouselOn; }
    void setCarouselOn(bool on) { m_carouselOn = on; }

        // Windows, tabs, or both, resolved once when the run opens, like m_engine: changing
        // it mid-run would lay out two vocabularies. Live change lands on the NEXT run.
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

        // slot[k] = the card index slot k shows, with the geometry it was resolved with: card
        // k is NOT tile k, and assuming otherwise draws the wrong window.
    [[nodiscard]] const std::vector<std::size_t> &slot() const { return m_slot; }


        // The tile the carousel's k-th slot holds, or -1: the carousel orders tiles by DEPTH,
        // not index. -1 marks a stale slot; callers must fall back, not grab another tile.
    [[nodiscard]] int tileInSlot(std::size_t k) const {
      return k < m_slot.size() ? static_cast<int>(m_slot[k]) : -1;
    }

    [[nodiscard]] int slotOf(std::size_t tile) const {
      for (std::size_t k = 0; k < m_slot.size(); ++k)
        if (m_slot[k] == tile)
          return static_cast<int>(k);
      return -1;
    }

        // Scene::swapTiles exchanges two tiles' TARGETS, but the carousel reads depth as
        // cards[slotOf(tile)].depth: swap targets alone and the travelled card stays at
        // depth 0. Swapping the slot entries travels with the geometry.
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
#pragma once

#include "subject_cache.hpp"

#include <cstdint>

#include <unordered_map>
#include <string>

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace Render {
class ITexture;
class IFramebuffer;
} // namespace Render

namespace hyprview::pixel {

// Entries with no object behind them live in a map of their own and are never swept.
enum Slot : int { All = -2, CarouselCount = -1 };

// The KEYING POLICY is the whole job: entries are owned by what they describe, so an address
// Hyprland recycles cannot hand one object's texture to another (AGENTS.md 2.9).
class PixelCache {
  public:
    struct LabelEntry {
      std::string text;
      int maxW = 0;
      int size = 0;
            // Colour and size belong to the key: config is live, a stale caption would keep being drawn.
      CHyprColor col;
      SP<Render::ITexture> tex;

      [[nodiscard]] bool matches(const std::string &t, int w, int s,
                                 const CHyprColor &c) const {
        return tex && text == t && maxW == w && size == s && col == c;
      }
    };

        // maxW <= 0 = untruncated; renderText clips, so a wide title is shortened and re-rendered.
    SP<Render::ITexture> windowLabel(const PHLWINDOW &win, const std::string &text,
                                     const CHyprColor &col, int size, int maxW = 0);
        // A miss is NOT cached: the browser may not have written the file yet.
    SP<Render::ITexture> putPicture(const model::SubjectRef &s,
                                   const std::string &screenshotPath,
                                   const std::string &iconPath);
    [[nodiscard]] SP<Render::ITexture> picture(const model::SubjectRef &s) const;

    SP<Render::ITexture> subjectLabel(const model::SubjectRef &s,
                                      const std::string &text,
                                      const CHyprColor &col, int size,
                                      int maxW = 0);

    SP<Render::ITexture> workspaceLabel(const PHLWORKSPACE &ws,
                                        const std::string &text,
                                        const CHyprColor &col, int size,
                                        int maxW = 0);
    SP<Render::ITexture> virtualLabel(WORKSPACEID id, const std::string &text,
                                      const CHyprColor &col, int size,
                                      int maxW = 0);
    SP<Render::ITexture> fixedLabel(Slot slot, const std::string &text,
                                    const CHyprColor &col, int size,
                                    int maxW = 0);

    SP<Render::ITexture> glyph(const std::string &icon, const CHyprColor &col);
    [[nodiscard]] SP<Render::ITexture> closeGlyph() const;

    void putSnapshot(const PHLWINDOW &win, const SP<Render::ITexture> &tex);
    [[nodiscard]] SP<Render::ITexture> snapshot(const PHLWINDOW &win) const;
    void clearSnapshots() { m_snapshots.clear(); }
    void clearArt() { m_subjectArt.clear(); }

  private:
    template <typename Map, typename T>
    SP<Render::ITexture> labelIn(Map &map, const CWeakPointer<T> &owner,
                                 const std::string &text, const CHyprColor &col,
                                 int size, int maxW) {
      const auto key = owner.lock().get();
      if (!key)
        return nullptr;
      auto &slot = map[key];
            // Recycled address: an expired reference is the only proof the entry is stale.
      if (slot.owner.expired())
        slot = {};
      slot.owner = owner;
      auto &e = slot.entry;
      if (e.matches(text, maxW, size, col))
        return e.tex;
      e = LabelEntry{truncate(text, col, size, maxW), maxW, size, col, nullptr};
      e.tex = render(e.text, col, size);
      return e.tex;
    }

    [[nodiscard]] static SP<Render::ITexture> render(const std::string &s,
                                                     const CHyprColor &col, int size);
    [[nodiscard]] static std::string truncate(const std::string &text,
                                              const CHyprColor &col, int size,
                                              int maxW);

    struct WinLabel {
      PHLWINDOWREF owner;
      LabelEntry entry;
    };
    struct WsLabel {
      PHLWORKSPACEREF owner;
      LabelEntry entry;
    };
    struct WinSnapshot {
      PHLWINDOWREF owner;
      SP<Render::ITexture> tex;
    };

    std::unordered_map<Desktop::View::CWindow *, WinLabel> m_winLabels;
    std::unordered_map<CWorkspace *, WsLabel> m_wsLabels;
        // Kept apart: a negative special workspace id would otherwise land on a Slot key.
    std::unordered_map<Slot, LabelEntry> m_slotLabels;
    std::unordered_map<WORKSPACEID, LabelEntry> m_virtualLabels;
    std::unordered_map<Desktop::View::CWindow *, WinSnapshot> m_snapshots;
    SubjectCache<LabelEntry> m_subjectLabels;
    SubjectCache<SP<Render::ITexture>> m_subjectArt;
    SP<Render::ITexture> m_glyph;
    std::string m_glyphIcon;
};

} // namespace hyprview
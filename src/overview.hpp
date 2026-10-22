#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace Render {
class ITexture;
class IFramebuffer;
}

namespace Config {
class CGradientValueData; // general:col.active_border's real type
}

#include "anim.hpp"
#include "anim_recipes.hpp"
#include "clock/clocks.hpp"
#include "clock/placement.hpp"
#include "clock/progress.hpp"
#include "clock/clock.hpp"
#include "view/state.hpp"
#include "model/effects.hpp"
#include "model/recency.hpp"
#include "model/scene.hpp"
#include "model/strip.hpp"
#include "model/model.hpp"
#include "pixel/cache.hpp"
#include "render/backdrop.hpp"
#include "render/frame_pump.hpp"
#include "render/geometry.hpp"
#include "model/window_source.hpp"
#include "model/tab_match.hpp"
#include "model/tab_source.hpp"
#include "session/hooks.hpp"
#include "session/layer_visibility.hpp"
#include "session/pending_click.hpp"
#include "session/preview_visibility.hpp"
#include "render/tile_look.hpp"
#include "blur.hpp"
#include "cursor.hpp"
#include "layout.hpp"

class CFunctionHook;
class CEventLoopTimer;

namespace hyprview {

class Overview : public model::IWindowHost {
public:
  explicit Overview(HANDLE handle);
  // Automation seam for hyprview:cmd: "" on success, a reason otherwise.
  std::string runCommand(const std::string &args);
  ~Overview();

  bool initialize();

  void toggle();

  void open();
  void close();
  void hardClose();           // animation-free teardown for the UNLOAD path
  void toggleDesktop();
  void toggleAllWorkspaces();
  void toggleCarousel();
  void switchDomain(model::Domain d);
  void toggleBrowserView();
  void altTabInvoke(bool reverse);

  void renderStage(eRenderStage stage);
  void paint();

  render::BackdropFrame backdropFrame() const;
  void renderBackdrop() const;
  void primeBackdrop();

  bool stripSlotGrabbed(size_t i, size_t j) const;
  void renderStrip() const;
  void renderStripWindows() const;
  void renderStripButtons() const;
  void renderPulses(bool strip) const;
  void renderTiles() const;
  void renderCarousel() const;
  [[nodiscard]] bool carouselMode() const { return m_view.run(); }
  void relayoutCarousel();
  void renderGhosts() const;
  void renderDragTile() const;
  void renderDragWindow() const;
  void renderCursorOnTop() const;

  void rearmanim() const { m_pump.ensure(animBusy(), m_monitor.lock()); }
  void ensureAnimPump() { m_pump.ensure(animBusy(), m_monitor.lock()); }

  void finishPendingDeactivate();

  bool isAboveLayer(const std::string &ns) const;
  void renderAboveLayers() const;

  bool onMouseButton(const IPointer::SButtonEvent &e);
  bool onMouseAxis(const IPointer::SAxisEvent &e);
  void onMouseMove();
  void updateHover();
  void onKey(const IKeyboard::SKeyEvent &e, bool &cancel);
  [[nodiscard]] bool belongs(const PHLWINDOW &w) const override;

  // Called once per run, never per frame: the feed file changes only when the browser does.
  void loadTabs();

  bool shouldHideWindow(const PHLWINDOW &w, const PHLMONITOR &m) const;
  [[nodiscard]] bool isTileWindow(const PHLWINDOW &w) const;
  // Where this window's preview actually is: the tile box and the strip cards that show it.
  void damageWindowPreview(const PHLWINDOW &w) const;

  // Three axes, not refinements of one another (AGENTS.md 3.3).
  [[nodiscard]] bool active() const { return m_life.active(); }
  [[nodiscard]] bool openedOrParked() const { return m_life.ownsScreen(); }
  [[nodiscard]] bool opening() const { return m_life.opening(); }
  [[nodiscard]] std::uint64_t frameTick() const { return m_pump.tick(); }
  [[nodiscard]] PHLMONITOR monitor() const { return m_monitor.lock(); }
  [[nodiscard]] bool blurEnabled() const;
  [[nodiscard]] SP<Render::ITexture> backdropBlurTexture() const {
    return m_backdrop.blurTexture();
  }
  // Composited (unblurred) backdrop; whether one exists is backdrop.cpp's decision.
  [[nodiscard]] SP<Render::ITexture> backdropSourceTexture() const {
    return m_backdrop.composite();
  }
  // A stale cache still holds the last blur, which the entry path falls back to.
  [[nodiscard]] bool backdropBlurStale() const {
    return m_backdrop.blurStale();
  }
  [[nodiscard]] bool backdropDrawn() const { return m_backdrop.drawn(); }
  [[nodiscard]] SP<Render::ITexture> snapshot(const PHLWINDOW &w) const {
    return m_pixel.snapshot(w);
  }
  [[nodiscard]] bool snapshotMode() const;

private:
  HANDLE m_handle = nullptr;
  std::chrono::steady_clock::time_point m_openStamp{}; // frame-trace t=0
  clock::Placement m_place;
  mutable render::FramePump m_pump;

  // Parked is neither visible nor closed: nothing drawn, windows already lost focus.
  model::Lifecycle m_life;
  model::Recency m_recency;
  PHLWINDOWREF m_lastFocus;
  PHLWINDOWREF m_focusOutside;
  // Applied BEFORE the run resolves its centre, so the opening aims at the target window.
  int m_pendingStep = 0;
  void syncRecency();
  void pruneRecency();
  model::Scene m_scene;
  // Holds a reference to this object as its host; both are members, so neither outlives.
  model::WindowsContentSource m_windows{*this};
  // Browser side, read in rather than dialled: a file under XDG_RUNTIME_DIR, no socket and no thread (AGENTS.md 1).
  model::TabsContentSource m_tabs;
  model::BrowserLinks m_links;
  int m_showDiscarded = -1;
  int m_allForced = -1;
  // The window behind a tile; null for a tab or a gone window (resolution lives here, not on Tile).
  PHLWINDOW tileWindow(const model::Tile &t) const { return m_windows.windowOf(t.subj); }

  struct Card {
    PHLWINDOW window = {};
    bool      paintsOwnContent = false;
    bool      coversChrome = false;     // occludes the strip and yields to it

    [[nodiscard]] bool isTab() const { return !window; }
    [[nodiscard]] bool hasWindow() const { return static_cast<bool>(window); }
  };
  [[nodiscard]] Card cardOf(const model::Tile &t) const {
    const auto w = m_windows.windowOf(t.subj);
    return Card{w, static_cast<bool>(w), static_cast<bool>(w)};
  }

  static std::string browserSideFile(const std::string &leaf) {
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    return (rt && *rt ? std::string(rt) : std::string("/tmp")) + "/hyprview" + leaf;
  }
  [[nodiscard]] static model::SubjectRef subjectOf(const PHLWINDOW &w) {
    return w ? model::SubjectRef{reinterpret_cast<std::uintptr_t>(w.get()), 0}
             : model::SubjectRef{};
  }
  model::Effects m_fx;
  std::vector<PHLWORKSPACEREF> m_newWs;
  view::State m_view;
  mutable std::chrono::steady_clock::time_point m_lastAnimTick{};
  void startTileGlide(
      const std::vector<std::pair<model::SubjectRef, LRect>> &oldBoxes,
      double seamRunW = 0.0);
  PHLMONITORREF m_monitor;
  PHLWORKSPACEREF m_workspace;
  PHLWORKSPACEREF m_liveWsAtOpen; // for exit_on_switch
  model::Strip m_strip;
  mutable pixel::PixelCache m_pixel;
  double m_scrollAccum = 0.0; // sub-card wheel leftover, so slow paging still moves
  session::LayerVisibility m_layers;
  PHLWINDOWREF m_pendingWindow;


  int m_newCardId = 0;
  bool m_newCardAnim = false;
  model::ScrollState m_scroll;
  void animateStripTo(double from, double to);
  model::GridScroll m_gridScroll;
  void animateGridTo(double from, double to);
  void followGrid(int i);
  bool animBusy() const;
  // Clocks that outlive the master: the pump must name every clock, or the next one added never arms.
  bool secondaryAnimsActive() const {
    return m_place.clocks.anyRunning(anim::cfg(entryLeaf()).ms, anim::cfg(ghostLeaf()).ms,
                               anim::cfg(anim::Id::StripStep).ms);
  }
  double blurAlpha() const; // backdrop strength, on the open/close clock
  // A window that really occupies the desktop; tells an empty workspace from a busy one.
  static bool workspaceHasWindows(const PHLWORKSPACE &ws);
  // Windowed exit is done: tiles landed and Hyprland has spent the forced blur frames.
  bool backdropSettled() const;
  // Re-read on every open and close: the landing workspace can change while the overview is up.
  mutable bool m_landingEmpty = true;
  // The group of the picked card: a tab's group is its browser window.
  mutable model::GroupRef m_landingGroup;
  double entryProgress(int i) const;
  double entryNow(const model::Tile &t, int i) const;
  anim::Id entryLeaf() const;
  anim::Id ghostLeaf() const;
  anim::Id glideLeaf() const;
  double entryFade(size_t i) const;
  // How far the close has run, 0 -> 1; every exit fraction reads it through this.
  double exitT() const;
  double openT() const;
  [[nodiscard]] clock::Placement::Frame frame() const;
  // Clears ws alpha / fullscreen alpha / visibleRegion, or an off-ws window previews empty; restore undoes it.
  void forcePreviewVisibility();
  void restorePreviewVisibility();
  session::PreviewVisibility m_previewVis;
  void kickPulse(model::SubjectRef s);
  void beginSwapFX(const PHLWINDOW &w, const LRect &from,
                   anim::Move style, double ms, const std::string &curve,
                   bool toStrip = false);
  void landAfterMove(const PHLWINDOW &w, const LRect &oldBox, double ms,
                     const std::string &curve, bool strip);
  // Both surfaces of a swap: the dragged one from the cursor, the bystander from where it was.
  void landAfterSwap(const PHLWINDOW &w, const LRect &fromGrid,
                     const LRect &fromStrip, const LRect &drop, double ms,
                     const std::string &curve, bool draggedOnStrip);
  [[nodiscard]] bool hasStripThumb(const PHLWINDOW &w) const;
  [[nodiscard]] LRect stripThumbRect(const PHLWINDOW &w) const;
  // Per surface: each stands down only for its own flight.
  [[nodiscard]] bool swapfxActive(const PHLWINDOW &w, bool strip) const;
  [[nodiscard]] bool swapfxActiveSubject(model::SubjectRef s, bool strip) const {
    return m_fx.swapActive(s, strip);
  }

  // The window a card belongs to: the window itself, or the browser window a tab lives in.
  PHLWINDOW ownerWindow(const model::Tile &t) const {
    if (const auto w = tileWindow(t))
      return w;
    const auto group = m_tabs.groupOf(t.subj);
    const auto hostId = group.id ? m_links.hostOf(group.id) : 0;
    return hostId ? m_windows.windowOf(model::SubjectRef{hostId, 0}) : PHLWINDOW{};
  }

  // A tab's art is the page as it was last seen, else its icon, else its caption.
  SP<Render::ITexture> tabArt(const model::Tile &t) const {
    if (!cardOf(t).isTab())
      return nullptr;
    const auto id = std::to_string(t.subj.id);
    return m_pixel.putPicture(t.subj, browserSideFile("/previews/" + id + ".png"), {});
  }
  SP<Render::ITexture> windowIcon(const PHLWINDOW &w, int px) const {
    return w ? m_pixel.appIcon(w->m_class, px) : nullptr;
  }
  SP<Render::ITexture> browserIcon(const model::Tile &t, int px) const {
    if (!cardOf(t).isTab())
      return nullptr;
    const auto group = m_tabs.groupOf(t.subj);
    const auto hostId = group.id ? m_links.hostOf(group.id) : 0;
    const auto host = hostId ? m_windows.windowOf(model::SubjectRef{hostId, 0}) : PHLWINDOW{};
    return host ? m_pixel.appIcon(host->m_class, px) : nullptr;
  }
  SP<Render::ITexture> tabIcon(const model::Tile &t) const {
    if (!cardOf(t).isTab())
      return nullptr;
    return m_pixel.putIcon(t.subj, browserSideFile("/icons/" + std::to_string(t.subj.id) + ".png"));
  }
  void renderSwapFX() const;
  // Every box this window flies to on one surface, shared with the trace so drawn and reported cannot differ.
  [[nodiscard]] std::vector<LRect> swapDests(const PHLWINDOW &w, bool strip) const;

  session::PendingClick m_click;

  model::Drag m_drag;
  LRect m_lastDragBox; // drag box at RELEASE, the `from` for landings
  bool m_altTabbing = false;
  bool m_commitPending = false;

  session::Hooks m_hooks;

  CCursorModule m_cursor; // KMS plane preferred, software erases-and-redraws

  mutable render::Backdrop m_backdrop;
  void updateSnapshots();


  using Anchor = render::Anchor;
  Anchor stripAnchor() const;
  bool stripHorizontal() const;
  double stripThickness() const;
  double stripOffset() const;
  LRect stripBand() const;
  Vector2D stripSlide(double e) const;
  Vector2D stripPush(const LRect &band) const;
  // THE strip displacement: band, cards, their window previews and the hit-test all go through this.
  Vector2D stripOffset(double e) const;
  std::vector<LRect> coveringCards() const;
  Vector2D stripScroll() const;
  LRect stripCardAt(size_t i) const;
  int stripItemAt(double lx, double ly) const;
  int tileAt(double lx, double ly) const;
  bool showAllWorkspaces() const;
  bool tileBelongs(const PHLWINDOW &w, const PHLMONITOR &m,
                   const PHLWORKSPACE &ws) const; // buildTiles + syncTiles agree
  void buildTiles();
  void buildStrip();
  void layoutTiles();
  void updateAnimation();
  void deactivate();
  // Boundary policy (user-specified): windows hard-switch at BOTH edges, no per-window fades.
  double eased() const;
  double animDuration() const; // read LIVE, so config applies in-flight
  double glideDur() const { return m_place.clocks.tileMs > 0 ? m_place.clocks.tileMs : anim::cfg(glideLeaf()).ms; }
  bool entrySettled() const;
  uint32_t altTabModBit() const;
  double newCardDur() const { return anim::cfg(anim::Id::NewCard).ms; }
  double tileProgress(int i) const;
  LRect currentBox(const model::Tile &t, int i) const;
  // The drawn box of tile i: currentBox() letterboxed to the window's own aspect.
  LRect tileRect(size_t i) const;
  LRect dragBox() const;
  LRect dragStripBox() const;
  double dragLiftProgress() const;
  LRect dragVisualBox() const;
  int draggedTile() const;
  void dropLiftIfNotMoved();
  void drawTile(size_t i, const TileLook &look, bool withCaption = true) const;
  void drawTileCaption(int i, const TileLook &look) const;
  LRect tileBox(size_t i, const LRect &slot) const;
  void switchToWorkspace(const model::StripItem &it, bool instant = false);
  void dropOnWorkspace(const PHLWINDOW &w, const model::StripItem &it);
  // Shared tail of both release paths: move to another card, or swap with its last-focused window (RMB).
  bool dropOnStripCard(const PHLWINDOW &w, double lx, double ly, int skipItem);
  model::StripItem *homeStripCardFor(const PHLWINDOW &w);
  void swapOnWorkspace(const PHLWINDOW &w, const model::StripItem &it);
  // `fromA` is where the initiator ACTUALLY is (under the cursor at release); empty means "use the card slot".
  void swapTiles(int a, int b, const LRect &fromA = {});
  bool swapWindows(const PHLWINDOW &a, const PHLWINDOW &b,
                   const LRect &fromA = {}, bool draggedOnStrip = true);
  void drawPulseRing(const CBox &boxPx, int round, float roundPow,
                     const CHyprColor &col, double p) const;
  void addWorkspace();
  void closeWorkspaceWindows(const model::StripItem &it);
  void setDesktopMode(bool on);
  LRect closeButtonRect(const LRect &tile) const;
  bool closeButtonsAlwaysOn() const;
  bool closeOnDoubleClick() const;
  bool tileCloseButtons() const;
  void cancelPendingClick();
  double newCardScale() const;
  float blurStrength() const;
  int blurPasses() const;
  int blurSize() const;
  int blurResolution() const;
  LRect stripWinSlotRect(const model::StripItem &it, const LRect &card,
                   size_t j) const;
  void drawDragStripChrome() const;
  bool keyMatches(int keycode, uint32_t mods, const std::string &combo) const;
  int keyIndex(int keycode, uint32_t mods, const std::string &combo) const;
  void moveSelection(int dx, int dy);
  void activateSelection();
  // A target off the DISPLAYED workspace collapses onto it BEFORE the glide, so it aims at the real final position.
  void focusAndClose(const PHLWINDOW &w, Desktop::eFocusReason reason);
  // A tab card has no window: the browser is asked for the tab and its host is focused.
  void activateTile(int idx, Desktop::eFocusReason reason);
  [[nodiscard]] bool discardedShown() const;
  void setDiscardedShown(bool on);
  bool writeTabRequest(std::uint64_t browserWindow, std::int64_t tab);
  void requestTabShots();
  bool crossesRealWorkspace(const PHLWINDOW &w) const;
  void closeTileWindow(int i);
  LRect killZoneRect() const;
  double killZoneGlow(double lx, double ly) const;
  void renderKillZone() const;
  double m_killGlow = 0.0; // 1 at the zone, 0 a screen-width away, while dragging
  // Keyed by SUBJECT: a window-keyed vector drops every tab.
  std::vector<std::pair<model::SubjectRef, LRect>> captureCurrentBoxes(
      PHLWINDOW exclude = {}) const;
  void replayReflow(std::vector<std::pair<model::SubjectRef, LRect>> &oldBoxes);
  void releaseNewWorkspaces();
  void syncTiles();
  void stepWorkspace(int dir);
  void hideLayers();
  void restoreLayers();
  void restoreFill();
  void stepAltTab(int dir);
  bool stepRun(int dir);
  // Where the selected card stands in the run: the counter counts THIS, not the tile index.
  [[nodiscard]] int runPosition() const;
  // A card that joined or left the run has no slot to glide between. See keys.cpp.
  void pinRunEdges(const std::vector<char> &wasOnRun);
  int tileOf(const PHLWINDOW &w) const;
  void damage() const { m_pump.damage(m_monitor.lock()); }
};

}

inline hyprview::Overview *g_overview = nullptr;

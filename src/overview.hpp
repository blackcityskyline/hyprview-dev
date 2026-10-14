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
} // namespace Render

namespace Config {
class CGradientValueData; // general:col.active_border's real type
}

#include "anim.hpp"
#include "clock/clocks.hpp"
#include "clock/clock.hpp"
#include "view/state.hpp"
#include "model/effects.hpp"
#include "model/recency.hpp"
#include "model/scene.hpp"
#include "model/strip.hpp"
#include "model/model.hpp"
#include "pixel/cache.hpp"
#include "render/backdrop.hpp"
#include "render/geometry.hpp"
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

namespace gloview {

// Mission Control-style overview: a workspace strip over live window previews,
// drawn compositor-side while the real windows are hidden.
class Overview {
public:
  explicit Overview(HANDLE handle);
  // Automation seam for `hyprctl dispatch gloview:cmd ...`: state / sel / swap /
  // movews. Returns "" on success, a reason otherwise.
  std::string runCommand(const std::string &args);
  ~Overview();

  bool initialize();

  void toggle();

  void open();
  void close();
  void hardClose();           // animation-free teardown for the UNLOAD path
  void toggleDesktop();       // open, or switch into free-arrange mode
  void toggleAllWorkspaces();
  void toggleCarousel();   // flip the run on/off; on_open picks the start
  void altTabInvoke(bool reverse); // opens+seeds if closed, else advances

  void renderStage(eRenderStage stage);
  void paint();

  void renderBackdrop() const;
  void primeBackdrop();

  bool stripSlotGrabbed(size_t i, size_t j) const;
  void renderStrip() const;
  void renderStripWindows() const;
  void renderStripButtons() const;
  void renderPulses(bool strip) const;
  void renderTiles() const;
  void renderCarousel() const;
  bool carouselMode() const;
  void relayoutCarousel();
  void renderGhosts() const;
  void renderDragTile() const;
  void renderDragWindow() const;
  void renderCursorOnTop() const;

  void rearmanim() const;
  void ensureAnimPump();

  void finishPendingDeactivate();

  bool isAboveLayer(const std::string &ns) const;
  void renderAboveLayers() const;

  bool onMouseButton(const IPointer::SButtonEvent &e);
  bool onMouseAxis(const IPointer::SAxisEvent &e);
  void onMouseMove();
  void updateHover();
  void onKey(const IKeyboard::SKeyEvent &e, bool &cancel);
  bool shouldHideWindow(const PHLWINDOW &w, const PHLMONITOR &m) const;
  [[nodiscard]] bool isTileWindow(const PHLWINDOW &w) const;

  // The three lifecycle questions, delegated. They used to be three free booleans and each
  // one is still three booleans' worth of truth, so a reader could not tell which question
  // any given site was asking.
  //
  //   active()       visible -- the compositor should be painting the overlay. Parked is
  //                  NOT active: nothing is drawn yet.
  //   openedOrParked() owns the screen -- parked counts, because the windows must lose
  //                  focus on the first parked frame, not when the overlay appears. Every
  //                  caller that tested m_active right after open() must test this instead,
  //                  or it reads a waiting open as a failed one -- that is what silently
  //                  disarmed alt+tab.
  //   opening()      the entry animation is playing. Not the same axis: replayReflow and
  //                  switchToWorkspace set it on an overlay that is already on screen.
  [[nodiscard]] bool active() const { return m_life.active(); }
  [[nodiscard]] bool openedOrParked() const { return m_life.ownsScreen(); }
  [[nodiscard]] bool opening() const { return m_life.opening(); }
  [[nodiscard]] std::uint64_t frameTick() const { return m_frameTick; }
  [[nodiscard]] PHLMONITOR monitor() const { return m_monitor.lock(); }
  [[nodiscard]] bool blurEnabled() const;
  [[nodiscard]] SP<Render::ITexture> backdropBlurTexture() const {
    return m_backdrop.blurTexture();
  }
  // The composited (unblurred) backdrop, for anything that draws under the windows --
  // strip cards fill the gaps their windows leave with it. Reads the composite's own
  // state rather than reaching into its framebuffer: whether there IS a composite is
  // backdrop.cpp's decision, not the caller's.
  [[nodiscard]] SP<Render::ITexture> backdropSourceTexture() const {
    return m_backdrop.composite();
  }
  // True when the blur cache must be recomputed. Separate from having a texture:
  // a stale cache still holds the last blur, which is what the entry path falls back to.
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
  std::uint64_t m_frameTick = 0;

  // Who owns the screen, and whether the entry animation is playing.
  //
  // Six booleans used to be this, and the two facts that decide whether the overlay paints
  // at all lived in their write pattern rather than anywhere checkable: m_active and
  // m_pendingOpen are ONE axis (open parks, primeBackdrop promotes, close clears both),
  // while m_opening is not on it -- replayReflow and switchToWorkspace set it on an overlay
  // already on screen, which is why nine sites spell `m_active && m_opening`.
  //
  // Parked is the state that is neither visible nor closed: nothing is drawn, but the
  // windows already lost focus. Painting late beats painting black -- the frame the
  // overview is first visible had no backdrop of its own, and the wallpaper texture is
  // mid-recreation right then.
  model::Lifecycle m_life;
  model::Recency m_recency;
  PHLWINDOWREF m_lastFocus;
  // UNUSED. Never assigned -- only reset, in close() and deactivate(). The comment that used
  // to sit here claimed it held "the compositor's focus when the overlay opened, so hovering
  // a card cannot change what a passthrough bind acts on". No such snapshot exists:
  // input/keys.cpp reads Desktop::focusState()->window() live, and nothing else stores it.
  //
  // The property it described is probably true anyway, for a different reason: hover is
  // model-only (Scene::hover never touches the compositor) and open() drops focus from the
  // windows. That is a fact about the code, not about this field. Left in place rather than
  // deleted because the intent may still be wanted -- implementing it would mean deciding
  // what a passthrough bind should act on while the overview is up, which is a product
  // question, not a refactor.
  PHLWINDOWREF m_focusOutside;
  // A step the run must apply BEFORE it resolves its centre, so the opening
  // motion plays for the target window instead of landing on one and swapping to
  // another at the end. Consumed by layoutTiles, never by a key path.
  int m_pendingStep = 0;
  void syncRecency();
  void pruneRecency();
  model::Scene m_scene;
  model::Effects m_fx;
  view::State m_view;
  clock::Clocks m_clocks;
  double m_progress = 0.0;
  mutable std::chrono::steady_clock::time_point m_lastAnimTick{};
  void startTileGlide(
      const std::vector<std::pair<PHLWINDOW, LRect>> &oldBoxes);
  PHLMONITORREF m_monitor;
  PHLWORKSPACEREF m_workspace;
  PHLWORKSPACEREF m_liveWsAtOpen; // for exit_on_switch
  model::Strip m_strip;
  mutable pixel::PixelCache m_pixel;
  double m_scrollAccum = 0.0; // sub-card wheel leftover, so slow paging still moves
  session::LayerVisibility m_layers;
  PHLWINDOWREF m_pendingFocus;


  int m_newCardId = 0;
  bool m_newCardAnim = false;
  // The card group's scroll offset, its endpoints and its range. Four doubles that had to
  // agree on [0, max] and did not have anything making them: the endpoints were clamped by
  // the animateStripTo() caller and the current position by nothing at all.
  model::ScrollState m_scroll;
  void animateStripTo(double from, double to);
  anim::Cfg m_glide, m_entry, m_ghost, m_lift;
  anim::Move m_enterStyle{anim::Travel::Slide, anim::Axis::Horizontal}, m_exitStyle{anim::Travel::Slide, anim::Axis::Horizontal};
  bool animBusy() const;
  // Clocks that outlive the master: the pump must name every clock or the next one
  // added never arms it.
  bool secondaryAnimsActive() const {
    return m_clocks.anyRunning(anim::cfg(entryLeaf()).ms, anim::cfg(ghostLeaf()).ms,
                               anim::cfg(anim::Id::StripStep).ms);
  }
  // Read on the call site: a config edit mid-fade retimes the rest of the ramp.
  double blurAlpha() const; // backdrop strength, on the open/close clock
  // The predicate build.cpp/actions.cpp each re-spelled: a window that really occupies
  // the desktop. First true one on `ws` is what tells an empty workspace from a busy one.
  static bool workspaceHasWindows(const PHLWORKSPACE &ws);
  // Windowed exit is done: tiles landed and Hyprland has spent the forced blur frames.
  bool backdropSettled() const;
  // Landing workspace empty? Re-read on every open and close: the landing workspace can
  // change while the overview is up (all-workspaces, digit jump).
  mutable bool m_landingEmpty = true;
  double entryProgress(int i) const; // staggered 0..1 for tile i
  // The above, gated on the tile actually playing its entry (Tile::landed < 1).
  double entryNow(const model::Tile &t, int i) const;
  anim::Id entryLeaf() const;
  anim::Id ghostLeaf() const;
  anim::Id glideLeaf() const;
  double entryFade(size_t i) const;
  // How far the close has run, 0 -> 1. Every exit fraction reads it through this.
  double exitT() const;
  // Will this tile's window actually be on screen when the overlay leaves? Only then is
  // its natural box a destination rather than a rectangle on a workspace that is not
  // coming. See anim::tileExit.
  [[nodiscard]] bool tileGoesHome(const model::Tile &t) const;
  // Clear the three pieces of state that make the compositor skip a window that is not on
  // the active workspace: the workspace's alpha, the window's fullscreen alpha, and each
  // surface's visibleRegion. ScrollOverview does the same; without it a window from
  // another workspace previews as an empty frame. Restored on the way out.
  void forcePreviewVisibility();
  void restorePreviewVisibility();
  session::PreviewVisibility m_previewVis;
  void kickPulse(const PHLWINDOW &w);
  void beginSwapFX(const PHLWINDOW &w, const LRect &from,
                   anim::Move style, double ms, const std::string &curve,
                   bool toStrip = false);
  void landAfterMove(const PHLWINDOW &w, const LRect &oldBox, double ms,
                     const std::string &curve, bool strip);
  // Both surfaces of a swap: the dragged one from the cursor, the bystander from where
  // it was. See the note on the definition.
  void landAfterSwap(const PHLWINDOW &w, const LRect &fromGrid,
                     const LRect &fromStrip, const LRect &drop, double ms,
                     const std::string &curve, bool draggedOnStrip);
  [[nodiscard]] bool hasStripThumb(const PHLWINDOW &w) const;
  // Where a window's strip thumbnail sits right now, empty if it has none.
  [[nodiscard]] LRect stripThumbRect(const PHLWINDOW &w) const;
  // Is this window flying on THIS surface? The grid asks false, the strip true, and each
  // one only stands down for its own flight -- a shared predicate made the strip's
  // thumbnail disappear while the card flew.
  [[nodiscard]] bool swapfxActive(const PHLWINDOW &w, bool strip) const;
  void renderSwapFX() const;
  // Every box this window flies to on one surface. Shared by the painter and the
  // trace so a reported target cannot differ from a drawn one.
  [[nodiscard]] std::vector<LRect> swapDests(const PHLWINDOW &w, bool strip) const;

  session::PendingClick m_click;

  model::Drag m_drag; // pointer PRESS..RELEASE, both drag kinds
  LRect m_lastDragBox; // drag box at RELEASE, the `from` for landings
  bool m_altTabbing = false;
  bool m_commitPending = false;

  session::Hooks m_hooks;

  SP<CEventLoopTimer> m_animPump; // null when no animation is running
  CCursorModule m_cursor; // KMS plane preferred, software erases-and-redraws

  mutable render::Backdrop m_backdrop;
  void updateSnapshots();


  using Anchor = render::Anchor;
  Anchor stripAnchor() const; // falls back to bar_position
  bool stripHorizontal() const;
  double stripThickness() const; // perpendicular to the anchored edge
  double stripOffset() const;
  LRect stripBand() const;
  Vector2D stripSlide(double e) const; // reveal slide-in at progress e
  Vector2D stripPush(const LRect &band) const; // band yields to a preview that reached it
  // THE strip displacement. Band, cards, the window previews inside them and the hit-test all
  // go through this: computing it in each of them meant the previews and the hit-test drifted
  // from the cards by exactly the push (measured: window previews stopped following the
  // motion on any workspace that HAS windows).
  Vector2D stripOffset(double e) const;
  Vector2D pushOut(const LRect &r) const; // chrome yields to a preview
  Vector2D stripScroll() const;
  LRect stripCardAt(size_t i) const;   // card shifted by the scroll
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
  // Boundary policy (user-specified): real windows hard-switch at BOTH edges, NO
  // per-window fades — a fading original under the preview reads as double vision.
  double eased() const; // chrome opacity / backdrop progress
  double animDuration() const; // read LIVE, so config applies in-flight
  [[nodiscard]] anim::Id masterId() const; // which leaf drives the master clock
  double glideDur() const { return m_clocks.tileMs > 0 ? m_clocks.tileMs : anim::cfg(glideLeaf()).ms; }
  bool entrySettled() const;
  uint32_t altTabModBit() const;
  double newCardDur() const { return anim::cfg(anim::Id::NewCard).ms; }
  double tileProgress(int i) const;
  LRect currentBox(const model::Tile &t, int i) const;
  // The drawn box of tile i: currentBox() letterboxed to the window's own aspect.
  // Every consumer asking "where is tile i" wants this, not the pair spelled out.
  LRect tileRect(size_t i) const;
  LRect dragBox() const;
  LRect dragStripBox() const;
  double dragLiftProgress() const;
  LRect dragVisualBox() const;
  int draggedTile() const;
  void dropLiftIfNotMoved();
  void drawTile(size_t i, const TileLook &look) const;
  LRect tileBox(size_t i, const LRect &slot) const;
  void switchToWorkspace(const model::StripItem &it, bool instant = false);
  void dropOnWorkspace(const PHLWINDOW &w, const model::StripItem &it);
  // Shared tail of both release paths: a point on another card moves w there, or
  // swaps with its last-focused window (RMB).
  bool dropOnStripCard(const PHLWINDOW &w, double lx, double ly, int skipItem);
  model::StripItem *homeStripCardFor(const PHLWINDOW &w);
  void swapOnWorkspace(const PHLWINDOW &w, const model::StripItem &it);
  // `fromA` is where the initiator ACTUALLY is -- under the cursor at release, not the
  // card slot it was dragged out of. Empty means "use the card slot".
  void swapTiles(int a, int b, const LRect &fromA = {});
  bool swapWindows(const PHLWINDOW &a, const PHLWINDOW &b,
                   const LRect &fromA = {}, bool draggedOnStrip = true);
  void drawPulseRing(const CBox &boxPx, int round, float roundPow,
                     const CHyprColor &col, double p) const;
  void addWorkspace();
  void closeWorkspaceWindows(const model::StripItem &it);
  void setDesktopMode(bool on); // flip grid<->canvas, purely visual
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
  // A target off the DISPLAYED workspace collapses onto it and rebuilds BEFORE
  // the glide, so it aims at the real final position.
  void focusAndClose(const PHLWINDOW &w, Desktop::eFocusReason reason);
  bool crossesRealWorkspace(const PHLWINDOW &w) const;
  void closeTileWindow(int i);
  LRect killZoneRect() const;
  double killZoneGlow(double lx, double ly) const;
  void renderKillZone() const;
  double m_killGlow = 0.0; // 1 at the zone, 0 a screen-width away, while dragging
  std::vector<std::pair<PHLWINDOW, LRect>> captureCurrentBoxes(
      PHLWINDOW exclude = {}) const;
  void replayReflow(std::vector<std::pair<PHLWINDOW, LRect>> &oldBoxes);
  void releaseNewWorkspaces();
  void syncTiles();
  void stepWorkspace(int dir);
  void hideLayers();
  void restoreLayers();
  void restoreFill();
  void stepAltTab(int dir);
  int tileOf(const PHLWINDOW &w) const;
  void damage() const;
};

} // namespace gloview

inline gloview::Overview *g_overview = nullptr;

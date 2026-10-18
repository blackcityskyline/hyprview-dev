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

namespace gloview {

// Mission Control-style overview: a workspace strip over live window previews,
// drawn compositor-side while the real windows are hidden.
class Overview : public model::IWindowHost {
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
  void switchDomain(model::Domain d); // window view <-> browser-tab view, live
  void toggleBrowserView();  // what key_browser calls: flip to the other of the two
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
  // Not in carousel_view.cpp: it is a read of the run's mode and nothing else, so it lives
  // with the mode.
  [[nodiscard]] bool carouselMode() const { return m_view.run(); }
  void relayoutCarousel();
  void renderGhosts() const;
  void renderDragTile() const;
  void renderDragWindow() const;
  void renderCursorOnTop() const;

  // The pump, asked three ways: arm now, re-check from inside its own callback, or just
  // repaint. All three are the frame pump, none is the run's lifecycle.
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
  // IWindowHost's inbound port: the one question the window source cannot answer itself.
  [[nodiscard]] bool belongs(const PHLWINDOW &w) const override;

  // Reads what the browser side has written and re-decides the matching. Called when a run
  // opens, never per frame: the file changes when the browser does, and re-reading it per
  // frame would be a syscall per tile per frame to learn nothing new.
  void loadTabs();

  bool shouldHideWindow(const PHLWINDOW &w, const PHLMONITOR &m) const;
  [[nodiscard]] bool isTileWindow(const PHLWINDOW &w) const;

  // The three lifecycle questions, delegated. active() is visible; openedOrParked() is
  // ownership of the screen, parked included, because the windows must lose focus on the
  // first parked frame rather than when the overlay appears -- a caller testing active()
  // right after open() reads a waiting open as a failed one, which is how alt+tab got
  // silently disarmed. opening() is a third axis, not a refinement of the other two.
  [[nodiscard]] bool active() const { return m_life.active(); }
  [[nodiscard]] bool openedOrParked() const { return m_life.ownsScreen(); }
  [[nodiscard]] bool opening() const { return m_life.opening(); }
  [[nodiscard]] std::uint64_t frameTick() const { return m_pump.tick(); }
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
  // Separate from having a texture: a stale cache still holds the last blur, which the
  // entry path falls back to.
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

  // Who owns the screen, and whether the entry animation is playing. Six booleans used to be
  // this and the facts that decide whether the overlay paints lived in their write pattern.
  // Parked is neither visible nor closed: nothing is drawn, the windows already lost focus.
  // Painting late beats painting black -- the first visible frame has no backdrop of its own
  // while the wallpaper texture is mid-recreation.
  model::Lifecycle m_life;
  model::Recency m_recency;
  PHLWINDOWREF m_lastFocus;
  // UNUSED: never assigned, only reset in close() and deactivate(). A comment here once
  // claimed it held the compositor's focus so a passthrough bind could not be redirected by
  // hover; no such snapshot exists -- keys.cpp reads focusState()->window() live. The property
  // holds anyway because hover is model-only and open() drops window focus. Kept because what
  // a passthrough bind should act on while the overview is up is a product question.
  PHLWINDOWREF m_focusOutside;
  // Applied BEFORE the run resolves its centre, so the opening motion plays for the target
  // window instead of landing on one and swapping at the end.
  int m_pendingStep = 0;
  void syncRecency();
  void pruneRecency();
  model::Scene m_scene;
  // Holds a reference to this object as its host; both are members, so neither outlives.
  model::WindowsContentSource m_windows{*this};
  // The browser side, read in rather than dialled: a file under XDG_RUNTIME_DIR. No socket,
  // no thread, no JSON in this process (AGENTS.md 1). Which extension wrote it is unknown,
  // so the transport above it stays a separate deliverable.
  model::TabsContentSource m_tabs;
  // Rebuilt per run: it is made of titles, and a title is only true of the run it was read in.
  model::BrowserLinks m_links;
  // The window behind a tile, or null for a tab and for a window that has gone. Here and not
  // on Tile: a tile is a value with no route to its owner, so resolution belongs to the source
  // that holds the ownership and only there.
  PHLWINDOW tileWindow(const model::Tile &t) const { return m_windows.windowOf(t.subj); }

  // What a card IS, resolved in one place, with named answers. Thirty-three call sites used
  // to branch on `tileWindow(t)` for themselves and disagreed: drawTile skipped a null
  // window, pushOut yielded to it, the caption was gated on it, the border was not. A subject
  // kind with no window was then a bug hunt, not a one-line change. A fourth kind changes
  // this function and the sites keep reading.
  struct Card {
    PHLWINDOW window = {};        // empty for anything that is not a window
    bool      paintsOwnContent = false; // draws a picture of itself
    bool      coversChrome = false;     // occludes the strip and yields to it

    [[nodiscard]] bool isTab() const { return !window; }
    [[nodiscard]] bool hasWindow() const { return static_cast<bool>(window); }
  };
  [[nodiscard]] Card cardOf(const model::Tile &t) const {
    const auto w = m_windows.windowOf(t.subj);
    return Card{w, static_cast<bool>(w), static_cast<bool>(w)};
  }

  // The directory the browser side writes into. One place, because the feed, the screenshots
  // and the icons all live under it and a second spelling of the root is a second place for
  // them to disagree -- which is exactly the bug that left the feed at a doubled path for a
  // commit.
  static std::string browserSideFile(const std::string &leaf) {
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    return (rt && *rt ? std::string(rt) : std::string("/tmp")) + "/gloview" + leaf;
  }
  // The identity of a window the caller already holds. The inverse of tileWindow(), and on
  // the same side of the boundary: a caller that has a window asks here rather than casting
  // an address into a SubjectRef in three places.
  [[nodiscard]] static model::SubjectRef subjectOf(const PHLWINDOW &w) {
    return w ? model::SubjectRef{reinterpret_cast<std::uintptr_t>(w.get()), 0}
             : model::SubjectRef{};
  }
  model::Effects m_fx;
  view::State m_view;
  mutable std::chrono::steady_clock::time_point m_lastAnimTick{};
  void startTileGlide(
      const std::vector<std::pair<model::SubjectRef, LRect>> &oldBoxes);
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
  // Offset, endpoints and range: four doubles that had to agree on [0, max] with nothing
  // enforcing it.
  model::ScrollState m_scroll;
  void animateStripTo(double from, double to);
  bool animBusy() const;
  // Clocks outliving the master: the pump must name every clock, or the next one added
  // never arms.
  bool secondaryAnimsActive() const {
    return m_place.clocks.anyRunning(anim::cfg(entryLeaf()).ms, anim::cfg(ghostLeaf()).ms,
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
  // Only then is the natural box a destination rather than a rectangle on a workspace that
  // is not coming. See anim::tileExit.
  // The one place a Placement::Frame is built: three sites assembled the same six values and
  // one passed `true` for goesHome, a stub that reads as an answer.
  [[nodiscard]] clock::Placement::Frame frame() const;
  // Clear the three pieces of state that make the compositor skip a window that is not on
  // the active workspace: the workspace's alpha, the window's fullscreen alpha, and each
  // surface's visibleRegion. ScrollOverview does the same; without it a window from
  // another workspace previews as an empty frame. Restored on the way out.
  void forcePreviewVisibility();
  void restorePreviewVisibility();
  session::PreviewVisibility m_previewVis;
  void kickPulse(model::SubjectRef s);
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
  // Per surface: each stands down only for its own flight. A shared predicate made the
  // strip's thumbnail disappear while the card flew.
  [[nodiscard]] bool swapfxActive(const PHLWINDOW &w, bool strip) const;
  // Two predicates answering one question from two sides, so a tab can skip every effect.
  [[nodiscard]] bool swapfxActiveSubject(model::SubjectRef s, bool strip) const {
    return m_fx.swapActive(s, strip);
  }

  // A tab's art is its icon, and nothing else. The browser side cannot picture ONE tab:
  // captureVisibleTab photographs the visible window, so previews/<id>.png is the whole
  // browser -- chrome, sidebar, every other tab's thumbnail. Drawing it put a picture of the
  // browser on each of its own tab cards, thirteen times. No crop fixes that without
  // hardcoding one browser's sidebar geometry, so a tab card is its icon or its caption.
  SP<Render::ITexture> tabArt(const model::Tile &t) const {
    if (!cardOf(t).isTab())
      return nullptr;
    return m_pixel.putPicture(t.subj, "", browserSideFile("/icons/tab-" +
                                                               std::to_string(t.subj.id) + ".png"));
  }
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
  double glideDur() const { return m_place.clocks.tileMs > 0 ? m_place.clocks.tileMs : anim::cfg(glideLeaf()).ms; }
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
  // Keyed by SUBJECT, not by window. A browser tab has no window, and a vector keyed by one
  // drops every tab on the floor: capture came back empty, the glide was never armed, and the
  // card sat at the browser window's box instead of its own -- a full-screen card that did not
  // move. The subject is the identity both kinds have.
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
  // A card that joined or left the run has no slot to glide between. See keys.cpp.
  void pinRunEdges(const std::vector<char> &wasOnRun);
  int tileOf(const PHLWINDOW &w) const;
  // Defined here, next to m_pump, rather than in session/lifecycle.cpp. Twenty-two files
  // ask for a damage and only one of them is about the run's lifecycle -- an action, a
  // hit-test and a hover all need a repaint. Leaving the definition in lifecycle made
  // twenty-two files depend on the lifecycle, which is what kept the whole set in one
  // component.
  void damage() const { m_pump.damage(m_monitor.lock()); }
};

} // namespace gloview

inline gloview::Overview *g_overview = nullptr;

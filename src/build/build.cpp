#include "../config/config.hpp"
#include "../debug/log.hpp"
#include "../overview.hpp"
#include "../model/entries.hpp"
#include "../model/origin.hpp"
#include "../render/gl_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <cmath>
#include <numeric>
#include <set>
#include <utility>

#include <hyprland/src/desktop/history/WindowHistoryTracker.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

// Under the runtime dir, not home: live per-boot state must not survive a logout as current.
static std::string browserSideDir() {
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    return (rt && *rt ? std::string(rt) : std::string("/tmp")) + "/gloview";
}

// What the browser side left for one subject; exact paths built from ids already in hand, so
// "not there" is a defined answer rather than a guess.
static std::string tabFeedPath() { return browserSideDir() + "/tabs"; }

void Overview::loadTabs() {
    // Read once, whole. A snapshot replaces the previous one rather than merging: nothing to
    // reconcile, no second read.
    std::ifstream in(tabFeedPath(), std::ios::binary);
    std::string text;
    if (in)
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    const auto parsed = model::parseTabs(text);
    // Forget rather than serve an old generation: entriesFor's fallback then shows every
    // window as a window, which is the mode working.
    if (!parsed || !m_tabs.adopt(*parsed)) {
        // Not a failure: no extension running is ordinary. Recorded anyway -- "no tabs",
        // "not there" and "rejected" are identical outside.
        debug::trace::record("TABS", "load",
                             std::string("mode=") + model::domainName(m_view.domain()) +
                                 "|parsed=" + std::to_string(parsed.has_value() ? 1 : 0) +
                                 "|bytes=" + std::to_string(text.size()));
        m_tabs.clear();
        m_links.clear();
        return;
    }

    // The matching is NOT built here: buildTiles() derives it from the same enumeration that
    // builds the tiles, so the two cannot disagree.
}

void Overview::buildTiles() {
  // parked boxes survive the rebuild
  std::unordered_map<model::SubjectRef, std::pair<LRect, bool>> priorTargets;
  for (const auto &t : m_scene.tiles())
    if (t.parked)
      priorTargets.emplace(t.subj, std::pair{t.target, true});
  m_scene.clear();
  const auto m = m_monitor.lock();
  const auto ws = m_workspace.lock();
  if (!m || !ws)
    return;

  // Enumerate rather than walk the window list: enumerate() puts the windows on the source's
  // alive list, and an identity minted outside resolves to nobody -- a released-address crash.
  const auto mode = m_view.domain();
  const auto subjects = m_windows.enumerate();

  // From THIS enumeration, so the tile a tab is built for and the window it matches on cannot
  // be different windows.
  std::vector<model::HostWindow> hosts;
  std::string hostTitles;
  for (const auto subj : subjects) {
    const auto w = m_windows.windowOf(subj);
    if (w) {
      // The process, not the title: a browser appends its own name to a window's title, so the
      // two sides hold different strings by construction; a pid is the same number on both.
      hosts.push_back(model::HostWindow{reinterpret_cast<std::uintptr_t>(w.get()),
                                        static_cast<std::uint64_t>(w->getPID())});
      // The title itself, separators folded out of the way.
      hostTitles += "[" + w->m_title + "] ";
      for (auto &c : hostTitles)
        if (c == '|' || c == '\n' || c == '\r')
          c = '_';
    }
  }
  // Process per browser window, straight from the feed: two windows of one browser share it.
  const auto &pids = m_tabs.snapshot().pids;
  m_links.rebuild(hosts, pids);
  debug::trace::record("TABS", "match",
                       std::string("mode=") + model::domainName(m_view.domain()) + "|gen=" +
                           std::to_string(m_tabs.generation()) + "|hosts=" +
                           std::to_string(hosts.size()) + "|titles=" +
                           std::to_string(pids.size()) + "|links=" +
                           std::to_string(m_links.empty() ? 0 : 1) + "|cfg=" +
                           cfg::behavior.tile_mode.get() + "|hostLens=" + hostTitles);
  for (const auto subj : subjects) {
    const auto w = m_windows.windowOf(subj);
    if (!w)
      continue; // cannot happen straight after enumerating; not worth assuming

    // goal(), not value(): value() carries the slide offset. One box shared by everything
    // this window contributes -- a tab has no geometry of its own.
    const auto p = w->positionAnimation()->goal();
    const auto s = w->sizeAnimation()->goal();
    const LRect box{p.x - m->m_position.x, p.y - m->m_position.y,
                    std::max(1.0, s.x), std::max(1.0, s.y)};

    // Which browser window's tabs ride on this one. browserOf() answering 0 is not a browser
    // window: unmatched and not-yet-spoken look the same, and both show as a window.
    const auto bw = m_links.browserOf(reinterpret_cast<std::uintptr_t>(w.get()));
    const auto ids = m_tabs.tabIdsFor(bw);

    for (const auto &entry :
         model::entriesFor(w.get(), mode, m_tabs.knownFor(bw), ids, bw != 0)) {
      model::Tile t;
      // tabId == -1 is the window itself; anything else is a tab, identified under the
      // connection that issued the id. Decided once, in entriesFor.
      t.subj = entry.tabId < 0 ? subj
                               : model::SubjectRef{static_cast<std::uint64_t>(entry.tabId),
                                                   m_tabs.generation()};
      // A window's natural is where it sits on the desktop; a TAB has none, so it gets an
      // empty box that startTileGlide fills from the target on the way in.
      t.natural = entry.tabId < 0 ? box : LRect{};
      if (const auto it = priorTargets.find(t.subj); it != priorTargets.end()) {
        t.target = it->second.first;
        t.parked = true;
      }
      m_scene.add(std::move(t));
    }
  }

  // NO makeEGLCurrent() here: reached from key handlers and dispatch, where the EGL context
  // belongs to the compositor; ours raised a foreign one and killed it (hyprlandCrashReport41645).
  if (g_pHyprOpenGL && g_pHyprRenderer) {
    // A tile's title, in either layout -- the strip's own labels keep label_color.
    const auto lblCol = cfg::colors.title_label.get();
    for (size_t i = 0; i < m_scene.size(); ++i) {
      auto &t = m_scene.tileAt(i);
      // From whichever source owns this subject: asking the window source for a tab returns
      // empty, and an empty caption looks intentional.
      std::string text;
      SP<Render::ITexture> tex;
      if (cardOf(t).isTab()) {
        text = m_tabs.label(t.subj);
        tex  = m_pixel.subjectLabel(t.subj, text, lblCol, cfg::look.title_font_size.get());
      } else {
        const auto w = tileWindow(t);
        if (!w)
          continue;
        text = w->m_title;
        if (text.empty())
          text = w->m_class;
        if (text.size() > 80)
          text = text.substr(0, 79) + "…";
        tex = m_pixel.windowLabel(w, text, lblCol, cfg::look.title_font_size.get());
      }
      t.labelText = text;
      t.label     = tex;
    }
    // glyph pre-rendered: text rendering mid-pass is unsafe
    const std::string closeIcon =
        cfg::look.close_button_icon.get();
    m_pixel.glyph(closeIcon, cfg::colors.close_glyph.get());
  }
}

void Overview::buildStrip() {
  m_strip.clear();
  const auto m = m_monitor.lock();
  if (!m)
    return;
  const auto cur = m_workspace.lock();

  const std::string emptyMode =
      cfg::strip.empty_mode.get();
  const bool showSpecial = cfg::behavior.show_special != 0;
  // How many windows the predicate below accepts; split out so the log does not re-spell it.
  const auto countMappedOn = [](const PHLWORKSPACE &w) {
    int n = 0;
    for (const auto &win : Desktop::windowState()->windows())
      if (win && win->m_isMapped && !win->isHidden() && win->m_workspace == w)
        ++n;
    return n;
  };
  const auto wsHasWindows = [](const PHLWORKSPACE &w) {
    for (const auto &win : Desktop::windowState()->windows())
      if (win && win->m_isMapped && !win->isHidden() && win->m_workspace == w)
        return true;
    return false;
  };

  std::unordered_map<int, PHLWORKSPACE>
      real;
  const int curId = (cur && !cur->m_isSpecialWorkspace) ? cur->m_id : -1;
  int highestId = 0;
  for (const auto &wref : State::workspaceState()->workspaces()) {
    const auto ws = wref.lock();
    if (!ws || ws->m_monitor.lock() != m)
      continue;
    if (ws->m_isSpecialWorkspace) {
      if (showSpecial && wsHasWindows(ws)) {
        model::StripItem it;
        it.ws = ws;
        it.id = ws->m_id;
        it.active = (ws == cur);
        // Windows too: an empty card draws the wallpaper with no window. rel is the on-screen
        // position a stashed window keeps from the last time it was shown.
        for (const auto &w : Desktop::windowState()->windows()) {
          if (!w || !w->m_isMapped || w->isHidden() || w->m_workspace != ws)
            continue;
          const auto p = w->positionAnimation()->goal();
          const auto sz = w->sizeAnimation()->goal();
          model::StripWin sw;
          sw.subj = subjectOf(w);
          sw.rel = LRect{(p.x - m->m_position.x) / m->m_size.x,
                         (p.y - m->m_position.y) / m->m_size.y,
                         sz.x / m->m_size.x, sz.y / m->m_size.y};
          it.wins.push_back(sw);
        }
        // Every special card, every rebuild: it is the only way to tell "no window was
        // collected" from "the slot was clipped away" later.
        debug::dbg("strip special: ws=" + std::to_string(it.id) + " name=" + ws->m_name +
                   " wins=" + std::to_string(it.wins.size()) +
                   " mapped=" + std::to_string(countMappedOn(ws)));
        m_strip.add(std::move(it));
      }
      continue;
    }
    if (ws->m_id <= 0)
      continue;
    real[ws->m_id] = ws;
    highestId = std::max(highestId, static_cast<int>(ws->m_id));
  }
  // special cards bypass the numeric ordering
  std::vector<model::StripItem> specialCards = m_strip.takeAll();

  std::set<int> idsToShow;
  if (emptyMode == "hide") {
    for (const auto &[id, ws] : real)
      if (ws == cur || wsHasWindows(ws))
        idsToShow.insert(id);
  } else if (emptyMode == "neighbors") {
    for (const auto &[id, ws] : real)
      if (ws == cur || wsHasWindows(ws))
        idsToShow.insert(id);
    if (curId > 0) {
      if (curId - 1 > 0)
        idsToShow.insert(curId - 1);
      idsToShow.insert(curId + 1);
    }
  } else {
    const int upTo = std::max({10, highestId, curId});
    for (int id = 1; id <= upTo; ++id)
      idsToShow.insert(id);
    for (const auto &[id, ws] :
         real)
      idsToShow.insert(id);
  }
  if (curId > 0)
    idsToShow.insert(
        curId);

  // leading All-workspaces card; Kind::All guarded elsewhere
  if (cfg::strip.all_card != 0) {
    model::StripItem all;
    all.kind = model::StripItem::Kind::All;
    all.id = 0;
    m_strip.add(std::move(all));
  }

  for (const int id : idsToShow) {
    model::StripItem it;
    it.id = id;
    const auto found = real.find(id);
    if (found == real.end()) {
      it.virtualWs = true; // created on click/drop
      m_strip.add(std::move(it));
      continue;
    }
    const auto &ws = found->second;
    it.ws = ws;
    it.active = (ws == cur);
    for (const auto &w : Desktop::windowState()->windows()) {
      if (!w || !w->m_isMapped || w->isHidden() || w->m_workspace != ws)
        continue;
      const auto p = w->positionAnimation()->goal();
      const auto s = w->sizeAnimation()->goal();
      model::StripWin sw;
      sw.subj = subjectOf(w);
      sw.rel = LRect{(p.x - m->m_position.x) / m->m_size.x,
                     (p.y - m->m_position.y) / m->m_size.y, s.x / m->m_size.x,
                     s.y / m->m_size.y};
      it.wins.push_back(sw);
    }
    m_strip.add(std::move(it));
  }
  for (auto &sc : specialCards)
    m_strip.add(std::move(sc));

  model::StripItem plus;
  plus.kind = model::StripItem::Kind::Plus;
  plus.id = 0;
  m_strip.add(std::move(plus));

  if (g_pHyprOpenGL && g_pHyprRenderer) {
    const auto lblCol = cfg::colors.label.get();
    for (auto &it : m_strip) {
      if (it.kind == model::StripItem::Kind::Plus)
        continue;
      if (it.kind == model::StripItem::Kind::All) {
        it.label = m_pixel.fixedLabel(pixel::Slot::All, "All workspaces", lblCol, 13);
        continue;
      }
      const auto ws = it.ws.lock();
      std::string nm = ws ? ws->m_name : std::to_string(it.id);
      const bool numeric =
          !nm.empty() && std::all_of(nm.begin(), nm.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
          });
      const std::string text = numeric ? ("Workspace " + nm) : nm;
      it.label = ws ? m_pixel.workspaceLabel(ws, text, lblCol, 13)
                    : m_pixel.virtualLabel(it.id, text, lblCol, 13);
    }
  }

  const LRect band = stripBand();
  const bool horiz = stripHorizontal();
  const double margin = cfg::strip.margin;
  const double gap = cfg::strip.gap;
  const double labelH = cfg::strip.label_band;
  const double aspect = m->m_size.x / std::max(1.0, m->m_size.y);

  double cardW, cardH;
  if (horiz) {
    cardH = std::max(10.0, band.h - 2 * margin - labelH);
    cardW = cardH * aspect;
  } else {
    cardW = std::max(10.0, band.w - 2 * margin);
    cardH = cardW / aspect;
  }

  const double cellMain = horiz ? cardW : (cardH + labelH);
  const int n = static_cast<int>(m_strip.size());
  const double bandMain = horiz ? band.w : band.h;
  const double availMain = std::max(1.0, bandMain - 2 * margin);
  const double groupMain = n * cellMain + std::max(0, n - 1) * gap;
  const double mainOrigin = (horiz ? band.x : band.y) + margin;

  // Re-clamps endpoints and current into the new range.
  m_scroll.setMax(groupMain - availMain);
  debug::trace::scroll("setMax", m_scroll.describe());
  const double start = (!m_scroll.scrollable())
                           ? mainOrigin + (availMain - groupMain) / 2.0
                           : mainOrigin;

  const double cardX = band.x + margin;
  const double cardY = band.y + margin + labelH;

  double main = start;
  for (auto &it : m_strip) {
    if (horiz)
      it.card = LRect{main, cardY, cardW, cardH};
    else
      it.card = LRect{cardX, main + labelH, cardW, cardH};
    main += cellMain + gap;
  }

  const double prevScroll = m_scroll.target();
  double want = 0.0;
  if (m_scroll.scrollable()) {
    for (const auto &it : m_strip) {
      if (!it.active)
        continue;
      const double cardCenter =
          (horiz ? it.card.x + it.card.w / 2.0 : it.card.y + it.card.h / 2.0);
      const double viewCenter = mainOrigin + availMain / 2.0;
      want = std::clamp(cardCenter - viewCenter, 0.0, m_scroll.max());
      break;
    }
  }
  animateStripTo(prevScroll, want);
}

Vector2D Overview::stripScroll() const {
  return stripHorizontal() ? Vector2D{-m_scroll.current(), 0.0}
                           : Vector2D{0.0, -m_scroll.current()};
}

LRect Overview::stripCardAt(size_t i) const {
  if (i >= m_strip.size())
    return LRect{0, 0, 0, 0};
  const Vector2D scroll = stripScroll();
  const Vector2D slide = stripOffset(eased());
  return m_strip.cardAt(i, {slide.x, slide.y}, {scroll.x, scroll.y});
}

// `card` is already scroll/slide-adjusted.
LRect Overview::stripWinSlotRect(const model::StripItem &it, const LRect &card,
                                 size_t j) const {
  if (j >= it.wins.size())
    return LRect{0, 0, 0, 0};
  return render::winSlotRect(it.wins[j].rel, card);
}

void Overview::layoutTiles() {
  const auto m = m_monitor.lock();
  if (!m || m_scene.empty())
    return;

  // The renderer asks m_view.engine() / m_view.run(), never the config: free-arrange, the all/one
  // flip and the carousel switch each override what `layout` says.
  m_view.setEngine(m_view.desktop() ? Engine::Natural
                                    : parseEngine(cfg::grid.layout.get().c_str()));
  // A separate mechanism over any layout, only in the all-workspaces view: a
  // single workspace's handful of tiles has nothing to arrange against.
  m_view.setRun( m_view.carouselOn() && cfg::carousel.enabled != 0 && !m_view.desktop() &&
         showAllWorkspaces());

  LayoutCfg cfg;
  cfg.engine = m_view.engine();
  cfg.gap = cfg::grid.gap;
  const int padX = cfg::grid.padding;
  const int padT = cfg::grid.padding_top;
  const int padB = cfg::grid.padding_bottom;
  cfg.padLeft = padX;
  cfg.padRight = padX;
  cfg.padTop = padT;
  cfg.padBottom = padB;
  // reserve the band footprint on its anchored edge
  const double bandSpan = stripThickness() + stripOffset();
  switch (stripAnchor()) {
  case Anchor::Bottom:
    cfg.padBottom += bandSpan;
    break;
  case Anchor::Left:
    cfg.padLeft += bandSpan;
    break;
  case Anchor::Right:
    cfg.padRight += bandSpan;
    break;
  case Anchor::Top:
  default:
    cfg.padTop += bandSpan;
    break;
  }
  cfg.maxScale = cfg::grid.max_scale;

  // Room for the caption, per pill, on its configured edge: it is not clamped and would draw
  // over the band, which paints AFTER the tiles. Counter height must be known before this layout.
  double titleCapH = 0.0;
  for (const auto &t : m_scene.tiles()) {
    if (t.label && t.label->m_size.y > 0) {
      titleCapH = t.label->m_size.y + 2 * cfg::look.title_pad_y;
      break;
    }
  }
  double countCapH = 0.0;
  if (m_view.run() && m_scene.size() > 1) {
    const auto tex = m_pixel.fixedLabel(
        pixel::Slot::CarouselCount,
        std::to_string(m_scene.focused() + 1) + " / " + std::to_string(m_scene.size()),
        cfg::colors.counter_label.get(1.0), cfg::look.counter_font_size.get());
    if (tex && tex->m_size.y > 0)
      countCapH = tex->m_size.y + 2 * cfg::look.counter_pad_y;
  }
  {
    const std::string cpos = cfg::look.counter_position.get();
    const bool titleBelow = cfg::look.title_position.get() != "top";
    const bool counterBelow = cpos.empty() ? titleBelow : cpos != "top";
    const bool sameEdge = countCapH <= 0.0 || counterBelow == titleBelow;
    const double gap = cfg::look.title_gap;
    if (titleCapH > 0.0) {
      if (titleBelow) cfg.padBottom += gap + titleCapH;
      else cfg.padTop += gap + titleCapH;
    }
    if (countCapH > 0.0) {
      // On the title's edge the counter stacks behind it, so it adds its own gap on top of
      // the card gap; on the far edge it hangs off the card alone.
      const double room = gap + countCapH + (sameEdge ? cfg::look.counter_gap : 0.0);
      if (counterBelow) cfg.padBottom += room;
      else cfg.padTop += room;
    }
  }

  // whole monitor scaled into the usable box
  if (m_view.desktop()) {
    const double usableX = cfg.padLeft;
    const double usableY = cfg.padTop;
    const double usableW =
        std::max(1.0, m->m_size.x - cfg.padLeft - cfg.padRight);
    const double usableH =
        std::max(1.0, m->m_size.y - cfg.padTop - cfg.padBottom);
    const double s = std::min({usableW / m->m_size.x, usableH / m->m_size.y,
                               static_cast<double>(cfg.maxScale)});
    m_view.setTransform(s, usableX + (usableW - m->m_size.x * s) / 2.0,
                        usableY + (usableH - m->m_size.y * s) / 2.0);
    for (size_t i = 0; i < m_scene.size(); ++i) {
      auto &t = m_scene.tileAt(i);
      if (t.parked)
        continue;
      t.target = LRect{m_view.ox() + t.natural.x * s,
                       m_view.oy() + t.natural.y * s,
                       std::max(1.0, t.natural.w * s),
                       std::max(1.0, t.natural.h * s)};
    }
    return;
  }
  m_view.clearCards();

  // The run replaces the main area wholesale -- it lays one window against its neighbours,
  // which a grid of boxes cannot express.
  if (m_view.run()) {
    const int sel = m_scene.focused();
    const LRect usable{cfg.padLeft, cfg.padTop,
                       std::max(1.0, m->m_size.x - cfg.padLeft - cfg.padRight),
                       std::max(1.0, m->m_size.y - cfg.padTop - cfg.padBottom)};
    CarouselMetrics mt;
    mt.visibleCards = std::max(1, static_cast<int>(cfg::carousel.cards.get()));
    // The user's zoom on the run, on top of whatever the fit allows.
    mt.scale = std::max(0.1, static_cast<double>(cfg::carousel.scale.get()));
    // The standard card is a FULLSCREEN window's box -- the whole monitor, not the padded
    // usable rect, so containment is honest.
    mt.boxW = m->m_size.x;
    mt.boxH = m->m_size.y;
    if (const int w = static_cast<int>(cfg::carousel.card_width.get()); w > 0) {
      mt.boxW = std::min(static_cast<double>(w), m->m_size.x);
      mt.boxH = mt.boxW * m->m_size.y / m->m_size.x;
    }

    const auto n = static_cast<int>(m_scene.size());
    std::vector<std::size_t> slot;
    {
      std::vector<const void *> keys;
      keys.reserve(m_scene.size());
      for (const auto &t : m_scene.tiles())
        keys.push_back(tileWindow(t).get());
      m_recency.order(keys, slot);
    }
    auto lead = std::find(slot.begin(), slot.end(), static_cast<std::size_t>(sel));
    int selSlot = lead == slot.end() ? 0 : static_cast<int>(lead - slot.begin());


    if (m_pendingStep != 0 && selSlot >= 0) {
      const int nxt =
          ((selSlot + m_pendingStep) % n + n) % n;
      m_scene.select(static_cast<int>(slot[static_cast<std::size_t>(nxt)]));
      selSlot = nxt;
    }

    // Card k is slot k, so the centred card is at the selection's own position in the sequence.
    m_view.resolve(m_view.engine(), computeCarousel(n, selSlot, usable, mt),
                   sel, slot);
    // The centred card is what every caption measures itself against. Written where the run's
    // boxes exist and read at paint, so a caption shrinks with its card.
    m_view.setCardRefW(
        selSlot >= 0 && selSlot < n
            ? m_view.carousel().cards[static_cast<std::size_t>(selSlot)].w
            : 0.0);
    for (int k = 0; k < n; ++k) {
      const auto i = slot[static_cast<std::size_t>(k)];
      // cards[] is indexed by STRIP POSITION, which is what computeCarousel() writes;
      // indexing by the tile pairs card with tile by two numbering schemes.
      const auto &c = m_view.carousel().cards[static_cast<std::size_t>(k)];
      auto &t = m_scene.tileAt(static_cast<size_t>(i));
      // Zero target = off-run: nothing paints it, and a zeroed box keeps the glide
      // from resizing the card on its way out.
      t.target = c.visible ? LRect{usable.x + m_view.carousel().originX + c.x,
                                  usable.y + m_view.carousel().originY +
                                      m_view.carousel().stripY + c.y,
                                  c.w, c.h}
                           : LRect{};
      // Capture the caption HERE, where the card width is known, cut to it: paint only reads,
      // and asking at paint measured the pill off the uncut entry.
      if (c.visible && !t.labelText.empty()) {
        const auto maxW =
            static_cast<int>(std::max(1.0, tileBox(i, t.target).w - 28.0));
        if (const auto w = tileWindow(t))
          t.label = m_pixel.windowLabel(w, t.labelText,
                                        cfg::colors.title_label.get(1.0),
                                        cfg::look.title_font_size.get(), maxW);
        else
          t.label = m_pixel.subjectLabel(t.subj, t.labelText,
                                         cfg::colors.title_label.get(1.0),
                                         cfg::look.title_font_size.get(), maxW);
      }
      // Its own pill under the caption, its own font size so the two labels can be set apart.
      if (c.visible && c.depth == 0 && m_scene.size() > 1)
        t.countLabel = m_pixel.fixedLabel(
            pixel::Slot::CarouselCount,
            std::to_string(m_scene.focused() + 1) + " / " +
                std::to_string(m_scene.size()),
            cfg::colors.counter_label.get(1.0),
            cfg::look.counter_font_size.get());
      else
        t.countLabel = nullptr;
    }
    return;
  }
  m_view.clearCards();

  std::vector<LRect> nat;
  nat.reserve(m_scene.size());
  for (const auto &t : m_scene.tiles())
    nat.push_back(t.natural);

  const auto out =
      computeLayout(nat, LRect{0, 0, m->m_size.x, m->m_size.y}, cfg);
  m_scene.setTargets(out);
}

// Survivors glide from their captured boxes; no chrome re-reveal.
std::vector<std::pair<model::SubjectRef, LRect>>
Overview::captureCurrentBoxes(PHLWINDOW exclude) const {
  // Placement already filters by SUBJECT; resolving back to windows here would drop every
  // tab, and an empty capture is the same as no previous layout -- no glide, cards at natural.
  const auto keep = m_place.capture(
      frame(), exclude ? model::SubjectRef{reinterpret_cast<std::uintptr_t>(exclude.get()), 0}
                       : model::SubjectRef{});
  return keep;
}

// Freeze tiles at their shown boxes (`oldBoxes`, pre-rebuild); empty = fresh naturals, so a
// plain open starts from the real boxes.
void Overview::startTileGlide(
    const std::vector<std::pair<model::SubjectRef, LRect>> &oldBoxes) {
  bool newcomers = false;
  if (!oldBoxes.empty()) {
    for (size_t i = 0; i < m_scene.size(); ++i) {
      auto &t = m_scene.tileAt(i);
      // Where this card starts, from the one rule: model::outgoingBox() owns it. The capture
      // is authoritative for the slot -- the box each card was actually drawn at.
      const LRect *drawn = nullptr;
      for (const auto &[oldSubj, oldBox] : oldBoxes)
        if (oldSubj == t.subj) { drawn = &oldBox; break; }
      const bool onRun = m_view.run();
      const LRect ownPlace = t.natural;
      t.natural = model::outgoingBox(cardOf(t).hasWindow(), onRun,
                                     /*revealed=*/oldBoxes.empty(),
                                     drawn ? *drawn : model::noPlace(), ownPlace,
                                     t.target);
      t.landed = drawn ? 1.0 : 0.0;
      newcomers |= drawn == nullptr;
    }
    // Every removed tile fades out where it was -- never vanishes one frame.
    m_fx.clearGhosts();
    for (const auto &[oldSubj, oldBox] : oldBoxes) {
      bool kept = false;
      for (const auto &t : m_scene.tiles())
        if (t.subj == oldSubj) { kept = true; break; }
      if (!kept)
        m_fx.addGhost(model::Ghost{oldSubj, oldBox});
    }
  } else
    m_scene.settleEntry();
  m_place.clocks.tiles.begin();
  debug::trace::animStart("glide", "glide", "", m_place.recipes.glide.ms, m_place.recipes.glide.curve.c_str());
  m_place.clocks.tileMs    = m_place.recipes.glide.ms;
  m_place.clocks.tileCurve = m_place.recipes.glide.curve; // snapshot: currentBox() must use THIS curve
  // EVERY card is about to move, so the whole monitor must be repainted. damage() lives here,
  // not at the call sites: callers disagree (replayReflow damaged, relayoutCarousel did not).
  damage();
  if (newcomers || !m_fx.ghosts().empty()) {
    m_place.clocks.rebuild.begin();
    debug::trace::animStart("rebuild", "ghosts", "", m_place.recipes.ghost.ms, m_place.recipes.ghost.curve.c_str());
    ensureAnimPump(); // frames must keep coming while population runs
  }
}

void Overview::replayReflow(
    std::vector<std::pair<model::SubjectRef, LRect>> &oldBoxes) {
  m_scene.clearHover();
  buildTiles();
  buildStrip();
  layoutTiles();
  startTileGlide(oldBoxes);
  // Clamped, never past the end: an index off the cards indexed dead memory.
  if (m_scene.selected() >= static_cast<int>(m_scene.size()))
    m_scene.select(m_scene.empty() ? -1 : static_cast<int>(m_scene.size()) - 1);
  m_place.progress.settled();
  m_life.replayEntry();
  m_place.clocks.master.pinEnd(animDuration());
  damage();
}

void Overview::syncTiles() {
  if (!m_life.openingNow() || m_life.deactivating())
    return;
  const auto ws = m_workspace.lock();
  if (!ws)
    return;

  // Add/remove detection MUST reuse buildTiles()'s predicate (tileBelongs).
  const auto m = m_monitor.lock();
  const auto belongs = [&](const PHLWINDOW &w) {
    return tileBelongs(w, m, ws);
  };
  size_t expected = 0;
  for (const auto &w : Desktop::windowState()->windows())
    if (belongs(w))
      ++expected;

  bool diff = expected != m_scene.size();
  if (!diff)
    for (const auto &t : m_scene.tiles()) {
      const auto w = tileWindow(t);
      if (!belongs(w)) {
        diff = true;
        break;
      }
    }
  if (!diff)
    return;

  // The recency service caches the live set; a dead window's entry would hold a slot the next
  // window at the same address inherits.
  pruneRecency();

  auto oldBoxes = captureCurrentBoxes();

  // NOT parking here: `parked` means "the user dropped this window here", only the drop path
  // may set it; parking every survivor froze free-arrange.
  replayReflow(oldBoxes);
}

void Overview::restoreFill() {
  for (const auto &w : Desktop::windowState()->windows())
    if (w && w->wlSurface())
      w->wlSurface()->m_fillIgnoreSmall = false;
}

void Overview::hideLayers() {
  const auto m = m_monitor.lock();
  m_layers.hide(m, cfg::layer.hide_top != 0, cfg::layer.hide_overlay != 0,
                cfg::layer.above_namespaces.get());
  if (!m_layers.anyHidden())
    return;
  debug::dbg("hid " + std::to_string(m_layers.hiddenCount()) + " layer surface(s)");
  damage();
}

void Overview::restoreLayers() { m_layers.restore(); }

} // namespace gloview

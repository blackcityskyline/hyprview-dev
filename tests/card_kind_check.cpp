// A card's kind is decided in ONE function and the sites read named answers. It was thirty-three
// call sites each asking tileWindow() for itself, and they disagreed: drawTile skipped a null
// window, pushOut yielded to it, the caption was gated on it, the border was not,
// captureCurrentBoxes dropped it, startTileGlide matched nullptr against nullptr and called it
// found. Every one of those was found by a user report, not by a test, and the cost of a new
// subject kind was the whole file list.
//
// So this is not "the tab feature works" -- it is that the question has one home. If a site
// reaches past it, the site is guessing again, and the next kind pays the same price.
//
// Greps, on purpose: what is pinned is that one function exists, that it is the only thing
// classifying a card, and that the sites read it. None of that is reachable from a unit test,
// because the functions involved need a compositor.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string &p) {
  std::ifstream f(p);
  assert(f && ("cannot open " + p).c_str());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int failures = 0;

void want(bool ok, const char *what) {
  if (ok)
    return;
  ++failures;
  std::printf("FAIL %s\n", what);
}

// A file with its // and /* */ comments removed. A check that greps raw text finds its own
// documentation: this one already names every function it forbids, so leaving the comments in
// would make it pass on prose about the rule rather than on code obeying it.
std::string codeOnly(const std::string &src) {
  std::string out;
  enum { CODE, LINE, BLOCK } st = CODE;
  for (std::size_t i = 0; i < src.size(); ++i) {
    const char c = src[i];
    const char n = i + 1 < src.size() ? src[i + 1] : '\0';
    if (st == CODE && c == '/' && n == '/') {
      st = LINE;
      ++i;
      continue;
    }
    if (st == CODE && c == '/' && n == '*') {
      st = BLOCK;
      ++i;
      continue;
    }
    if (st == LINE && c == '\n')
      st = CODE;
    if (st == BLOCK && c == '*' && n == '/') {
      st = CODE;
      ++i;
      continue;
    }
    if (st == CODE)
      out += c;
  }
  return out;
}

std::size_t count(const std::string &hay, const std::string &needle) {
  std::size_t n = 0;
  for (std::size_t at = hay.find(needle); at != std::string::npos;
       at = hay.find(needle, at + needle.size()))
    ++n;
  return n;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const std::vector<std::string> files = {
      "src/overview.hpp",       "src/render/tile_view.cpp", "src/render/carousel_view.cpp",
      "src/render/fx.cpp",      "src/render/painter.cpp",   "src/session/geometry.cpp",
      "src/session/lifecycle.cpp", "src/build/build.cpp",   "src/actions/actions.cpp",
      "src/input/keys.cpp",     "src/input/mouse.cpp"};

  std::string all;
  for (const auto &f : files)
    all += codeOnly(slurp(root + "/" + f));

  // --- the one home ------------------------------------------------------------------------
  want(all.find("Card cardOf(") != std::string::npos,
       "cardOf() is gone: a card's kind must be decided in one function");
  want(all.find("paintsOwnContent") != std::string::npos &&
           all.find("coversChrome") != std::string::npos,
       "cardOf() must answer with named questions, not a bare pointer");

  // --- and the sites read it ---------------------------------------------------------------
  want(count(all, "cardOf(") >= 2,
       "cardOf() is declared and nothing calls it -- the sites are still guessing");

  // The failure mode that cost three days: a site that asks tileWindow() and branches on the
  // answer in its own way. tileWindow() stays -- it is the resolver cardOf() uses, and the
  // window-only paths (drag, swap, focus, close) legitimately want a window. What must not
  // survive is a site asking m_tabs.isTab() to classify a card, because that is the same
  // question spelled a fourth time, and the tab source is not the only thing without a window.
  want(all.find("m_tabs.isTab") == std::string::npos,
       "a site classifies a card with m_tabs.isTab() instead of cardOf()");

  // The close of a card: a tab has no window, so a close that asks the window first closes
  // nothing -- the tab card's x was a button that did nothing, and the drag into the closing
  // zone ended in the same silence. The kind comes first, and a tab leaves as a request to
  // the browser that owns it.
  const auto actions = codeOnly(slurp(root + "/src/actions/actions.cpp"));
  want(actions.find("writeTabClose") != std::string::npos,
       "a close never asks the browser to close a tab -- the tab card's x does nothing");
  const auto mouse = codeOnly(slurp(root + "/src/input/mouse.cpp"));
  const auto zone = mouse.find("killZoneRect().contains");
  const auto kind = zone == std::string::npos ? std::string::npos
                                              : mouse.rfind("isTab()", zone);
  want(zone != std::string::npos && kind != std::string::npos &&
           zone - kind < 200,
       "the closing zone skips a tab: a drag into it does nothing");
  want(mouse.find("cardIdx >= 0 && w") != std::string::npos,
       "a tab card can reach the window drop path, where the window is null");
  const auto buildSrc = codeOnly(slurp(root + "/src/build/build.cpp"));
  want(buildSrc.find("/close") != std::string::npos,
       "the close request has no file of its own: an old mediator drops it as malformed");
  // A card that leaves must leave the way every other card leaves: the ghost, the fading
  // copy the rebuild leaves behind. A tab has no window to draw, so the ghost must be able
  // to draw the card's picture -- otherwise the card blinks out of the layout.
  want(buildSrc.find("syncTabs()") != std::string::npos &&
           buildSrc.find("m_tabs.snapshot().stamp") != std::string::npos,
       "the tabs deck never reads the feed again: a closed tab's card stays for ever");
  const auto tiles = codeOnly(slurp(root + "/src/render/tile_view.cpp"));
  want(tiles.find("drawTabContent(g.subj") != std::string::npos,
       "the ghost of a tab draws nothing: the card blinks out instead of leaving");
  want(tiles.find("drawTabContent(t.subj") != std::string::npos,
       "the card and its ghost draw different things: a card leaves unlike it lived");

  // captureCurrentBoxes once converted subjects to windows and dropped every tab, so the
  // glide never armed and a card drew at the browser window's full-screen box. Keyed by
  // subject, never by window: that is the regression.
  const auto build = codeOnly(slurp(root + "/src/build/build.cpp"));
  want(build.find("SubjectRef, LRect>> captureCurrentBoxes") != std::string::npos ||
           build.find("SubjectRef, LRect>") != std::string::npos,
       "captureCurrentBoxes() is keyed by window again -- every tab is dropped on the floor");
  const auto hdr = codeOnly(slurp(root + "/src/overview.hpp"));
  want(hdr.find("std::pair<model::SubjectRef, LRect>") != std::string::npos,
       "the old box vector is declared with a window key");
  want(hdr.find("std::pair<PHLWINDOW, LRect>") == std::string::npos,
       "a box vector keyed by PHLWINDOW is still declared");

  if (failures) {
    std::printf("card_kind: %d violation(s)\n", failures);
    return 1;
  }
  std::puts("card_kind: one function decides what a card is, and the sites read it");
  return 0;
}

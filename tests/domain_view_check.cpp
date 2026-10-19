// key_browser's live view switch. Three things about it that a unit test cannot reach, because
// the function it drives needs a compositor, and one of them is a product decision rather than
// an implementation detail.
//
// 1. It is a REBUILD, not a relayout. The two views hold different subjects -- a window is not a
//    tab, and in the tab view a browser window has no card of its own at all -- so layoutTiles()
//    alone leaves windows sitting in a grid sized for tabs. replayReflow() is what rebuilds.
//
// 2. It re-reads the tab feed. The feed is a file a separate process rewrites whenever the user
//    switches tabs, and open() read it once when the run opened. Asking for the tab view is
//    exactly the moment its contents matter, and going without it means "b" shows a snapshot
//    from whenever the overlay was last opened.
//
// 3. The toggle is Windows <-> Tabs and Both is NOT in the cycle. Both is the plugin's own third
//    view -- a window's card plus one per tab -- and cycling through it would put both kinds of
//    card in one grid, which is neither view and is nobody's idea of a toggle target. It stays
//    reachable through tile_mode, where a user has to type it on purpose.
//
// Greps, on purpose: what is being pinned is which functions get called and which enum values
// appear, and the only honest way to ask that of a method on Overview is to read it.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

// The body of one function definition, braces balanced. Reading to the next "void Overview::
// would swallow whatever follows, and the neighbour of switchDomain() is toggleAllWorkspaces()
// -- which calls open(), which re-reads tile_mode and would undo the switch. A test that passes
// because it matched the wrong function is worse than no test.
//
// Comments come out first. Every function here explains in prose the very calls the test is
// looking for -- "open() is not the way in", "Both is not in the cycle" -- so a check that
// greps raw text passes on its own documentation and proves nothing at all. That is the third
// time in this repo a guard was satisfied by the shape of the check rather than by the code.
std::string body(const std::string &src, const std::string &signature) {
  const auto at = src.find(signature);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  size_t stop = std::string::npos;
  for (size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{')
      ++depth;
    else if (src[i] == '}' && --depth == 0) {
      stop = i + 1;
      break;
    }
  }
  if (stop == std::string::npos)
    return {};
  std::string out;
  for (size_t i = at; i < stop;) {
    if (src[i] == '/' && i + 1 < stop && src[i + 1] == '/') {
      while (i < stop && src[i] != '\n')
        ++i;
      continue;
    }
    out += src[i++];
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto lifecycle = slurp(root + "/src/session/lifecycle.cpp");
  const auto keys = slurp(root + "/src/input/keys.cpp");
  const auto cfg = slurp(root + "/src/config/config.cpp");
  const auto readme = slurp(root + "/README.md");

  const auto switcher = body(lifecycle, "void Overview::switchDomain");
  want(!switcher.empty(), "switchDomain() is not in lifecycle.cpp");
  const auto toggle = body(lifecycle, "void Overview::toggleBrowserView");
  want(!toggle.empty(), "toggleBrowserView() is not in lifecycle.cpp");

  // --- 1: a rebuild, and the feed read again ---------------------------------------------------
  {
    want(switcher.find("replayReflow") != std::string::npos,
         "switchDomain() must rebuild through replayReflow(), not just lay out");
    want(switcher.find("layoutTiles") == std::string::npos,
         "switchDomain() must not relayout on its own: the cards are different subjects");
    want(switcher.find("loadTabs()") != std::string::npos,
         "switchDomain() must re-read the tab feed: a separate process rewrites it");
    want(switcher.find("open()") == std::string::npos,
         "switchDomain() must not go through open(), which re-reads tile_mode and undoes it");
  }

  // --- 2: the toggle is two views, not three ---------------------------------------------------
  {
    want(toggle.find("Domain::Windows") != std::string::npos,
         "toggleBrowserView() must be able to go back to the window view");
    want(toggle.find("Domain::Tabs") != std::string::npos,
         "toggleBrowserView() must be able to go to the tab view");
    want(toggle.find("Both") == std::string::npos,
         "Both must stay out of the toggle: it puts a window and its tabs in one grid");
    want(toggle.find("switchDomain") != std::string::npos,
         "the key and the harness command must drive the same method");
  }

  // --- 3: registered, bound, documented ---------------------------------------------------------
  {
    want(cfg.find("plugin:hyprview:key_browser") != std::string::npos,
         "key_browser is not registered in config.cpp");
    want(keys.find("cfg::keys.browser.get()") != std::string::npos,
         "key_browser is registered but nothing reads it");
    want(keys.find("toggleBrowserView()") != std::string::npos,
         "the key does not reach the toggle");
    want(readme.find("key_browser") != std::string::npos,
         "key_browser is undocumented");
  }

  if (failures) {
    std::printf("domain_view: %d violation(s)\n", failures);
    return 1;
  }
  std::puts("domain_view: b rebuilds into the other view, and Both stays a tile_mode value");
  return 0;
}

// The box guard on immediate-mode draws.
//
// Every immediate-mode entry point in Hyprland opens with the same check:
//   OpenGL.cpp:1071  renderRect            RASSERT(box.width > 0 && box.height > 0)
//   OpenGL.cpp:2163  renderBorder          "
//   OpenGL.cpp:2341  renderRoundedShadow   "
// That assert is a crash INSIDE the compositor: not a dropped frame, the whole
// session, which is why the 2026-10-10 19:16 report reads as a freeze on the last
// frame rather than an exit. pxb() rounds, so a box under half a pixel becomes zero
// even while non-empty, and a cross-surface flight lerps a grid card down to a strip
// thumbnail -- one order of magnitude -- passing through boxes that are legitimately
// tiny.
//
// This test does not check the arithmetic of the guard. It checks that the guard is
// still the ONLY way a box reaches the compositor, which is the property that rots:
// adding a draw is the normal way to write new chrome, and nothing else in the build
// objects when the new one is unchecked.
//
// Two failures it has to catch, both shipped:
//   - renderSwapFX drew a flight's shadow with no guard at all (crash 45823).
//   - strip_view's safeRenderRect tested `width < 0`, so a ZERO box passed through
//     the helper that exists to stop exactly that. Correct shape, wrong sign, and it
//     read as covered -- worse than no guard, because it silenced the question.
//
// A grep over the sources rather than a link: the guarded wrappers call
// g_pHyprOpenGL, which lives in the Hyprland binary, so testing them for real would
// mean stubbing CHyprColor and NColorManagement into fakes whose members cannot even
// be written from outside. A stub that silently produces garbage is a worse trap than
// the one this test prevents.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void fail(const std::string &what) {
  std::printf("  FAIL  %s\n", what.c_str());
  ++failures;
}

std::string slurp(const std::string &p) {
  std::ifstream f(p);
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", p.c_str());
    std::exit(2);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::vector<std::string> sources(const std::string &root) {
  std::vector<std::string> out;
  for (const auto &e : std::filesystem::recursive_directory_iterator(root)) {
    const std::string ext = e.path().extension().string();
    if (e.is_regular_file() && (ext == ".cpp" || ext == ".hpp"))
      out.push_back(e.path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string lineAt(const std::string &text, size_t pos) {
  size_t start = text.rfind('\n', pos);
  start = start == std::string::npos ? 0 : start + 1;
  size_t end = text.find('\n', pos);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: draw_sites_check <src dir>\n");
    return 2;
  }
  const std::string root = argv[1];
  const std::string util = root + "/render/gl_util.hpp";
  const std::string utilText = slurp(util);

  // 1. Every wrapper the callers are told to use gates on drawable(). If one stops,
  //    it stops silently -- its signature is unchanged and every call site still
  //    compiles.
  struct {
    const char *fn;
    const char *gate;
  } wrappers[] = {
      {"safeRenderRect", "drawable("},
      {"dropShadow", "drawable("},
      {"strokeRing", "drawable("},
      {"strokeRingPx", "drawable("},
  };
  for (const auto &w : wrappers) {
    const size_t at = utilText.find(std::string("void ") + w.fn + "(");
    if (at == std::string::npos) {
      fail(std::string("wrapper gone: ") + w.fn +
           " -- callers compile against a missing declaration instead of failing");
      continue;
    }
    // The guard has to be inside the body, so look at the next few lines only: a
    // mention further down the file belongs to somebody else's function.
    const size_t body = utilText.find('{', at);
    const size_t stop = utilText.find("\ninline", body);
    const std::string chunk =
        utilText.substr(body, stop == std::string::npos ? 400 : stop - body);
    if (chunk.find(w.gate) == std::string::npos)
      fail(std::string(w.fn) + " draws without checking the box");
  }

  // 2. Nothing outside gl_util.hpp reaches the compositor directly. These three are
  //    the size-asserting entry points; renderTexture is absent on purpose (its
  //    asserts are pMonitor and a nullptr texture, neither reachable from a box).
  const char *raw[] = {"g_pHyprOpenGL->renderRect(",
                       "g_pHyprOpenGL->renderBorder(",
                       "g_pHyprOpenGL->renderRoundedShadow("};
  size_t sites = 0;
  for (const auto &f : sources(root)) {
    if (f == util)
      continue;
    const std::string text = slurp(f);
    for (const char *needle : raw) {
      size_t at = text.find(needle);
      while (at != std::string::npos) {
        ++sites;
        fail(std::string("unguarded draw in ") + f.substr(root.size() + 1) + ": " +
             lineAt(text, at));
        at = text.find(needle, at + 1);
      }
    }
  }
  std::printf("  %zu raw draw site(s) outside the guarded header\n", sites);

  // 3. The wrappers are reachable as wrappers. A caller that hand-rolls the guard
  //    around a raw call looks exactly like the thing this test forbids, so the
  //    header has to keep offering the guarded form for each entry point.
  for (const char *needle : raw) {
    if (utilText.find(needle) == std::string::npos)
      fail(std::string("gl_util.hpp offers no guarded path for ") + needle);
  }

  if (failures) {
    std::printf("draw_sites: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("draw_sites: ok\n");
  return 0;
}

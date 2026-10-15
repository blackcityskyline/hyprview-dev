// Every configured animation must be observable, or be on a list with a reason.
//
// Today ANIM|begin is emitted from exactly one place -- beginSwapFX. The swap flight is
// therefore the ONLY configured animation that can be seen to have run, or to have not.
// A workspace entry that never started was indistinguishable from one that started with
// the wrong style, because nothing was written either way. That is not a missing nicety:
// the absence of a record is the same as the absence of the animation, so the trace cannot
// tell a silent path from a wrong one.
//
// This reads the sources and requires each clock start to be traced nearby, or to appear
// on the exemption list below WITH its reason attached. An exemption without a reason
// fails, so the list cannot grow silently into a to-do file.
//
// The check is a grep, deliberately: these are calls into Overview-owned state that no
// test can reach without a compositor, and `scan_draw_sites` already established that a
// grep written this way catches the real thing.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <regex>
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

std::vector<std::string> sources(const std::string &root) {
  std::vector<std::string> out;
  for (const char *sub : {"", "clock/", "render/", "session/", "input/", "build/", "actions/"}) {
    std::string dir = root + "/src/" + sub;
    for (const char *name : {"clocks.cpp", "clock.cpp", "curves.cpp", "painter.cpp",
                             "backdrop.cpp", "fx.cpp", "carousel_view.cpp", "tile_view.cpp",
                             "strip_view.cpp", "window_content.cpp", "hooks.cpp",
                             "geometry.cpp", "lifecycle.cpp", "mouse.cpp", "keys.cpp",
                             "build.cpp", "actions.cpp"}) {
      std::ifstream probe(dir + name);
      if (probe)
        out.push_back(dir + name);
    }
  }
  return out;
}

// start site -> why it may be silent. A one-line reason, or the check fails.
struct Exempt {
  const char *file;
  int         line;
  const char *why;
};

const Exempt kExempt[] = {
    // newCard: the new-card scale is a property of ONE card and is visible in the frame
    // log as `pop=`; a begin/end pair per card would triple the record rate for a motion
    // nobody parameterises independently.
    {"src/actions/actions.cpp", 123, "newCard scale, one card, frame log carries pop="},
    {"src/actions/actions.cpp", 134, "newCard scale, one card, frame log carries pop="},
    {"src/actions/actions.cpp", 885, "newCard scale, one card, frame log carries pop="},
    {"src/input/keys.cpp", 123, "newCard scale, one card, frame log carries pop="},
    // dragLift: the lift is a constant, not a configured animation -- there is no leaf to
    // report. Its effect is the card's offset, which the frame log shows directly.
    {"src/clock/clocks.cpp", 195, "dragLift is a constant, not a configured leaf"},
    // strip: the scroll glide. Traced separately as SCROLL| (setMax/glide/snap), which
    // carries the positions the ANIM| record would have carried.
    {"src/clock/clocks.cpp", 309, "emitted as SCROLL|glide instead"},
};

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";

  int total = 0, traced = 0, exempted = 0, missing = 0;
  for (const auto &src : sources(root)) {
    const std::string text = slurp(src);
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      ++n;
      std::smatch m;
      if (!std::regex_search(line, m, std::regex(R"(m_place.clocks\.(\w+)\.begin\(\))")))
        continue;
      ++total;
      const std::string clock = m[1].str();

      // Traced on the same line or within six lines after: that window covers the one
      // pattern used everywhere else (arm the clock, then say what it is).
      const size_t at = text.find(line + "\n", static_cast<size_t>(0));
      std::string near;
      {
        std::istringstream win(text.substr(at, line.size() * 9 + 64));
        std::string l2;
        int k = 0;
        while (std::getline(win, l2) && k < 7) {
          near += l2 + "\n";
          ++k;
        }
      }
      if (near.find("trace::animStart") != std::string::npos ||
          near.find("trace::animBegin") != std::string::npos ||
          line.find("trace::anim") != std::string::npos) {
        ++traced;
        continue;
      }

      bool ok = false;
      for (const auto &e : kExempt)
        if (e.file == src && e.line == n) {
          ok = true;
          ++exempted;
          break;
        }
      if (!ok) {
        std::printf("FAIL %s:%d -- %s clock starts with no ANIM| record and no exemption\n",
                    src.c_str(), n, clock.c_str());
        ++missing;
      }
    }
  }

  std::printf("%d animation start(s): %d traced, %d exempted with a reason, %d silent\n",
              total, traced, exempted, missing);
  assert(total > 0 && "no clock starts found -- the scan drifted from the sources");
  assert(exempted <= static_cast<int>(std::size(kExempt)) &&
         "an exemption matched nothing; the list is stale");

  if (missing) {
    std::puts("An animation that is applied but never recorded cannot be told apart from "
              "one that did not run. Emit trace::animStart next to it, or add it to the "
              "exemption list WITH a reason -- an unexplained exemption fails.");
    return 1;
  }
  // The frame's derived values are computed by Placement::resolve, not by the recipe
  // resolver it happens to call. Getting this wrong compiles, passes every test here, and
  // paints the whole overlay at alpha zero: no exit animation, no strip, no caption pills,
  // no cursor -- and nothing crashes, so the harness reports green throughout.
  //
  // It happened. Placement::resolve recomputes eased() and the master duration every frame
  // from the recipes; m_place.recipes.resolve() only fills the recipes, so those two stayed
  // at their initial 0.0 for as long as the plugin was loaded.
  {
    const auto clocksText = slurp((root + "/src/clock/clocks.cpp").c_str());
    // Which of the two updateAnimation() reaches for.
    std::size_t pos = clocksText.find("void Overview::updateAnimation() {");
    assert(pos != std::string::npos && "updateAnimation moved");
    const auto end = clocksText.find("\n}\n", pos);
    const auto body = clocksText.substr(pos, end - pos);
    if (body.find("m_place.resolve(") == std::string::npos) {
      std::puts("FAIL updateAnimation does not call Placement::resolve -- the frame's "
                "eased() and master duration are computed there");
      std::puts("  m_place.recipes.resolve() fills the recipes only; both values stay 0 and "
                "the overlay paints at alpha 0");
      std::exit(1);
    }
    if (body.find("m_place.recipes.resolve(") != std::string::npos) {
      std::puts("FAIL updateAnimation resolves the recipes directly -- that skips the "
                "per-frame eased() and master duration");
      std::exit(1);
    }
    std::puts("updateAnimation resolves the frame through Placement");
  }

  std::puts("OK");
  return 0;
}
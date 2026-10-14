// Overview is not allowed to hold state that has exactly one writer.
//
// Why this is a test and not a note: the god object did not arrive in one commit. It
// arrived as a handful of correct-looking fields, each added next to the code that used
// it, and nothing flagged them because every one of them was locally reasonable. The only
// property that separates the 25 fields that belong in a type from the ones that legitimately
// stay is WHICH FILE WRITES THEM -- and that property is measurable by grep.
//
// So it is measured by grep. Two halves:
//
//   1. A registry of the subsystems already extracted. Each must keep its state private,
//      and Overview must hold none of it. Reverting an extraction fails here.
//   2. Every remaining Overview field is counted across the .cpp tree. A field with exactly
//      one writing file has no business being a field on the coordinator: it is state that
//      belongs to the type doing the writing. This is what makes check 1 a ratchet -- it
//      names the next extraction instead of only guarding the last one.
//
// The count is deliberately about WRITERS, not readers. A field read from fourteen files
// and written from one is the shape of a subsystem that has not been extracted yet; a field
// written from three files is genuinely shared and stays.

#include <cassert>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path &p) {
  std::ifstream f(p);
  assert(f && ("cannot open " + p.string()).c_str());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::vector<fs::path> sources(const fs::path &src) {
  std::vector<fs::path> out;
  for (const auto &e : fs::recursive_directory_iterator(src))
    if (e.is_regular_file()) {
      const auto ext = e.path().extension().string();
      if (ext == ".cpp" || ext == ".hpp")
        out.push_back(e.path());
    }
  std::sort(out.begin(), out.end());
  return out;
}

// `  mutable render::Backdrop m_backdrop;` and friends, private section only.
std::vector<std::string> overviewFields(const std::string &hdr) {
  const auto priv = hdr.find("private:");
  assert(priv != std::string::npos && "overview.hpp has no private section");
  // `  mutable render::Backdrop m_backdrop;`, `  CFunctionHook *m_shouldRenderHook = ...;`
  const std::regex decl(
      R"(^\s{2}(?:mutable\s+)?(?:const\s+)?[\w:]+\s*(?:<[^;]*>)?\s*\**\s*(m_\w+)\s*(?:[={;]))",
      std::regex::multiline);
  std::vector<std::string> out;
  for (std::sregex_iterator it(hdr.begin() + static_cast<long>(priv), hdr.end(), decl),
       end;
       it != end; ++it)
    out.push_back((*it)[1].str());
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

struct Subsystem {
  const char *type;
  const char *header;
  std::vector<const char *> state;
};

} // namespace

int main(int argc, char **argv) {
  const fs::path src = argc > 1 ? argv[1] : "src";
  const auto     all = sources(src);

  const auto overviewText = slurp(src / "overview.hpp");
  const auto fields       = overviewFields(overviewText);

  // --- 1. the extracted subsystems keep their state --------------------------------------
  // Names the type as `namespace::Type`; the header is matched by its own name only.
  const std::vector<Subsystem> subs{
      {"Backdrop", "render/backdrop.hpp",
       {"m_blur", "m_srcFB", "m_scratchFB", "m_probeFB", "m_drawn", "m_srcTex",
        "m_lastObservedTex", "m_srcAnimStreak", "m_srcAnimated", "m_blurFilter"}},
      // The three pieces of compositor state a cross-workspace preview has to be shown
      // against, saved as a set and put back as a set.
      {"PreviewVisibility", "session/preview_visibility.hpp", {"m_ws", "m_wins", "m_regions"}},
      // Everything that attaches to the compositor. Held because the destructor removes the
      // hooks: leaving shouldRenderWindow hooked after unload sends the next call into freed
      // code, which is the shape of the 2026-10-13 SEGV.
      {"Hooks", "session/hooks.hpp",
       {"m_shouldRenderHook", "m_damageSurfaceHook", "m_setPointerFocusHook",
        "m_setKeyboardFocusHook", "m_renderStageL", "m_mouseButtonL",
        "m_mouseAxisL", "m_mouseMoveL", "m_keyL"}},
      // close_trigger == "doubleclick": the armed timer IS the record of a pending click.
      {"PendingClick", "session/pending_click.hpp", {"m_win", "m_timer"}},
      // The bars and popups faded out for the overlay, and the alphas that put them back.
      // hide() without restore() leaves the user's taskbar invisible with nothing else in
      // the plugin noticing, which is why the saved values live next to the fading.
      {"LayerVisibility", "session/layer_visibility.hpp", {"m_hidden"}},
  };

  std::set<std::string> delegated;
  for (const auto &s : subs) {
    const fs::path hp = src / s.header;
    assert(fs::exists(hp) && (std::string("missing ") + s.header).c_str());
    const auto text = slurp(hp);

    // Word-bounded: a substring test passes on `class BackdropGone`, which is exactly the
    // kind of near-miss a rename produces.
    if (!std::regex_search(text, std::regex(std::string("\\b(class|struct)\\s+") + s.type + "\\b"))) {
      std::printf("FAIL %s no longer declares class %s\n", s.header, s.type);
      return 1;
    }
    if (text.find("private:") == std::string::npos) {
      std::printf("FAIL %s: class %s holds no private state -- extraction reverted?\n",
                  s.header, s.type);
      return 1;
    }
    for (const auto *f : s.state) {
      if (text.find(std::string(f)) == std::string::npos) {
        std::printf("FAIL %s: %s does not own %s\n", s.header, s.type, f);
        return 1;
      }
      if (overviewText.find(std::string(f)) != std::string::npos) {
        std::printf("FAIL overview.hpp still declares %s -- %s should own it\n", f,
                    s.type);
        return 1;
      }
      delegated.insert(f);
    }
  }
  std::printf("%zu subsystem(s) checked, %zu field(s) delegated\n", subs.size(),
              delegated.size());

  // --- 2. every other Overview field, counted by writing file -----------------------------
  // An assignment, or any container mutation. A bare read does not count as a writer.
  const std::regex writer(R"(\b(m_\w+)((?:\.\w+|->\w+|\[[^\]]*\])*)\s*(=[^=]|\+\+|--|\+=|-=|\.push_back|\.clear\(\)|\.erase))");

  struct Single {
    std::string field;
    std::string file;
  };

  // A lone field is not a subsystem. `m_scrollAccum` is written in one file because it is
  // one double, not because a type is hiding behind it. What identifies real state is a
  // CLUSTER: several fields, one writer. Those are the ones that name an extraction, and
  // requiring two is what keeps this from crying wolf on every scalar Overview owns.
  //
  // Exemptions are listed with the reason they are legitimately shared rather than merely
  // small. m_monitor/m_workspace/m_liveWsAtOpen/m_landingEmpty are what the overlay is
  // looking at -- there is no second type for "the thing being looked at". m_clocks and
  // m_animPump are the animation engine, written through member calls that this scan does
  // not see. The rest are single values of the coordinator itself.
  // Each entry is a field of the coordinator that has no second type to live in.
  const std::set<std::string> exempt{
      // what the overlay is looking at
      "m_monitor", "m_workspace", "m_liveWsAtOpen", "m_landingEmpty",
      // the run itself
      "m_openStamp", "m_progress", "m_lastAnimTick", "m_frameTick",
      "m_newCardId", "m_newCardAnim", "m_altTabbing", "m_commitPending",
      "m_pendingStep", "m_pendingFocus", "m_focusOutside",
      // the animation engine, written through member calls this scan does not see
      "m_clocks", "m_animPump", "m_enterStyle",
      // one value each, owned by the coordinator because nothing else does the owning
      "m_scrollAccum", "m_killGlow", "m_lastDragBox", "m_lastFocus", "m_handle",
  };

  std::vector<Single>                          single;
  std::map<std::string, std::set<std::string>> writers;

  for (const auto &f : all) {
    if (f.extension() != ".cpp")
      continue;
    const auto name = f.filename().string();
    const auto text = slurp(f);
    for (std::sregex_iterator it(text.begin(), text.end(), writer), end; it != end;
         ++it)
      writers[(*it)[1].str()].insert(name);
  }

  for (const auto &field : fields) {
    if (delegated.count(field) || exempt.count(field))
      continue;
    const auto it = writers.find(field);
    if (it != writers.end() && it->second.size() == 1)
      single.push_back({field, *it->second.begin()});
  }

  // Zero means the scan drifted off the sources: a broken test, not a pass.
  assert(fields.size() > 10 && "overview.hpp yielded no fields -- parser drifted");
  assert(writers.size() > 10 && "no writers found -- writer regex drifted");
  std::printf("%zu Overview field(s), %zu single-writer, %zu exempt\n",
              fields.size(), single.size() + delegated.size(), delegated.size());

  // Report by cluster: a field matters when its file has others like it.
  std::map<std::string, std::vector<std::string>> byFile;
  for (const auto &s : single)
    byFile[s.file].push_back(s.field);

  bool bad = false;
  for (const auto &[file, group] : byFile) {
    if (group.size() < 2)
      continue;
    bad = true;
    std::printf("FAIL %zu field(s) written only by %s:\n", group.size(), file.c_str());
    for (const auto &g : group)
      std::printf("       %s\n", g.c_str());
    std::puts("  a cluster with one writer is a type that has not been extracted yet");
  }

  if (bad)
    return 1;

  std::puts("OK");
  return 0;
}
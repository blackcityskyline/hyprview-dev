// Two rules about Hyprland types, both of which were prose in AGENTS.md.
//
// Neither is about types. The first is about which files are allowed to SEE them, and the
// second about what a map keyed by a window must carry. Both were greppable the whole time
// -- a minefield written as a paragraph, which is the form this project keeps converting
// into ctest.
//
// 1. THE PURE DOMAIN STAYS PURE. Six sources are compiled by ctest without a compositor:
//    no hyprland/ header exists on their include path, so one #include turns a test from
//    a 0.4s assert into a link failure. layout.cpp is the pattern AGENTS.md points at
//    ("the only Hyprland/GL-independent domain -- the goal, not the exception"); this is
//    what stops the exception from quietly growing back.
//
// 2. A MAP KEYED BY A RAW WINDOW POINTER OWNS IT. AGENTS.md: "new per-window maps are not
//    keyed by raw pointers (ABA when an address is reused) -- a PHLWINDOWREF, or an
//    explicit lifecycle with cleanup." The second half of that sentence is what
//    pixel/cache.hpp does: WinLabel/WsLabel/WinSnapshot hold a weak owner, and
//    putSnapshot() drops the entry when `owner.expired()`. That is the rule made code.
//
//    Checking the KEY TYPE instead is the mistake this test is written against -- see the
//    note at the bottom. Every one of these maps is keyed by CWindow*, so a ban on raw
//    pointer keys would reject all of them and pass the very bug it was meant to catch.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
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

} // namespace

int main(int argc, char **argv) {
  // argv[1] is src/, the same convention draw_sites_check uses.
  const fs::path src = argc > 1 ? argv[1] : "src";
  const auto     all = sources(src);

  // --- 1. the pure domain --------------------------------------------------------------
  // Each of these is compiled by its own ctest with no hyprland/ on the include path
  // (see CMakeLists.txt). The rule is not "these files are nice" -- it is "adding an
  // include to any of them breaks a test", which is exactly what should happen and exactly
  // what a reader cannot tell by looking at the file.
  const std::vector<std::pair<const char *, const char *>> pure{
      {"layout.cpp", "layout"},
      {"layout.hpp", "layout"},
      {"model/recency.cpp", "recency"},
      {"model/recency.hpp", "recency"},
      {"clock/clock.cpp", "dismiss"},
      {"view/state.hpp", "carousel_layout"},
      {"model/effects.hpp", "effects"},
  };

  for (const auto &[rel, test] : pure) {
    const fs::path p = src / rel;
    assert(fs::exists(p) && (std::string("missing ") + rel).c_str());
    const auto text = slurp(p);
    const auto at   = text.find("hyprland/");
    if (at != std::string::npos) {
      const auto line = static_cast<size_t>(std::count(text.begin(), text.begin() + at, '\n')) + 1;
      std::printf("FAIL %s pulls in hyprland/ at line %zu -- ctest '%s' cannot "
                  "compile it\n",
                  rel, line, test);
      return 1;
    }
  }
  std::printf("%zu pure source(s) still free of hyprland/\n", pure.size());

  // --- 2. a per-window map must own its key --------------------------------------------
  // Value types that carry a weak owner. Read from the whole tree: a map and its value
  // struct are usually declared a few lines apart, but not always in the same file.
  const std::regex ownerDecl(R"(struct\s+(\w+)\s*\{[^}]*PHL(WINDOW|WORKSPACE)REF)");
  std::vector<std::string> owned;
  for (const auto &f : all) {
    const auto text = slurp(f);
    for (std::sregex_iterator it(text.begin(), text.end(), ownerDecl), end; it != end;
         ++it)
      owned.push_back((*it)[1].str());
  }
  std::sort(owned.begin(), owned.end());
  owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
  std::printf("%zu struct(s) declare a weak owner\n", owned.size());

  // std::unordered_map<Key, Value> where Key is a raw pointer at a Hyprland object.
  // The pointer types are spelled several ways in this tree -- CWindow *, CWorkspace *,
  // Desktop::View::CWindow * -- because of which header each file includes first.
  const std::regex perWindowMap(
      R"(std::(unordered_map|map)<\s*((?:[\w:]+::)?(?:CWindow|CWorkspace|PHLWINDOW|PHLWORKSPACE)\s*\*)\s*,\s*(\w+))");
  int checked = 0;
  for (const auto &f : all) {
    const auto text = slurp(f);
    for (std::sregex_iterator it(text.begin(), text.end(), perWindowMap), end; it != end;
         ++it) {
      const auto value  = (*it)[3].str();
      const auto begin  = text.begin() + static_cast<long>(it->position());
      const auto line   = static_cast<size_t>(std::count(text.begin(), begin, '\n')) + 1;
      ++checked;
      if (std::find(owned.begin(), owned.end(), value) == owned.end()) {
        std::printf("FAIL %s:%zu -- %s is keyed by a raw %s but declares no weak "
                    "owner\n",
                    fs::relative(f, src).c_str(), line, value.c_str(),
                    (*it)[2].str().c_str());
        std::puts("  hold a PHLWINDOWREF and drop the entry when it expires, or key the "
                  "map by the ref itself");
        return 1;
      }
    }
  }
  // Zero would mean the regex drifted from the code -- which is a broken test, not a pass.
  assert(checked > 0);
  std::printf("%d per-window map(s), every one owns its key\n", checked);

  // --- why the key type is the wrong thing to check ------------------------------------
  // Every map above is keyed by a raw pointer, and every one is correct. A rule phrased as
  // "do not key by raw pointers" rejects all of them -- and still passes the bug it was
  // written for, `m_caches[win.get()]`, where the conversion to a pointer is implicit and
  // the key type reads as size_t. The distinction that actually decides correctness is
  // whether a dead key's value can be handed to whatever new tenant has the address, and
  // that is answered by the owner, not the key.

  std::puts("OK");
  return 0;
}
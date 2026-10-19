// The style sets, and the strings the motion code compares against.
//
// The value of this is not the membership check -- that is a loop. It is that the legal
// values are now DECLARED rather than implied by five string comparisons split across
// clocks.cpp and actions.cpp. Before, adding a style meant writing a branch and
// forgetting one left a value the config happily accepted and the motion ignored: no
// error, the transition just came out as something else.
//
// This test therefore also does the thing that makes the declaration stick: it reads the
// source of the two motion files and fails if a style name is compared there that is not
// in the corresponding list. A new style added to the renderer without the list shows up
// here; a list entry the renderer never reads shows up as unused below.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>

#include "../src/anim_types.hpp"

using namespace hyprview::anim;

namespace {

std::string slurp(const char *path) {
  std::ifstream f(path);
  assert(f && "cannot open the source to scan");
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto clocks  = slurp((root + "/src/clock/clocks.cpp").c_str());
  const auto actions = slurp((root + "/src/actions/actions.cpp").c_str());
  // parseSwapStyle -- the swap name -> SwapStyle mapping -- lives in overview.hpp, not in
  // actions.cpp, which is why a first pass at this scanned two files and reported
  // "horizontal" as unreachable when it is read on every swap.
  const auto overview = slurp((root + "/src/overview.hpp").c_str());

  // --- the sets are what they claim ------------------------------------------------
  for (const char *s : kEnterStyles)
    assert(isEnterStyle(s));
  std::puts("declared styles are accepted by their own predicates");

  // --- and nothing else is ---------------------------------------------------------
  // There used to be two lists and a pair of assertions separating them: "horizontal" was
  // swap-only and "slide" was enter-only, because SwapStyle and WsStyle were separate enums
  // whose first member had different names. One vocabulary means one list, and both
  // spellings now resolve to the same Move.
  assert(!isEnterStyle("instant"));   // that one is jump_style's, a different key
  assert(!isEnterStyle(""));
  assert(!isEnterStyle("Slide"));    // case is not normalised anywhere
  assert(parseMove("horizontal") == parseMove("slide"));
  std::puts("near-misses are rejected; horizontal and slide are one spelling each");

  // --- no dead members ----------------------------------------------------------------
  // This used to count how many style-NAME literals the renderer compared against, and
  // required at least one. After the unification that count is zero BY DESIGN: no branch
  // compares a string any more, parse() resolves the name once and everything downstream
  // reads an enum. A check that observes nothing is not a pass, so it is gone rather than
  // weakened.
  //
  // What replaces it is the same question asked per dispatcher, which is where the old
  // check was weakest: it asked whether a member was read ANYWHERE, so one file keeping a
  // reference hid another file dropping one -- deleting the Fade branch from fx.cpp went
  // green because clocks.cpp still mentioned Fade. Deleting a branch is exactly the bug.
  //
  // The rule is per file, and it is not uniform because the dispatchers are not uniform:
  // fx.cpp renders all three travels, while the entry and the exit distinguish slide from
  // fade and let pop BE the fall-through. A member the user can name, that the file which
  // dispatches on it never tests, is a dead key with an enum member instead of a string.
  struct Dispatcher {
    const char *file;
    const char *mustName[3];
  };
  const Dispatcher kDispatchers[] = {
      // all three: a flight is drawn, not just positioned
      {"src/render/fx.cpp", {"Slide", "Pop", "Fade"}},
      // the entry and the exit separate slide from fade; pop is the scale-in below
      // placement.cpp, not clocks.cpp: the entry's three branches moved there with box()
      // when Placement took the tile's position.
      {"src/clock/placement.cpp", {"Slide", "Fade"}},
  };
  int dead = 0;
  for (const auto &d : kDispatchers) {
    const auto src = slurp((root + "/" + d.file).c_str());
    for (const char *m : d.mustName) {
      if (!m)
        break;
      if (src.find(std::string("Travel::") + m) == std::string::npos) {
        std::printf("FAIL: %s dispatches on travel but never tests %s -- a style the "
                    "parser accepts and this renderer routes past\n", d.file, m);
        ++dead;
      }
    }
  }
  if (dead)
    std::exit(1);
  std::puts("every dispatcher tests every travel it can be asked to distinguish");

  // --- every canonical name round-trips through its parser ---------------------------
  // Two lists of strings and a motion code that compared them one by one is what let a
  // name with no branch reading it sit there looking configured. parse() resolves a name to
  // a Move in one place, so what is asserted is the round trip: moveName(parseMove(name))
  // must be name again. A member with no spelling goes red, and a spelling with no member
  // goes red -- the two can no longer drift apart, which is the whole point of resolving
  // once instead of comparing everywhere.
  {
    // The round trip, over the canonical spellings. It used to be over style NAMES, and the
    // axis rode along inside the same string -- "slidevert" was one name meaning "slide on
    // the vertical axis" -- so parse(slidevert) could not produce an enum whose name() was
    // "slidevert" again, because there was no such member. That gap is where the dead
    // `fade` lived. The axis is part of the spelling now, and a trace record parses back.
    int roundTripped = 0;
    for (const char *name : kMoveNames) {
      const std::string back = moveName(parseMove(name));
      if (back != name) {
        std::printf("FAIL \"%s\" parses to %s and back, not itself\n", name, back.c_str());
        std::exit(1);
      }
      ++roundTripped;
    }
    assert(roundTripped == static_cast<int>(std::size(kMoveNames)));
    std::printf("%d canonical name(s) round-trip through parseMove/moveName\n",
                roundTripped);

    // The short spellings resolve INTO the canonical set rather than beside it. Two names
    // for one thing is how "horizontal" and "slide" came to mean the same motion while
    // reading as different vocabularies.
    assert(moveName(parseMove("slidevert")) == "slide-vertical");
    assert(moveName(parseMove("slide")) == "slide-horizontal");
    assert(moveName(parseMove("horizontal")) == "slide-horizontal");
    std::puts("short spellings resolve into the canonical set");

    // A name outside the family must land on the kRows default (anim.cpp:40-41), not on
    // whatever the last branch happened to return. "horizontal" is the swap spelling and
    // has always fallen through to the default slide.
    assert(parseMove("horizontal").travel == Travel::Slide);
    assert(parseMove("").travel == Travel::Slide);
    assert(parseMove("Slide").travel == Travel::Slide);
    std::puts("a name outside the family falls back to the kRows default");
  }

  // The three branches at clocks.cpp:145, and every one of them is reachable. Fade used to
  // be in the sliding set, which is exactly how it became a dead key: with a slide
  // direction it travelled sideways, and without one it fell through to the pop scale-in.
  // A travel with no branch is not a setting, so each is asserted against the reader.
  {
    assert(Move{Travel::Slide, Axis::Horizontal}.slides());
    assert(Move{Travel::Slide, Axis::Vertical}.slides());
    assert(!Move{Travel::Fade, Axis::Horizontal}.slides());
    assert(!Move{Travel::Pop, Axis::Horizontal}.slides());

    // slidevert is slide on the vertical axis, said the long way round -- and it must keep
    // meaning that, because it is a spelling users have in their configs.
    const auto sv = parseMove("slidevert");
    assert(sv.travel == Travel::Slide && sv.axis == Axis::Vertical);
    assert(parseMove("slide") == Move{Travel::Slide, Axis::Horizontal});
    assert(parseMove("fade") == Move{Travel::Fade, Axis::Horizontal});
    assert(parseMove("pop") == Move{Travel::Pop, Axis::Horizontal});

    // Spring is deliberately not a member: it has no duration to sit in the same _ms
    // scale as the others, so accepting the spelling would promise something the code
    // does not do. Asserted so that adding it is a deliberate act, not a parser branch.
    assert(parseMove("spring") == Move{Travel::Slide, Axis::Horizontal});
    std::puts("slide, pop and fade each have a branch; slidevert is slide-vertical");
  }

  // Reading these numbers: "horizontal" sits at 3/4 on purpose. parseSwapStyle ends in
  // ": SwapStyle::Horizontal" -- it is the branch that catches everything unrecognised,
  // not a comparison against its own name. So it works, and is reachable, just not by
  // a string test. A number BELOW the list size for any other entry would be a key the
  // user can set that nothing reads.

  // --- the DEFAULT style must produce motion -------------------------------------------
  // landAfterMove branched on `gridSwapStyle() != Horizontal` and fell through to
  // pinNatural() when it WAS Horizontal -- so the default, the first value in the README
  // and the one every config copying the example carries, was the only style that
  // produced no flight of its own. A grid swap showed the pulse and two cards in their
  // new places with nothing travelling between them.
  //
  // The general rule: no style may be a "no animation" spelling. Every swap style has to
  // reach beginSwapFX, because switchTargets moves both windows in one frame and a plain
  // glide cannot read as two cards trading places.
  //
  // This asserts the SHAPE of the call site rather than running it: a condition whose
  // false arm returns without starting a flight is exactly the bug.
  {
    const auto actionsSrc = slurp((root + "/src/actions/actions.cpp").c_str());
    // landAfterMove's body: from its signature to the next top-level function.
    const auto start = actionsSrc.find("void Overview::landAfterMove");
    assert(start != std::string::npos && "landAfterMove not found");
    const auto end = actionsSrc.find("\nvoid Overview::", start + 10);
    const auto body = actionsSrc.substr(
        start, end == std::string::npos ? std::string::npos : end - start);

    // The style must be READ (a style accessor or parseMove appears), and the
    // flight must be started unconditionally within this function.
    assert(body.find("beginSwapFX") != std::string::npos);
    assert(body.find("parseMove") != std::string::npos);
    // A bare `if (... style ... != Horizontal)` guard is the bug: it makes one style
    // a spelling for "no flight". Reject the comparison outright -- and note that the
    // guard could not be written the old way any more, because there is no style whose
    // name is "Horizontal" any more: the axis is not a travel, and a branch can only
    // skip a travel it names.
    assert(body.find("!= model::SwapStyle") == std::string::npos);
    assert(body.find("model::SwapStyle") == std::string::npos);
    std::puts("every swap style reaches beginSwapFX; none is a no-flight spelling");
  }

  // --- every style KEY resolves to something -----------------------------------------
  // grid_swap_style and strip_swap_style are registered and documented, and for a long
  // time nothing read them: the call sites asked anim::style("grid_swap"), which looks
  // the name up in kRows -- where neither name exists -- so it returned {}. {} parses to
  // SwapStyle::Horizontal, which is the default, so the key parsed and did nothing. No
  // error, no log line: setting it just had no effect.
  //
  // The general shape of the bug is asking style() for a name style() cannot resolve.
  // So assert the rule instead of the two instances: any anim::style("x") / anim::cfg("x")
  // literal must name a kRows entry. kRows is the table, and the check is a substring scan
  // of the table for the quoted name.
  const auto animSrc = slurp((root + "/src/anim.cpp").c_str());
  const auto rowsAreDeclared = [&animSrc](const std::string &name) {
    // kRows rows start with {"name", -- so look for the name as a row's first field.
    return animSrc.find("{\"" + name + "\",") != std::string::npos;
  };

  // anim_recipes.hpp is in the list because resolve() is now where the two handle-backed
  // styles are read: the names moved out of clocks.cpp when the six recipes took an owner,
  // and a check that only watched the old file would now assert on an empty scan.
  const auto recipes = slurp((root + "/src/anim_recipes.hpp").c_str());
  const auto styleCallers = {&clocks, &actions, &overview, &recipes};
  int styleCalls = 0;
  // Both spellings: "anim::style(" outside the namespace and a bare "style(" inside it.
  // anim_recipes.hpp lives in hyprview::anim, where the qualified form would not compile.
  const std::string kQual{"anim::style(\""}, kBare{"style(\""};
  for (const auto *src : styleCallers) {
    for (const auto *needle : {&kQual, &kBare}) {
      for (std::size_t p = src->find(*needle); p != std::string::npos;
           p = src->find(*needle, p + 1)) {
        const auto at = p + needle->size();
        const auto e  = src->find('"', at);
        const std::string name = src->substr(at, e - at);
        ++styleCalls;
        if (!rowsAreDeclared(name)) {
          std::printf("FAIL %s%s\" -- no such row in kRows, so it returns {}\n",
                      needle->c_str(), name.c_str());
          std::exit(1);
        }
      }
    }
  }
  assert(styleCalls > 0);
  std::printf("%d anim::style() call(s), every name is a kRows row\n", styleCalls);

  // The two handle-backed keys must be read through their accessors, not through
  // style() -- that is the whole fix, and this is what stops it reverting.
  for (const auto *src : styleCallers) {
    assert(src->find("anim::style(\"grid_swap\"") == std::string::npos);
    assert(src->find("anim::style(\"strip_swap\"") == std::string::npos);
  }
  assert(animSrc.find("std::string gridSwapStyle()") != std::string::npos);
  assert(animSrc.find("std::string stripSwapStyle()") != std::string::npos);
  std::puts("grid_swap_style / strip_swap_style read through their accessors");

  // --- every row that HAS a style can be WRITTEN ---------------------------------------
  // style() ends in `overridden("<row>_style")`, but setTuning() accepted `_style` only for
  // three hardcoded names. So `set ws_enter_style pop` answered "unknown key" while
  // style("ws_enter") read a key no command could produce -- a documented key no path
  // reaches, which is the grid_swap_style shape one level down.
  //
  // The rule, not the two instances: a kRows row with hasStyle is readable through
  // `<row>_style`, so it must be writable through it too.
  {
    const auto rowsWithStyle = [&animSrc](std::vector<std::string> &out) {
      const auto t0 = animSrc.find("std::array<Row");
      const auto t1 = animSrc.find("= {{", t0);
      const auto t2 = animSrc.find("}};", t1);
      const auto body = animSrc.substr(t1, t2 - t1);
      size_t pos = 0;
      while ((pos = body.find("{\"", pos)) != std::string::npos) {
        const auto q = body.find('"', pos + 2);
        if (q == std::string::npos)
          break;
        const auto name = body.substr(pos + 2, q - pos - 2);
        // A row is {"name", {}, {}, {}, {}, {}, ms, "curve", hasStyle, "style"} --
        // hasStyle is the last bool before the quoted default. hasStyle true is always
        // followed by a non-empty style string, so the two appear together and the
        // emptiness is the reliable marker: an unstyled row ends with false, "".
        const auto endRow = body.find("},", q);
        const auto row = body.substr(q, endRow == std::string::npos ? std::string::npos
                                                                   : endRow - q);
        if (row.size() >= 2 && row.compare(row.size() - 2, 2, "\"\"") != 0)
          out.push_back(name);
        pos = q + 1;
      }
    };
    std::vector<std::string> styled;
    rowsWithStyle(styled);
    assert(!styled.empty() && "no styled kRows row found -- the scan drifted from kRows");

    // Which keys setTuning accepts, read from its own body rather than restated here.
    // The marker is the comparison, not the bare literal: the suffix also appears in a
    // branch that has been switched off, and a test that passed on a dead branch is worse
    // than no test at all. (This comment deliberately spells the marker differently from
    // the code, for the same reason -- a comment containing it would keep the test green
    // against exactly the bug it exists to catch.)
    const auto st0 = animSrc.find("bool setTuning(");
    const auto st1 = animSrc.find("\n}", st0);
    const auto setTuningBody =
        animSrc.substr(st0, st1 == std::string::npos ? std::string::npos : st1 - st0);
    const bool genericStyle = setTuningBody.find("\"_style\") == 0") != std::string::npos;

    int unreachable = 0;
    for (const auto &row : styled) {
      const std::string key = row + "_style";
      const bool hardcoded =
          setTuningBody.find("\"" + key + "\"") != std::string::npos;
      if (!hardcoded && !genericStyle) {
        std::printf("FAIL %s -- style() reads \"%s\" but setTuning cannot write it\n",
                    row.c_str(), key.c_str());
        ++unreachable;
      }
    }
    assert(unreachable == 0);
    std::printf("%d styled row(s), every one writable through <row>_style\n",
                (int)styled.size());
  }

  // --- the Id enum and kRows cannot drift ----------------------------------------------
  // cfg(Id) is cfg(kRows[(size_t)id].name), and kRows is std::array<Row, Id::Count>, so
  // a missing row would not compile and the enum path cannot silently name nothing.
  // Worth pinning anyway: the ORDER is the link, and an enum member reordered against
  // the table would compile and make every Id resolve to another animation's config.
  // grid_swap_style's bug was the string path; this is what keeps the enum path honest.
  {
    const auto animHdr = slurp((root + "/src/anim.hpp").c_str());
    const auto animSrc = slurp((root + "/src/anim.cpp").c_str());

    // Id members, minus the sentinel: `Count` is the array size, not a row.
    std::vector<std::string> ids;
    {
      // From the brace that OPENS the body: "enum class Id : uint8_t {" has one
      // already, and taking the text after the header keeps `std::uint8_t` out.
      const auto decl = animHdr.find("enum class Id");
      const auto e0 = animHdr.find('{', decl);
      const auto e1 = animHdr.find("};", e0);
      assert(e0 != std::string::npos && e1 != std::string::npos &&
             "enum class Id not found");
      const auto body = animHdr.substr(e0 + 1, e1 - e0 - 1);
      std::string cur;
      for (const char c : body) {
        if (c == ',') {
          if (!cur.empty())
            ids.push_back(cur);
          cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
          cur.push_back(c);
        }
      }
      if (!cur.empty())
        ids.push_back(cur);
      const auto at = std::find(ids.begin(), ids.end(), "Count");
      assert(at != ids.end() && "Id::Count sentinel not found");
      ids.erase(at);
    }

    // kRows, scoped to its initialiser so a Row literal elsewhere cannot leak in.
    std::vector<std::string> rows;
    {
      const auto t0 = animSrc.find("std::array<Row");
      const auto t1 = animSrc.find("= {{", t0);
      const auto t2 = animSrc.find("}};", t1);
      const auto body = animSrc.substr(t1, t2 - t1);
      for (std::size_t p = body.find("{\""); p != std::string::npos;
           p = body.find("{\"", p + 2)) {
        const auto q = body.find('"', p + 2);
        if (q != std::string::npos)
          rows.push_back(body.substr(p + 2, q - (p + 2)));
      }
    }

    if (ids.size() != rows.size()) {
      std::printf("FAIL %zu ids vs %zu rows in kRows\n", ids.size(), rows.size());
      std::exit(1);
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
      // The naming convention: Id::SwapMain <-> "swap_main", Id::StripStep <->
      // "strip_step". Lowercase with an underscore before every capital that is not
      // first. cfg(Id) is cfg(kRows[id].name), so getting this wrong would compile and
      // silently give an Id another animation's duration -- which is the same class of
      // quiet failure as grid_swap_style.
      std::string want;
      for (std::size_t k = 0; k < ids[i].size(); ++k) {
        const char c = ids[i][k];
        if (k && std::isupper(static_cast<unsigned char>(c)))
          want.push_back('_');
        want.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      if (want != rows[i]) {
        std::printf("FAIL Id::%s -> \"%s\", expected \"%s\"\n", ids[i].c_str(),
                    rows[i].c_str(), want.c_str());
        std::exit(1);
      }
    }
    std::printf("%zu ids match kRows by name and order\n", ids.size());
  }

  std::puts("OK");
  return 0;
}
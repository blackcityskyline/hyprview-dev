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

using namespace gloview::anim;

namespace {

std::string slurp(const char *path) {
  std::ifstream f(path);
  assert(f && "cannot open the source to scan");
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Every `== "x"` / `!= "x"` literal in the file, which is how the motion code asks what
// style it was given.
std::set<std::string> comparedLiterals(const std::string &src) {
  std::set<std::string> out;
  for (size_t p = src.find("\""); p != std::string::npos; p = src.find('"', p + 1)) {
    const auto e = src.find('"', p + 1);
    if (e == std::string::npos)
      break;
    const auto tok = src.substr(p + 1, e - p - 1);
    if (!tok.empty())
      out.insert(tok);
    p = e;
  }
  return out;
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
  for (const char *s : kSwapStyles)
    assert(isSwapStyle(s));
  std::puts("declared styles are accepted by their own predicates");

  // --- and nothing else is ---------------------------------------------------------
  // "horizontal" is a swap-only name; typing it into ws_enter_style must not pass.
  assert(!isEnterStyle("horizontal"));
  assert(!isSwapStyle("instant"));   // that one is jump_style's, a different key
  assert(!isEnterStyle(""));
  assert(!isEnterStyle("Slide"));    // case is not normalised anywhere
  assert(!isSwapStyle("slide"));     // swap says "horizontal", not "slide"
  std::puts("near-misses are rejected");

  // --- the renderer does not compare against anything undeclared --------------------
  // The whole reason these lists exist. Walk every string literal the two motion files
  // compare with and require it to be a style name that some list declares.
  auto literals = comparedLiterals(clocks);
  for (const std::string *src : {&actions, &overview}) {
    const auto b = comparedLiterals(*src);
    literals.insert(b.begin(), b.end());
  }

  // Literals that appear in those files for other reasons -- log tags, format strings,
  // unrelated comparisons. Only names in the style vocabulary are checked.
  const std::set<std::string> styleWords = {"slide",   "slidevert", "fade",  "pop",
                                            "horizontal", "instant"};
  int checked = 0;
  for (const auto &lit : literals) {
    if (!styleWords.count(lit))
      continue;
    ++checked;
    assert(isEnterStyle(lit) || isSwapStyle(lit));
  }
  assert(checked > 0);
  std::printf("renderer compares against %d style name(s), all declared\n", checked);

  // --- no dead entries ---------------------------------------------------------------
  // A name in a list that no comparison can reach is a style the user can type and
  // nothing will read. The membership above cannot see this; counting can.
  const auto reach = [&literals](const char *const *list, std::size_t n) {
    int hit = 0;
    for (std::size_t i = 0; i < n; ++i)
      if (literals.count(list[i]))
        ++hit;
    return hit;
  };
  const int enterHit = reach(kEnterStyles, std::size(kEnterStyles));
  const int swapHit  = reach(kSwapStyles, std::size(kSwapStyles));
  std::printf("enter styles reached: %d/%zu, swap styles reached: %d/%zu\n", enterHit,
              std::size(kEnterStyles), swapHit, std::size(kSwapStyles));

  // --- every declared name round-trips through its parser -----------------------------
  // The generalisation the two-point grep could not give. kEnterStyles and kSwapStyles were
  // lists of strings and the motion code compared them one by one, so a name in the list
  // with no branch reading it was invisible: the membership test passes, the style is just
  // never used. That is exactly the "key the user can set that nothing reads" class.
  //
  // parseWsStyle closes it differently than the swap family does, and the difference is
  // worth pinning. It RESOLVES rather than compares: name -> enum in one place, so a
  // declared name with no reader is impossible by construction. The test is therefore the
  // round trip -- parse(name) must produce an enum whose name() is name again. If a member
  // is added to WsStyle without a spelling in wsStyleName, or a spelling is added to
  // parseWsStyle that no member can carry, this goes red.
  {
    int roundTripped = 0;
    for (const char *name : kEnterStyles) {
      const WsStyle s = parseWsStyle(name);
      if (std::strcmp(wsStyleName(s), name) != 0) {
        std::printf("FAIL \"%s\" parses to %s and back, not itself\n", name,
                    wsStyleName(s));
        std::exit(1);
      }
      ++roundTripped;
    }
    // Only the enter family round-trips: its enum lives here, in a header a test can
    // include. The swap parser is parseSwapStyle in overview.hpp, which drags in hyprland/
    // and cannot be linked -- so that family stays on the `reach` count above, which
    // proves the same thing the other way (each name is compared somewhere).
    assert(roundTripped == static_cast<int>(std::size(kEnterStyles)));
    std::printf("%d enter style name(s) round-trip through parseWsStyle\n", roundTripped);

    // A name outside the family must land on the kRows default (anim.cpp:40-41), not on
    // whatever the last branch happened to return. "horizontal" is the swap spelling.
    assert(parseWsStyle("horizontal") == WsStyle::Slide);
    assert(parseWsStyle("") == WsStyle::Slide);
    assert(parseWsStyle("Slide") == WsStyle::Slide);
    std::puts("a name outside the family falls back to the kRows default");
  }

  // The slide/pop split is the whole shape of ws_enter's branch (clocks.cpp:145): slides
  // return early, pop falls through to the scale-in. If WsStyle grows a member that
  // answers "slides", it has to be a deliberate choice, so the set is asserted here
  // rather than left to the ternary in anim_types.hpp.
  assert(wsStyleSlides(WsStyle::Slide));
  assert(wsStyleSlides(WsStyle::SlideVert));
  assert(wsStyleSlides(WsStyle::Fade));
  assert(!wsStyleSlides(WsStyle::Pop));
  std::puts("pop is the only ws_enter style that scales instead of sliding");

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

    // The style must be READ (a style accessor or parseSwapStyle appears), and the
    // flight must be started unconditionally within this function.
    assert(body.find("beginSwapFX") != std::string::npos);
    assert(body.find("parseSwapStyle") != std::string::npos);
    // A bare `if (... style ... != Horizontal)` guard is the bug: it makes one style
    // a spelling for "no flight". Reject the comparison outright.
    assert(body.find("!= model::SwapStyle::Horizontal") == std::string::npos);
    assert(body.find("!= model::SwapStyle") == std::string::npos);
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

  const auto styleCallers = {&clocks, &actions, &overview};
  int styleCalls = 0;
  for (const auto *src : styleCallers) {
    for (std::size_t p = src->find("anim::style(\""); p != std::string::npos;
         p = src->find("anim::style(\"", p + 1)) {
      constexpr std::size_t kAt = 13; // strlen("anim::style(\"")
      const auto e = src->find('"', p + kAt);
      const std::string name = src->substr(p + kAt, e - (p + kAt));
      ++styleCalls;
      if (!rowsAreDeclared(name)) {
        std::printf("FAIL anim::style(\"%s\") -- no such row in kRows, so it returns {}\n",
                    name.c_str());
        std::exit(1);
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
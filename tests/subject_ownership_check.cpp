// A SubjectRef is a NUMBER. Someone has to hold the window it stands for.
//
// This check exists because that someone was wrong once, and the failure was not a wrong
// answer -- it was a dead session. WindowsContentSource::windowOf() rebuilt a shared pointer
// from a raw address, the ownership had ended with the loop that produced the identity, and
// the release of an already-released window took the compositor down:
//
//   CSharedPointer<CWindow>::_delete -> ~CSharedPointer -> Overview::buildTiles
//
// Reading this as a grep is the point, not a compromise. The ownership lives in two files
// that cannot be compiled without a compositor, so no unit test can reach it -- exactly the
// situation AGENTS.md 2.8 describes, and the reason scan_draw_sites is a grep too. A unit
// test on a fake source proves the CONTRACT holds; it cannot prove the adapter honours it.
//
// Four rules, all of which the crashing revision violated at least one of:
//
//   1. enumerate() is not const. Enumerating is what refreshes what the source holds alive.
//      A const signature would push that into a mutable member and hide the mutation.
//   2. enumerate() fills the alive list. Otherwise there is nothing for windowOf to search
//      and every lookup answers null -- an overlay that draws nothing and reports no error.
//   3. windowOf() never casts the id. It searches the alive list and returns a COPY, so the
//      address it hands out cannot have been freed underneath it.
//   4. buildTiles() takes its subjects from enumerate(). Minting them there produces tiles
//      that nothing can resolve -- the same crash, approached from the other side.

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

void want(bool ok, const char *rule, const std::string &detail) {
  if (ok)
    return;
  ++failures;
  std::printf("FAIL %s: %s\n", rule, detail.c_str());
}

// The body of a member function, from its signature to the line that closes it. Crude on
// purpose: braces inside string literals do not appear in this file, and a wrong slice here
// shows up as a FAIL rather than as a silent pass.
std::string body(const std::string &src, const std::string &sig) {
  const auto at = src.find(sig);
  if (at == std::string::npos)
    return {};
  auto i = src.find('{', at);
  if (i == std::string::npos)
    return {};
  int depth = 0;
  for (std::size_t j = i; j < src.size(); ++j) {
    if (src[j] == '{')
      ++depth;
    else if (src[j] == '}' && --depth == 0)
      return src.substr(i, j - i + 1);
  }
  return {};
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto iface = slurp(root + "/src/model/content_source.hpp");
  const auto hpp = slurp(root + "/src/model/window_source.hpp");
  const auto cpp = slurp(root + "/src/model/window_source.cpp");
  const auto build = slurp(root + "/src/build/build.cpp");

  // Rule 1 -- enumerate() is a mutating operation, declared as one everywhere.
  want(iface.find("virtual std::vector<SubjectRef> enumerate() = 0;") != std::string::npos,
       "rule1", "IContentSource::enumerate() must not be const");
  want(hpp.find("std::vector<SubjectRef> enumerate() override;") != std::string::npos,
       "rule1", "WindowsContentSource::enumerate() must not be const");
  want(cpp.find("WindowsContentSource::enumerate() {") != std::string::npos, "rule1",
       "WindowsContentSource::enumerate() definition is const, or is missing");

  // Rule 2 -- the alive list exists, is filled, and is cleared rather than appended to.
  const auto alive = body(cpp, "WindowsContentSource::enumerate() {");
  want(hpp.find("m_alive;") != std::string::npos, "rule2",
       "no alive list on the source: windowOf would have nothing to search");
  want(alive.find("m_alive.push_back(w)") != std::string::npos, "rule2",
       "enumerate() does not take ownership of what it hands out");
  want(alive.find("m_alive.clear();") != std::string::npos, "rule2",
       "enumerate() appends instead of rebuilding: a stale identity would keep resolving");

  // Rule 3 -- the id may be READ, never OWNED.
  //
  // The distinction is the whole check, so it is worth being exact about: casting s.id to a
  // pointer for a COMPARISON is fine and necessary -- that is how a list is searched. Building
  // a shared pointer out of it is the use-after-free. A blunt "no s.id anywhere" rule flagged
  // the comparison too, which was wrong: the check has to fail on the crash, not on the search.
  const auto winOf = body(cpp, "WindowsContentSource::windowOf(SubjectRef s) const {");
  want(cpp.find("PHLWINDOW{reinterpret_cast") == std::string::npos, "rule3",
       "a shared pointer is constructed straight from an address -- that is the "
       "use-after-free that took the compositor down");
  want(cpp.find("return PHLWINDOW{") == std::string::npos, "rule3",
       "windowOf() returns a pointer built from the id rather than one it holds");
  want(winOf.find("m_alive") != std::string::npos, "rule3",
       "windowOf() does not search what enumerate() holds, so it can only be guessing");
  want(winOf.find("return w;") != std::string::npos, "rule3",
       "windowOf() must hand back a COPY of the reference it found, not a view onto it");

  // Rule 4 -- tiles get their identity from the source, never minted at the point of use.
  const auto buildTiles = body(build, "void Overview::buildTiles() {");
  want(buildTiles.find("m_windows.enumerate()") != std::string::npos, "rule4",
       "buildTiles() does not enumerate: its identities belong to nobody that can resolve "
       "them, and every tile resolves to null");
  want(buildTiles.find("Desktop::windowState()->windows()") == std::string::npos, "rule4",
       "buildTiles() walks the window list itself instead of going through the source");

  if (failures == 0) {
    std::printf("subject_ownership: 4 rules hold\n");
    return 0;
  }
  std::printf("subject_ownership: %d violation(s)\n", failures);
  return 1;
}
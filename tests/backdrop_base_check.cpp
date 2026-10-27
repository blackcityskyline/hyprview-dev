// The backdrop's base: the sharp source, with the glass over it. The blur rides the glass
// value, which holds while a window is under the overlay -- a transparent window's blur is the
// compositor's, and ours has to stand in for it once we cover it. When the glass does fade, it
// fades over the sharp source, never over a stale framebuffer.
//
// Two regressions this pins: the Blur branch drawing the glass without the source under it
// (a fading blur would fade to a stale framebuffer), and the sharp source being faded with
// the glass (the desktop would darken and then brighten at the cut).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string bodyOf(const std::string &src, const std::string &signature) {
  const auto at = src.find(signature);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  bool seen = false;
  for (std::size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
      seen = true;
    } else if (src[i] == '}') {
      --depth;
      if (seen && depth == 0)
        return src.substr(at, i - at + 1);
    }
  }
  return {};
}

int fail(const char *what) {
  std::printf("FAIL: %s\n", what);
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto src = slurp(root + "/src/render/backdrop.cpp");
  if (src.empty())
    return fail("cannot read src/render/backdrop.cpp");

  // The base is decided by the rule, not by a condition of its own.
  if (src.find("backdropBase(") == std::string::npos)
    return fail("the base is not decided by backdropBase()");

  if (src.find("case BackdropBase::Source:") == std::string::npos ||
      src.find("case BackdropBase::Blur:") == std::string::npos)
    return fail("the base branches are gone -- the blur and the source are one image");

  const auto sourceBranch = bodyOf(src, "case BackdropBase::Source:");
  if (sourceBranch.find("comp") == std::string::npos)
    return fail("the Source branch draws nothing -- with the blur off there is no backdrop");

  const auto blurBranch = bodyOf(src, "case BackdropBase::Blur:");
  if (blurBranch.find("comp, 1.0F") == std::string::npos ||
      blurBranch.find("cachedTex") == std::string::npos)
    return fail("the Blur branch must draw the sharp source at full alpha, then the glass over it");

  if (src.find("drawBlended(comp, static_cast<float>(glassK))") != std::string::npos)
    return fail("the sharp source fades with the glass -- the fade has nothing opaque under it");

  // The glass is keyed on the desktop under it: holding it over a window is what keeps a
  // transparent card from showing a sharp hole.
  const auto clocks = slurp(root + "/src/clock/clocks.cpp");
  if (clocks.find("anim::glassAlpha(eased(), m_place.progress.raw(), opening(), m_landingEmpty)") == std::string::npos)
    return fail("the glass is no longer keyed on the landing -- a windowed desktop fades and holes");

  // The parked phase primes the blur: the first VISIBLE frame must find one ready.
  const auto prime = bodyOf(src, "void Overview::primeBackdrop()");
  if (prime.empty())
    return fail("primeBackdrop not found");
  if (prime.find("ensureBlur(") == std::string::npos)
    return fail("the parked phase does not prime the blur -- the first visible frame finds no blur");

  std::printf("backdrop base: the sharp source, the glass over it\n");
  return 0;
}

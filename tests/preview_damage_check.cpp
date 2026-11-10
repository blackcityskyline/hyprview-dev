// A preview repaints because its OWN window committed pixels, never because it repainted.
//
// Three separate holes, each measured live before it was closed:
//  - CWindow::commitWindow drops the damage of a window whose workspace is off screen
//    (Window.cpp:2750), so a card of another workspace never hears its window (previews=0 with
//    a video on ws5 and the overview on ws1).
//  - Chromium and mpv commit a blue-sky subsurface, not the toplevel surface, and a blue-sky
//    surface has no view of its own: the commit was never attributed to a window.
//  - Chromium acknowledges a frame with an empty-damage commit. Feeding that back damages the
//    card, the card repaints, the repaint hands out a frame callback, the client commits again:
//    an idle overview ran at a locked 60 fps and 30% of a core with nothing moving.

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The body of one function: from its signature to the brace that closes it.
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

bool has(const std::string &body, const char *needle, const char *why) {
  if (!body.empty() && body.find(needle) != std::string::npos)
    return true;
  std::printf("FAIL: %s\n", why);
  return false;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  int               failed = 0;

  const std::string LIFE = slurp(root + "/src/session/lifecycle.cpp");
  const std::string HOOKS = slurp(root + "/src/session/hooks.cpp");
  if (LIFE.empty() || HOOKS.empty()) {
    std::printf("FAIL: cannot read src/session/lifecycle.cpp / src/session/hooks.cpp\n");
    return 1;
  }

  // Hiding is not "this window is on my workspace": every card is painted here, so every card's
  // window must stop drawing at its own place.
  const auto HIDE = bodyOf(LIFE, "bool Overview::shouldHideWindow(");
  if (!has(HIDE, "showsAsCard(",
           "shouldHideWindow only hides tiles and the active workspace; a card of another "
           "workspace keeps drawing into its off-screen box"))
    failed = 1;

  // The card's damage follows the committing surface to its window: a subsurface has none.
  if (!has(HOOKS, "SURFACE_ROLE_SUBSURFACE",
           "hooks.cpp never climbs to the toplevel surface; a subsurface commit cannot be "
           "attributed to a window"))
    failed = 1;

  // Empty commit is not a repaint. Without this the preview and its client drive each other.
  const auto SURF = bodyOf(HOOKS, "void hkDamageSurface(");
  if (!has(SURF, "computeDamage().empty()",
           "hkDamageSurface damages the card on every commit, including the empty ones clients "
           "send to acknowledge a frame"))
    failed = 1;
  if (!has(SURF, "damageWindowPreview(",
           "hkDamageSurface no longer routes a commit to the card that shows the window"))
    failed = 1;

  // Off-screen workspaces never reach damageSurface: commitWindow is the only place left.
  const auto COMMIT = bodyOf(HOOKS, "void hkCommitWindow(");
  if (COMMIT.empty()) {
    std::printf("FAIL: no hkCommitWindow(): a window on a non-visible workspace has no path to "
                "its card\n");
    failed = 1;
  } else {
    if (!has(COMMIT, "commitWindowOrig()", "hkCommitWindow does not call the original"))
      failed = 1;
    if (!has(COMMIT, "damageWindowPreview(",
             "hkCommitWindow does not damage the card: the card freezes on its first frame"))
      failed = 1;
  }
  if (!has(HOOKS, "\"commitWindow\"", "commitWindow is never hooked"))
    failed = 1;
  if (!has(HOOKS, "m_commitWindowHook->hook()",
           "the commitWindow hook is created but never installed"))
    failed = 1;

  if (!failed)
    std::printf("preview damage: cards follow their window's pixels, empty commits are ignored\n");
  return failed;
}

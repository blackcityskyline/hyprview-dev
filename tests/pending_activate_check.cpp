// One action for a window and for a tab. The browser only adds WHICH TAB to switch, and the
// request goes out with the pick: the browser is behind the overlay while the card flies, and
// the mediator polls for the request -- a request sent at the exit lands after the plugin is
// already gone, and the browser shows the old tab when it comes forward.

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

int fail(const char *what) {
  std::printf("FAIL: %s\n", what);
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";

  const std::string build = slurp(root + "/src/build/build.cpp");
  const std::string life = slurp(root + "/src/session/lifecycle.cpp");
  const std::string head = slurp(root + "/src/overview.hpp");
  if (build.empty())
    return fail("src/build/build.cpp not found");
  if (life.empty())
    return fail("src/session/lifecycle.cpp not found");
  if (head.empty())
    return fail("src/overview.hpp not found");

  // The pick sends the request: the browser is behind the overlay while the card flies, and
  // the mediator polls for it -- a request sent at the exit lands after the plugin is gone.
  const auto pick = build.find("void Overview::activateTile(");
  if (pick == std::string::npos)
    return fail("the pick of a card is gone from build.cpp");
  const auto body = build.substr(pick, build.find("\n}\n", pick) - pick);
  if (body.find("writeTabRequest(") == std::string::npos)
    return fail("the pick no longer asks the browser to switch the tab -- the request would "
                "then go out at the exit, after the plugin is gone");

  if (life.find("writeTabRequest(") != std::string::npos)
    return fail("the exit sends the tab request -- it must go out with the pick, so the switch "
                "happens while the card is still flying");

  // One pending action, not two.
  if (head.find("m_pendingFocus") != std::string::npos)
    return fail("there are two pendings again -- the window and the tab must be one action");

  std::printf("PASS: the tab switches with the focus, in one action\n");
  return 0;
}

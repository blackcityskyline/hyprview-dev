// One action, one moment. Picking a card is the same action for a window and for a tab: the
// card flies home, the overlay leaves, and the focus lands. The browser only adds WHICH TAB to
// switch, and that rides along with the focus -- a tab switched earlier changes the page
// behind the card that is still flying to it.

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

  // The pick never talks to the browser: the request goes out with the focus.
  const auto pick = build.find("void Overview::activateTile(");
  if (pick == std::string::npos)
    return fail("the pick of a card is gone from build.cpp");
  const auto body = build.substr(pick, build.find("\n}\n", pick) - pick);
  if (body.find("writeTabRequest(") != std::string::npos)
    return fail("the pick still sends the tab request itself -- the switch belongs to the "
                "focus, so it happens when the card has landed");

  if (life.find("writeTabRequest(") == std::string::npos)
    return fail("nothing sends the tab request any more");

  // One pending action, not two.
  if (head.find("m_pendingFocus") != std::string::npos)
    return fail("there are two pendings again -- the window and the tab must be one action");

  std::printf("PASS: the tab switches with the focus, in one action\n");
  return 0;
}

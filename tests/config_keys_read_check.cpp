// A config key that is registered but never read is a lie, and nothing in the test suite can
// see it.
//
// scan_config_keys already proves every key is registered, and that is the easy half: adding
// the line is all it takes to pass. Reading it is the part that decays, and it decays
// silently -- `tile_mode` sat registered and unread for the whole of the tab work, with a
// default of "windows" that happened to match what the code did, so nothing looked wrong and
// nothing was: a user setting tile_mode = "tabs" would have been ignored with no trace.
//
// So: a key nobody reads is a FAIL, not a warning. The fix is to delete the key or read it,
// and both are right; quietly leaving it is not.
//
// What this does NOT see, stated here so nobody trusts it further than it goes: a value read
// into a local and dropped passes. Telling a dead read from a live one is dataflow analysis,
// not a grep, and AGENTS.md 7.1.5 already draws that line -- a unit cannot check the caller's
// argument, and this is the check that stands in for it. Verified while writing it: putting
// `const auto mode = cfg::behavior.tile_mode.get(); (void)mode;` into lifecycle.cpp left this
// green. The gap is accepted and recorded rather than papered over with a heuristic that would
// also reject the legitimate one-line reads this is for.

#include <cassert>
#include <cstdio>
#include <dirent.h>
#include <fstream>
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

// The sources to search, taken from CMakeLists rather than walked: CMakeLists is already the
// authoritative list of what compiles into the plugin, and walking the tree would search files
// no target ever builds -- which would make a key look read because of code that cannot run.
std::vector<std::string> sources(const std::string &root) {
  std::vector<std::string> out;
  std::istringstream in(slurp(root + "/CMakeLists.txt"));
  std::string line;
  while (std::getline(in, line)) {
    const auto at = line.find("src/");
    if (at == std::string::npos)
      continue;
    const auto rest = line.substr(at);
    if (rest.find(".cpp") == std::string::npos && rest.find(".hpp") == std::string::npos)
      continue;
    const auto end = rest.find_first_of(" \t)");
    const auto rel = end == std::string::npos ? rest : rest.substr(0, end);
    if (rel.find("config/config") != std::string::npos)
      continue; // the writer is not a reader
    out.push_back(root + "/" + rel);
  }
  return out;
}

std::vector<std::string> split_lines(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string line;
  while (std::getline(in, line))
    out.push_back(line);
  return out;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";

  // config.cpp is the single writer: one table, every key, nothing declared elsewhere.
  //
  // What gets searched for is the MEMBER EXPRESSION, not the key string, and the difference
  // is not cosmetic: `carousel_on_open` lives in `carousel.on_open` and `label_color` in
  // `colors.label`, so searching for the key finds neither in the reader and reports 59 of 131
  // keys unread when every one of them is read. That is what this check looked like first.
  struct Key {
    std::string name;
    std::string member;
  };
  std::vector<Key> keys;
  for (const auto &line : split_lines(slurp(root + "/src/config/config.cpp"))) {
    const auto amp = line.find("{&");
    if (amp == std::string::npos)
      continue;
    const auto comma = line.find(',', amp);
    if (comma == std::string::npos)
      continue;
    Key k;
    k.member = line.substr(amp + 2, comma - amp - 2);
    const auto at = line.find("\"plugin:hyprview:");
    if (at == std::string::npos)
      continue;
    const auto end = line.find('"', at + 16);
    if (end == std::string::npos)
      continue;
    k.name = line.substr(at + 16, end - at - 16);
    keys.push_back(k);
  }
  assert(!keys.empty() && "no keys found in config.cpp");

  const auto files = sources(root);
  int failures = 0;

  for (const auto &k : keys) {
    bool read = false;
    for (const auto &f : files) {
      std::istringstream in(slurp(f));
      std::string line;
      while (std::getline(in, line)) {
        const auto c = line.find("//");
        const auto code = c == std::string::npos ? line : line.substr(0, c);
        // Read means reached through the cfg:: accessor. The bare member name matches the
        // config struct's own declaration sites otherwise, which would pass everything.
        if (code.find("cfg::") != std::string::npos &&
            code.find(k.member) != std::string::npos) {
          read = true;
          break;
        }
      }
      if (read)
        break;
    }
    if (!read) {
      ++failures;
      std::printf("FAIL %s (%s) is registered but nothing reads it\n", k.name.c_str(),
                  k.member.c_str());
    }
  }

  if (failures) {
    std::printf("config_keys_read: %d of %zu key(s) unread\n", failures, keys.size());
    return 1;
  }
  std::printf("config_keys_read: %zu key(s) registered and read\n", keys.size());
  return 0;
}
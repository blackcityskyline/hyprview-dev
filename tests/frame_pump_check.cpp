// The pump's timer is disarmed, never reset. reset() drops the last shared pointer, and the
// pump is reachable from the timer's own callback: the pointer dies inside the callback that
// owns it, and the session dies with it (SIGSEGV 2026-10-13, _delete -> nudgeTimers).
//
// The rule is two words in the source, and a comment saying "do not reset it" is not the rule:
// the words are what the compiler sees.

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string codeOnly(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  bool inBlock = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!inBlock && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n')
        ++i;
      out += '\n';
      continue;
    }
    if (!inBlock && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
      inBlock = true;
      i += 1;
      continue;
    }
    if (inBlock) {
      if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/') {
        inBlock = false;
        i += 1;
      }
      continue;
    }
    out += text[i];
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";

  std::ifstream f(root + "/src/render/frame_pump.cpp");
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (text.empty()) {
    std::printf("FAIL: cannot read src/render/frame_pump.cpp\n");
    return 1;
  }

  const std::string code = codeOnly(text);
  if (code.find("reset()") != std::string::npos) {
    std::printf("FAIL: the pump resets its timer -- reset() drops the last shared pointer inside "
                "the callback that owns it (SIGSEGV 2026-10-13)\n");
    return 1;
  }
  if (code.find("cancel()") == std::string::npos) {
    std::printf("FAIL: the pump never disarms its timer\n");
    return 1;
  }

  std::printf("PASS: the pump disarms its timer and never resets it\n");
  return 0;
}

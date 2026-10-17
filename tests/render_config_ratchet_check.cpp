// The ratchet on AGENTS.md 2.15: geometry takes its settings as arguments and never reads
// the config itself.
//
// Why a ratchet and not a pass/fail. The rule is broken in 106 places today, in files nobody
// is going to rewrite this week, and a test that simply fails would be red forever -- which
// teaches the next person that red is normal here. So the number is the baseline: it is
// allowed to go DOWN and every increase is a new violation, which is the one direction that
// means a decision was made inside a paint function instead of being passed to it.
//
// This is the rule that made the same bug repeat for a week. A knob read inside paint is a
// knob whose effect depends on WHEN in the frame it is read -- paint runs per tile, so the
// first tile reads a config the rest of the frame has not caught up with, and a value changed
// mid-frame lands on some cards and not others. That is not a style complaint; it is the
// mechanism behind "the caption is wrong on the second card only".
//
// Counted, not listed: a list is a to-do that rots. The count is a number that has to be
// defended, and defending it is the work.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

// The count at the moment the ratchet was set. Lower it when you migrate a file; never raise
// it. If a change genuinely needs a config read in render/, the argument is in the rule, not
// here -- change the rule and the reason with it, in the same commit.
constexpr int kBaseline = 111;

// A read is `cfg::something`. A NAMESPACE-qualified mention is not a read and must not be
// counted: the rule is about reading a setting, and matching the token alone would also match
// the comment that explains why the file does not.
const std::regex kRead(R"(cfg::[a-zA-Z_][a-zA-Z0-9_]*)");

int countReads(const std::string &src) {
  // Comments and string literals are prose about the rule, not the rule. Stripped first so a
  // long explanation of why this file reads nothing does not read as a violation.
  static const std::regex kComment(R"(//[^\n]*|/\*[\s\S]*?\*/)");
  static const std::regex kString(R"("(?:[^"\\]|\\.)*")");
  std::string s = std::regex_replace(src, kComment, " ");
  s = std::regex_replace(s, kString, " \"\" ");
  int n = 0;
  for (auto it = std::sregex_iterator(s.begin(), s.end(), kRead); it != std::sregex_iterator(); ++it)
    ++n;
  return n;
}

int countFile(const std::filesystem::path &p) {
  std::ifstream f(p);
  if (!f)
    return 0;
  return countReads(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()));
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1 ? argv[1] : "src";
  const std::filesystem::path render = root / "render";

  if (!std::filesystem::exists(render)) {
    std::fprintf(stderr, "no %s -- pass the source root\n", render.c_str());
    return 2;
  }

  std::vector<std::pair<std::string, int>> per;
  int total = 0;
  for (const auto &e : std::filesystem::directory_iterator(render)) {
    if (!e.is_regular_file())
      continue;
    const auto ext = e.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp")
      continue;
    const int n = countFile(e.path());
    total += n;
    if (n)
      per.emplace_back(e.path().filename().string(), n);
  }
  std::sort(per.begin(), per.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

  if (total > kBaseline) {
    std::fprintf(stderr, "  %s: %d config reads in src/render/, the baseline is %d (+%d)\n",
                 "render config ratchet", total, kBaseline, total - kBaseline);
    for (const auto &[f, n] : per)
      if (n >= 5)
        std::fprintf(stderr, "    %-22s %d\n", f.c_str(), n);
    std::fprintf(stderr,
                 "  a read inside paint lands on the tiles painted before it and not the ones\n"
                 "  after. Pass the value in (AGENTS.md 2.15) and lower kBaseline in\n"
                 "  tests/render_config_ratchet_check.cpp by what you removed.\n");
    return 1;
  }

  std::printf("render config ratchet: %d reads, baseline %d, headroom %d\n", total, kBaseline,
              kBaseline - total);
  return 0;
}
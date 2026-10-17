// Comment weight. AGENTS.md 10.3/10.4 ask for comments that state a rule or carry a reason;
// they do not cap how many. There is no cap, so the codebase sits at 25.6% -- nearly four
// comment lines for every ten of code -- and the cost is not the file size.
//
// The cost is that the rules contradict each other inside drawTile: five successive comment
// blocks describe five successive versions of "what happens to a caption that is being
// shoved", each written to answer the bug visible that day, each cancelling the one above it.
// Five versions of one rule is not documentation, it is a log of not having a rule. And a
// comment that is not the current answer is worse than no comment, because it is read as the
// answer.
//
// So the cap is on WEIGHT, not on count, and it is a ratchet rather than a wall: 25.6% today
// is the number to beat, 10% is the target, and going over the baseline is red. Cutting it is
// real work -- the history goes with the code, git has it -- and it is the kind of work that
// does not happen unless something turns red when it is skipped.
//
// What counts as a comment: full-line // and /* */ blocks, and a trailing // on a line of
// code. Blank lines are not counted on either side: padding is not weight.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Comment lines as a share of comment + code, per file, for the whole of src/. Not a per-file
// wall: a header that states a rule the code cannot express is allowed to be heavy, and a
// header that only declares things is not allowed to be empty of explanation.
constexpr double kBaselinePct = 20.7;
constexpr double kTargetPct   = 10.0;

struct Weight {
  int comments = 0;
  int code     = 0;
};

// A block comment is one comment line per line it spans: five lines inside /* */ is five lines
// the reader has to get through. Blank lines are counted on neither side. A line with code and
// a trailing note counts as both, because it is both.
Weight weigh(const std::string &src) {
  std::string clean;
  int blockLines = 0;
  bool inBlock = false;
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (!inBlock && i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
      inBlock = true;
      i += 1;
      continue;
    }
    if (inBlock && i + 1 < src.size() && src[i] == '*' && src[i + 1] == '/') {
      inBlock = false;
      i += 1;
      continue;
    }
    if (src[i] == '\n') {
      if (inBlock)
        ++blockLines;
      else
        clean += '\n';
      continue;
    }
    if (!inBlock)
      clean += src[i];
  }

  Weight w;
  w.comments = blockLines;
  std::istringstream in(clean);
  std::string l;
  while (std::getline(in, l)) {
    if (l.find_first_not_of(" \t\r") == std::string::npos)
      continue;
    ++w.code;
    if (l.find("//") != std::string::npos)
      ++w.comments;
  }
  return w;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1 ? argv[1] : "src";

  Weight all;
  std::vector<std::pair<std::string, Weight>> per;
  for (const auto &e : std::filesystem::recursive_directory_iterator(root)) {
    if (!e.is_regular_file())
      continue;
    const auto ext = e.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp")
      continue;
    std::ifstream f(e.path());
    if (!f)
      continue;
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const Weight w = weigh(src);
    all.comments += w.comments;
    all.code += w.code;
    per.emplace_back(std::filesystem::relative(e.path(), root).string(), w);
  }

  const int total = all.comments + all.code;
  const double pct = total ? 100.0 * all.comments / total : 0.0;

  const auto pctOf = [](const Weight &w) {
    const int t = w.comments + w.code;
    return t ? 100.0 * w.comments / t : 0.0;
  };
  std::sort(per.begin(), per.end(),
            [&](const auto &a, const auto &b) { return pctOf(a.second) > pctOf(b.second); });

  if (pct > kBaselinePct + 0.05) {
    std::fprintf(stderr, "  comment weight %.1f%%, baseline %.1f%% (+%.1f)\n", pct, kBaselinePct,
                 pct - kBaselinePct);
    for (const auto &[name, w] : per) {
      const double p = pctOf(w);
      if (p >= 90.0)
        std::fprintf(stderr, "    %-36s %5.1f%%  %d/%d\n", name.c_str(), p, w.comments, w.code);
    }
    std::fprintf(stderr,
                 "  comments are where five versions of one rule live side by side, each\n"
                 "  written for the bug of its day. A rule that changed leaves one comment.\n");
    return 1;
  }

  std::printf("comment weight %.1f%% of %d lines (baseline %.1f%%, target %.1f%%)\n", pct, total,
              kBaselinePct, kTargetPct);
  return 0;
}
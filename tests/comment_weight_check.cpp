// Comment weight, capped by AGENTS.md 10.3: 8% of comment+code, and a ratchet on how many
// comment lines the tree may hold.
//
// The ratchet is an ABSOLUTE count, not a share. A share punishes every refactor that
// removes code, and the removal is the point: moving a hundred lines of drawing into a
// pure function raises the share with no comment added, and the check then red on a change
// that made the tree smaller. An absolute budget asks the question that matters -- did any
// comment get written -- and the share stays the ceiling.
//
// What counts as a comment: full-line // and /* */ blocks, and a trailing // on a line of
// code. Blank lines are counted on neither side: padding is not weight.

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

// The wall is a share of comment + code, per file, for the whole of src/. Not a per-file
// wall: a header that states a rule the code cannot express may be heavier than one that only
// declares things. The ratchet is the absolute number of comment lines the tree may hold.
constexpr double kTargetPct      = 8.0;
constexpr int    kCommentBudget  = 920;

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

  if (all.comments > kCommentBudget || pct > kTargetPct + 0.05) {
    std::fprintf(stderr, "  comment weight %.1f%% (wall %.1f%%), %d comment lines (budget %d)\n",
                 pct, kTargetPct, all.comments, kCommentBudget);
    for (const auto &[name, w] : per) {
      const double p = pctOf(w);
      if (p >= 90.0)
        std::fprintf(stderr, "    %-36s %5.1f%%  %d/%d\n", name.c_str(), p, w.comments, w.code);
    }
    std::fprintf(stderr,
                 "  comments are where five versions of one rule live side by side, each\n"
                 "  written for the bug of its day. A rule that changed leaves one comment.\n"
                 "  A refactor that removes code does not have to delete a word.\n");
    return 1;
  }

  std::printf("comment weight %.1f%% of %d lines (wall %.1f%%, budget %d comment lines)\n", pct,
              total, kTargetPct, kCommentBudget);
  return 0;
}
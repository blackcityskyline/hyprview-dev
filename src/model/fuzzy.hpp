#pragma once

#include <cctype>
#include <cstddef>
#include <string_view>

namespace hyprview::model {

// fzf-style subsequence scoring: the query matches only when its characters appear in order.
// Higher is better, -1 is no match. An empty query matches everything with 0, so the whole
// list stays visible until the user types.
//
// Greedy left-to-right alignment, deliberately: one pass, no table. It cannot find the
// optimal alignment of a repeated character, and that is the ceiling -- swap in a scoring
// matrix if a search ever ranks visibly wrong.
[[nodiscard]] inline bool isWordStart(char prev) {
  const auto c = static_cast<unsigned char>(prev);
  return std::isspace(c) || c == '/' || c == '.' || c == '-' || c == '_' || c == ':' ||
         c == '(' || c == '[' || c == '|';
}

[[nodiscard]] inline int fuzzyScore(std::string_view needle, std::string_view hay) {
  if (needle.empty())
    return 0;
  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  constexpr std::size_t kNone = std::string_view::npos;
  std::size_t hi = 0, last = kNone;
  int score = 0, matched = 0;
  for (const char qc : needle) {
    const char want = lower(qc);
    while (hi < hay.size() && lower(hay[hi]) != want)
      ++hi;
    if (hi >= hay.size())
      return -1;
    score += 1;
    if (hi == 0)
      ++score;
    else if (isWordStart(hay[hi - 1]) ||
             (std::islower(static_cast<unsigned char>(hay[hi - 1])) &&
              std::isupper(static_cast<unsigned char>(hay[hi]))))
      score += 4;
    if (last != kNone && hi == last + 1)
      score += 3;
    last = hi;
    ++hi;
    ++matched;
  }
  // An anchored first match outranks the same letters found mid-word.
  if (matched > 0 && lower(hay.front()) == lower(needle.front()))
    score += 8;
  return score;
}

// The best of several fields: a title hit and a class hit are the same row, so the row keeps
// its strongest reason to be on screen.
[[nodiscard]] inline int fuzzyScoreAny(std::string_view needle,
                                       const std::string_view *fields, std::size_t n) {
  int best = -1;
  for (std::size_t i = 0; i < n; ++i) {
    const int s = fuzzyScore(needle, fields[i]);
    if (s > best)
      best = s;
  }
  return best;
}

} // namespace hyprview::model

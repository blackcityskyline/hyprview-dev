#pragma once

#include <cctype>
#include <cstddef>
#include <string_view>

namespace hyprview::model {

[[nodiscard]] inline bool isWordStart(char prev) {
  const auto c = static_cast<unsigned char>(prev);
  return std::isspace(c) || c == '/' || c == '.' || c == '-' || c == '_' || c == ':' ||
         c == '(' || c == '[' || c == '|';
}

inline int substringBonus(std::string_view needle, std::string_view hay) {
  if (needle.size() < 3)
    return 0;
  for (std::size_t start = 0; start + needle.size() <= hay.size(); ++start) {
    bool same = true;
    for (std::size_t i = 0; i < needle.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(hay[start + i])) !=
          std::tolower(static_cast<unsigned char>(needle[i]))) {
        same = false;
        break;
      }
    if (!same)
      continue;
    return 6 + (start == 0 || isWordStart(hay[start - 1]) ? 6 : 0);
  }
  return 0;
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
  if (matched > 0 && lower(hay.front()) == lower(needle.front()))
    score += 8;
  return score + substringBonus(needle, hay);
}

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

}

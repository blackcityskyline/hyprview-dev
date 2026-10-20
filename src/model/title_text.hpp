#pragma once

#include <cstddef>
#include <string>

namespace hyprview::model {

inline constexpr std::size_t kTitleLimit = 50;

inline std::size_t titleChars(const std::string &s) {
  std::size_t n = 0;
  for (const char c : s)
    n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
  return n;
}

inline std::string truncateTitle(const std::string &s, std::size_t max = kTitleLimit) {
  if (max <= 3 || titleChars(s) <= max)
    return s;
  std::size_t chars = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
      continue;
    if (++chars == max - 2)
      return s.substr(0, i) + "...";
  }
  return s;
}

}

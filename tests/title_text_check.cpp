// Self-check for the caption limit (src/model/title_text.hpp).
//
//   g++ -std=c++23 -Wall -Wextra -I src tests/title_text_check.cpp -o /tmp/opencode/tt
//       && /tmp/opencode/tt
//
// The caption is a name, not a page: 50 characters, the three dots included. Two
// properties are asserted because both were the obvious way to get it wrong. The
// limit counts CHARACTERS -- a byte count cuts a Cyrillic title at half a letter,
// which renders as a replacement box and reads as a font bug. And the dots are
// inside the limit, not on top of it: a "50 + ..." caption is 53 characters wide
// and still runs past the card the limit exists to keep it inside.

#include <cstdio>
#include <string>

#include "model/title_text.hpp"

using namespace hyprview;

namespace {

int g_failed = 0;

void want(bool ok, const char *what) {
  if (!ok) {
    ++g_failed;
    std::printf("FAIL %s\n", what);
  }
}

// Every character is one code point here: the count is what the limit is about.
std::size_t countChars(const std::string &s) { return model::titleChars(s); }

} // namespace

int main() {
  using model::truncateTitle;

  want(truncateTitle("") == "", "empty stays empty");
  want(truncateTitle("Add-ons Manager") == "Add-ons Manager", "a short title is untouched");

  const std::string exact(50, 'a');
  want(truncateTitle(exact) == exact, "exactly 50 characters is not truncated");

  const std::string over(51, 'a');
  const auto cut = truncateTitle(over);
  want(countChars(cut) == 50, "a 51-character title comes out at 50");
  want(cut == std::string(47, 'a') + "...", "47 characters then three dots");

  // Cyrillic is two bytes per letter: a byte-count cut lands inside a letter.
  std::string cyr;
  for (int i = 0; i < 60; ++i)
    cyr += "ж"; // 2 bytes each
  const auto cyrCut = truncateTitle(cyr);
  want(countChars(cyrCut) == 50, "60 Cyrillic letters come out at 50");
  want(cyrCut.size() == 47 * 2 + 3, "the cut is on a character boundary");
  want(cyrCut.ends_with("..."), "and it ends in three dots");
  want((static_cast<unsigned char>(cyrCut[47 * 2 - 2]) & 0xC0) != 0x80,
       "the last kept letter starts on a byte boundary");

  // A 4-byte character must survive the same way.
  std::string emoji;
  for (int i = 0; i < 55; ++i)
    emoji += "\xF0\x9F\x98\x80"; // U+1F600
  const auto emojiCut = truncateTitle(emoji);
  want(countChars(emojiCut) == 50, "55 four-byte characters come out at 50");
  want(emojiCut.size() == 47 * 4 + 3, "no four-byte character is split");

  want(truncateTitle(over, 3) == over, "a limit below the dots is not applied");
  want(truncateTitle(over, 10) == std::string(7, 'a') + "...", "a smaller limit keeps its dots");

  if (g_failed) {
    std::printf("%d check(s) failed\n", g_failed);
    return 1;
  }
  std::puts("title_text: limit 50 characters, dots included, cut on character boundaries");
  return 0;
}

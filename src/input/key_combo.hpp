#pragma once

// Key-combo parsing: "shift+tab, super+q" against a pressed keycode and a modifier mask.
//
// No Hyprland and no config reads. The modifier bits are declared here rather than pulled
// from Hyprland's eKeyboardModifiers so this compiles under ctest; keys.cpp static_asserts
// them against the real enum, so a Hyprland change that moved a bit fails the build here
// instead of silently rejecting every combo using it.
//
// The rule this file exists to hold: EXTRA held modifiers fail the match. A bare "tab"
// must not swallow shift+tab, or the user's own toggle bind stops firing. That is one line
// at the end of comboMatches and it is the reason this is not `heldMods == need`.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gloview::input {

inline constexpr uint32_t kModShift = 1U << 0;
inline constexpr uint32_t kModCtrl  = 1U << 2;
inline constexpr uint32_t kModAlt   = 1U << 3;
inline constexpr uint32_t kModMeta  = 1U << 6;

inline std::vector<std::string> keyTokens(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  for (const char c : s) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else
      cur.push_back(c);
  }
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

inline const std::vector<int> &keyNameToCodes(std::string t) {
  static const std::vector<int> NONE;
  for (auto &c : t)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const std::unordered_map<std::string, std::vector<int>> M = {
      {"0", {11}},
      {"1", {2}},
      {"2", {3}},
      {"3", {4}},
      {"4", {5}},
      {"5", {6}},
      {"6", {7}},
      {"7", {8}},
      {"8", {9}},
      {"9", {10}},
      {"grave", {41}},
      {"tilde", {41}},
      {"`", {41}},
      {"esc", {1}},
      {"escape", {1}},
      {"tab", {15}},
      {"space", {57}},
      {"enter", {28, 96}},
      {"return", {28, 96}},
      {"kpenter", {96}},
      {"backspace", {14}},
      {"delete", {111}},
      {"del", {111}},
      {"insert", {110}},
      {"left", {105}},
      {"right", {106}},
      {"up", {103}},
      {"down", {108}},
      {"home", {102}},
      {"end", {107}},
      {"pageup", {104}},
      {"pagedown", {109}},
      {"shift", {42, 54}},
      {"lshift", {42}},
      {"rshift", {54}},
      {"ctrl", {29, 97}},
      {"control", {29, 97}},
      {"lctrl", {29}},
      {"rctrl", {97}},
      {"alt", {56, 100}},
      {"lalt", {56}},
      {"ralt", {100}},
      {"super", {125, 126}},
      {"meta", {125, 126}},
      {"win", {125, 126}},
      {"f1", {59}},
      {"f2", {60}},
      {"f3", {61}},
      {"f4", {62}},
      {"f5", {63}},
      {"f6", {64}},
      {"f7", {65}},
      {"f8", {66}},
      {"f9", {67}},
      {"f10", {68}},
      {"f11", {87}},
      {"f12", {88}},
      {"a", {30}},
      {"b", {48}},
      {"c", {46}},
      {"d", {32}},
      {"e", {18}},
      {"f", {33}},
      {"g", {34}},
      {"h", {35}},
      {"i", {23}},
      {"j", {36}},
      {"k", {37}},
      {"l", {38}},
      {"m", {50}},
      {"n", {49}},
      {"o", {24}},
      {"p", {25}},
      {"q", {16}},
      {"r", {19}},
      {"s", {31}},
      {"t", {20}},
      {"u", {22}},
      {"v", {47}},
      {"w", {17}},
      {"x", {45}},
      {"y", {21}},
      {"z", {44}},
  };
  const auto it = M.find(t);
  return it != M.end() ? it->second : NONE;
}

inline uint32_t modNameToBit(std::string t) {
  for (auto &c : t)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t == "shift")
    return kModShift;
  if (t == "ctrl" || t == "control")
    return kModCtrl;
  if (t == "alt")
    return kModAlt;
  if (t == "super" || t == "meta" || t == "win")
    return kModMeta;
  return 0;
}

// Extra held modifiers fail the match: a bare "tab" must not swallow
// shift+tab or the user's own toggle bind.
inline bool comboMatches(int keycode, uint32_t heldMods, std::string token) {
  constexpr uint32_t STRICTMODS =
      kModShift | kModCtrl | kModAlt | kModMeta;

  uint32_t need = 0;
  size_t pos;
  while ((pos = token.find('+')) != std::string::npos) {
    const uint32_t bit = modNameToBit(token.substr(0, pos));
    if (bit == 0)
      return false;
    need |= bit;
    token.erase(0, pos + 1);
  }

  bool codeHit = false;
  for (const int c : keyNameToCodes(token))
    if (c == keycode) {
      codeHit = true;
      break;
    }
  if (!codeHit)
    return false;
  if ((heldMods & need) != need)
    return false;
  return (heldMods & STRICTMODS & ~need) == 0;
}

// True if any token in the comma-separated list matches. Used for single-key binds.
[[nodiscard]] inline bool keyMatches(int keycode, uint32_t heldMods,
                                     const std::string &combo) {
  for (const auto &tok : keyTokens(combo))
    if (comboMatches(keycode, heldMods, tok))
      return true;
  return false;
}

// Which token matched, or -1. A multi-position bind needs to know WHICH position, which is
// why this is not keyMatches returning bool.
[[nodiscard]] inline int keyIndex(int keycode, uint32_t heldMods,
                                  const std::string &combo) {
  int idx = 0;
  for (const auto &tok : keyTokens(combo)) {
    if (comboMatches(keycode, heldMods, tok))
      return idx;
    ++idx;
  }
  return -1;
}

} // namespace gloview::input

// The order of the passes in paint() and the order AGENTS.md 3.4 documents.
//
// The z-order is the rule: what sits ON TOP of content is drawn AFTER it. A pass
// moved one line up is a card under its own caption, a strip window over its
// close button, a cursor under a menu -- and it looks like "the overview glitches
// sometimes", not like an ordering bug.
//
// The two are compared, not duplicated: the document says what the order MEANS,
// the code says what it IS, and this check says they are the same order. Change
// one and the check goes red until the other follows.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The body of one function: from its signature to the brace that closes it.
std::string bodyOf(const std::string &src, const std::string &signature) {
  const auto at = src.find(signature);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  bool seen = false;
  for (std::size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
      seen = true;
    } else if (src[i] == '}') {
      --depth;
      if (seen && depth == 0)
        return src.substr(at, i - at + 1);
    }
  }
  return {};
}

// The fenced block that documents the order.
std::string docBlock(const std::string &md) {
  const auto at = md.find("### paint()");
  if (at == std::string::npos)
    return {};
  const auto fence = md.find("```", at);
  if (fence == std::string::npos)
    return {};
  const auto from = md.find('\n', fence);
  const auto to = md.find("```", from);
  if (from == std::string::npos || to == std::string::npos)
    return {};
  return md.substr(from, to - from);
}

const char *kPasses[] = {
    "renderBackdrop",  "renderCarousel", "renderTiles",   "renderGhosts",
    "renderPulses",    "renderStrip",    "renderStripWindows", "renderStripButtons",
    "renderSwapFX",    "renderKillZone", "renderDragTile", "renderDragWindow",
    "renderAboveLayers", "renderCursorOnTop",
};

// Every pass call in the order the code makes them.
std::vector<std::string> callsInOrder(const std::string &body) {
  std::vector<std::pair<std::size_t, std::string>> hits;
  for (const char *name : kPasses) {
    const std::string needle = std::string(name) + "(";
    std::size_t pos = 0;
    while ((pos = body.find(needle, pos)) != std::string::npos) {
      hits.emplace_back(pos, name);
      pos += needle.size();
    }
  }
  std::sort(hits.begin(), hits.end());
  std::vector<std::string> out;
  for (const auto &[pos, name] : hits)
    out.push_back(name);
  return out;
}

// What the document's slot means, as the set of passes it covers.
std::vector<std::vector<std::string>> docSlots(const std::string &block) {
  std::vector<std::vector<std::string>> slots;
  std::stringstream in(block);
  std::string line;
  std::string joined;
  while (std::getline(in, line))
    joined += line + " ";
  // The arrow is three UTF-8 bytes: split on a plain marker instead.
  const std::string arrow = "\xE2\x86\x92";
  std::string flat;
  for (std::size_t i = 0; i < joined.size();) {
    if (joined.compare(i, arrow.size(), arrow) == 0) {
      flat += '|';
      i += arrow.size();
    } else {
      flat += joined[i];
      ++i;
    }
  }
  std::string token;
  std::stringstream parts(flat);
  while (std::getline(parts, token, '|')) {
    std::string slot;
    for (const char c : token)
      if (!std::isspace(static_cast<unsigned char>(c)))
        slot += c;
    if (slot.empty())
      continue;
    if (slot == "backdrop") slots.push_back({"renderBackdrop"});
    else if (slot == "tiles/carousel") slots.push_back({"renderTiles", "renderCarousel"});
    else if (slot == "ghosts") slots.push_back({"renderGhosts"});
    else if (slot == "pulses") slots.push_back({"renderPulses"});
    else if (slot == "strip") slots.push_back({"renderStrip"});
    else if (slot == "stripwindows") slots.push_back({"renderStripWindows"});
    else if (slot == "stripbuttons") slots.push_back({"renderStripButtons"});
    else if (slot == "swapfx") slots.push_back({"renderSwapFX"});
    else if (slot == "drag") slots.push_back({"renderKillZone", "renderDragTile", "renderDragWindow"});
    else if (slot == "aboveLayers") slots.push_back({"renderAboveLayers"});
    else if (slot == "cursor") slots.push_back({"renderCursorOnTop"});
    else {
      std::printf("FAIL: the document names a slot this check does not know: %s\n", slot.c_str());
      std::exit(1);
    }
  }
  return slots;
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  const auto painter = slurp(root + "/src/render/painter.cpp");
  const auto agents = slurp(root + "/AGENTS.md");
  if (painter.empty() || agents.empty()) {
    std::printf("FAIL: cannot read the painter or AGENTS.md under %s\n", root.c_str());
    return 1;
  }

  const auto body = bodyOf(painter, "void Overview::paint()");
  if (body.empty()) {
    std::printf("FAIL: paint() not found\n");
    return 1;
  }
  const auto calls = callsInOrder(body);
  const auto slots = docSlots(docBlock(agents));

  if (slots.empty()) {
    std::printf("FAIL: the documented order not found in AGENTS.md\n");
    return 1;
  }

  // Every documented slot is one contiguous run of calls in the code, and the runs
  // cover the code exactly: a pass the document does not mention is a pass whose
  // place in the z-order nobody knows.
  std::size_t at = 0;
  for (const auto &slot : slots) {
    if (at >= calls.size()) {
      std::printf("FAIL: the document lists more slots than the code draws\n");
      return 1;
    }
    const std::size_t from = at;
    while (at < calls.size() &&
           std::find(slot.begin(), slot.end(), calls[at]) != slot.end())
      ++at;
    if (at == from) {
      std::printf("FAIL: the slot is not drawn where the document puts it\n");
      return 1;
    }
  }
  if (at != calls.size()) {
    std::printf("FAIL: %zu pass(es) are drawn and the document does not place them\n",
                calls.size() - at);
    return 1;
  }

  std::printf("paint order: %zu passes, and the document says the same order\n", calls.size());
  return 0;
}

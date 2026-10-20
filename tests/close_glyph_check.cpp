// The close glyph is drawn by ONE rule, on every surface that draws it.
//
// The tile scaled the glyph into its button by the knob; the strip drew it at a
// hardcoded fraction of the texture, so the knob meant nothing there and the
// glyph did not match the button it sat in. The rule moved to the geometry
// header; this keeps every surface on it.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  int bad = 0;

  // Every surface that draws the close button asks the rule.
  for (const char *file : {"src/render/tile_view.cpp", "src/render/strip_view.cpp"}) {
    const auto src = slurp(root + "/" + file);
    if (src.empty()) {
      std::printf("FAIL: cannot read %s\n", file);
      return 1;
    }
    if (src.find("glyphRect(") == std::string::npos) {
      std::printf("FAIL: %s draws the close glyph without the rule -- the knob means "
                  "nothing there\n", file);
      ++bad;
    }
  }

  // And the glyph's size is never a literal of its own: the texture's size may
  // only be handed to the rule.
  const std::regex literal(R"(closeGlyph->m_size\.[xy]\s*\*)");
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root + "/src/render")) {
    if (!entry.is_regular_file())
      continue;
    const auto ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp")
      continue;
    const auto src = slurp(entry.path().string());
    if (std::regex_search(src, literal)) {
      std::printf("FAIL: %s scales the close glyph by a number of its own\n",
                  entry.path().filename().c_str());
      ++bad;
    }
  }

  if (bad)
    return 1;
  std::printf("the close glyph: one rule, and every surface asks it\n");
  return 0;
}

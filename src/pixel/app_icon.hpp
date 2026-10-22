#pragma once

// Where a window class's icon lives on disk; filesystem only, so the lookup has a test.
// XDG precedence: an entry in ~/.local/share/applications beats the system one, and its Icon=
// may be an absolute path into the user's own icon set.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace hyprview::pixel::app_icon {

inline std::string lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::vector<std::string> splitDirs(const std::string &list) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : list) {
    if (c == ':' || c == ';') {
      if (!cur.empty())
        out.push_back(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

inline std::string home() {
  const char *h = std::getenv("HOME");
  return h ? h : "";
}

inline std::string dataHome() {
  const char *x = std::getenv("XDG_DATA_HOME");
  return x && *x ? x : home() + "/.local/share";
}

inline std::vector<std::string> dataDirs() {
  const char *x = std::getenv("XDG_DATA_DIRS");
  return splitDirs(x && *x ? x : "/usr/local/share:/usr/share");
}

struct Entry {
  std::string icon, wmClass;
};

inline Entry readEntry(const std::filesystem::path &file) {
  std::ifstream in(file);
  Entry e;
  std::string line;
  bool wanted = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty() && line.front() == '[') {
      wanted = line == "[Desktop Entry]";
      continue;
    }
    if (!wanted)
      continue;
    if (line.rfind("Icon=", 0) == 0)
      e.icon = line.substr(5);
    else if (line.rfind("StartupWMClass=", 0) == 0)
      e.wmClass = line.substr(15);
  }
  return e;
}

// Largest first: a badge is up to 60 logical pixels, so a 16px file is the one that looks bad.
inline int sizeOf(const std::string &dir) {
  int n = 0;
  for (char c : dir) {
    if (c < '0' || c > '9')
      return n;
    n = n * 10 + (c - '0');
  }
  return n;
}

// The theme directories of one root, biggest matching file: `<root>/<theme>/<size>/apps/<name>`.
inline std::string scanRoot(const std::filesystem::path &root, const std::string &name,
                            const std::string &ext) {
  std::error_code ec;
  std::string best;
  int bestSize = -1;
  for (auto theme = std::filesystem::directory_iterator(root, ec);
       !ec && theme != std::filesystem::directory_iterator(); theme.increment(ec)) {
    if (!theme->is_directory(ec))
      continue;
    for (auto size = std::filesystem::directory_iterator(theme->path(), ec);
         !ec && size != std::filesystem::directory_iterator(); size.increment(ec)) {
      if (!size->is_directory(ec) || size->path().filename() == "apps")
        continue;
      const auto file = size->path() / "apps" / (name + ext);
      if (!std::filesystem::exists(file, ec))
        continue;
      const int px = sizeOf(size->path().filename().string());
      if (px > bestSize) {
        bestSize = px;
        best = file.string();
      }
    }
  }
  return best;
}

// hicolor first and cheap: that is where a name from Icon= is installed to.
inline std::string findByRoots(const std::string &name, const std::string &ext) {
  std::vector<std::string> roots{dataHome() + "/icons", home() + "/.icons"};
  for (const auto &d : dataDirs())
    roots.push_back(d + "/icons");
  for (const auto &root : roots)
    for (const char *size : {"256x256", "128x128", "64x64", "48x48", "32x32", "16x16"}) {
      const auto file = std::filesystem::path(root) / "hicolor" / size / "apps" / (name + ext);
      if (std::filesystem::exists(file))
        return file.string();
    }
  for (const auto &root : roots) {
    const auto file = scanRoot(root, name, ext);
    if (!file.empty())
      return file;
  }
  return {};
}

inline std::string iconFile(const std::string &name) {
  if (name.empty())
    return {};
  if (name.front() == '/')
    return std::filesystem::exists(name) ? name : std::string{};
  if (const auto png = findByRoots(name, ".png"); !png.empty())
    return png;
  for (const auto &d : dataDirs())
    if (std::filesystem::exists(std::filesystem::path(d) / "pixmaps" / (name + ".png")))
      return (std::filesystem::path(d) / "pixmaps" / (name + ".png")).string();
  return findByRoots(name, ".svg"); // rasterised by the caller; absent librsvg means no icon
}

// StartupWMClass is the entry's own statement about which window it is; a file stem is a
// convention. Within one directory the statement wins, and the directory order is the answer.
inline std::string appIconFile(const std::string &windowClass) {
  if (windowClass.empty())
    return {};
  const std::string want = lower(windowClass);
  std::vector<std::string> dirs{dataHome() + "/applications"};
  for (const auto &d : dataDirs())
    dirs.push_back(d + "/applications");
  for (const auto &dir : dirs) {
    std::error_code ec;
    std::string byClass, byStem;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || it->path().extension() != ".desktop")
        continue;
      const auto e = readEntry(it->path());
      if (e.icon.empty())
        continue;
      if (!e.wmClass.empty() && lower(e.wmClass) == want) {
        byClass = e.icon;
        break;
      }
      if (byStem.empty() && lower(it->path().stem().string()) == want)
        byStem = e.icon;
    }
    if (!byClass.empty())
      return iconFile(byClass);
    if (!byStem.empty())
      return iconFile(byStem);
  }
  return {};
}

} // namespace hyprview::pixel::app_icon

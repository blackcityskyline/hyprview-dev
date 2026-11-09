#pragma once

// The fields a .desktop entry declares, filtered to entries a menu may show. Filesystem only.

#include "app_icon.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hyprview::pixel::desktop_entry {

using app_icon::lower;

struct Entry {
  std::string id;
  std::string name;
  std::string genericName;
  std::string exec;
  std::string icon;
  std::string wmClass;
  bool        terminal = false;
  bool        noDisplay = false;
  bool        hidden = false;
};

inline std::string trim(std::string_view s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
    ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    --e;
  return std::string(s.substr(b, e - b));
}

inline bool truthy(std::string_view value) {
  const auto v = lower(trim(value));
  return v == "true" || v == "1";
}

inline std::string sanitizeExec(std::string_view exec) {
  std::string out;
  out.reserve(exec.size());
  for (std::size_t i = 0; i < exec.size(); ++i) {
    if (exec[i] != '%') {
      out += exec[i];
      continue;
    }
    if (i + 1 >= exec.size())
      break;
    if (exec[++i] == '%')
      out += '%';
  }
  return trim(out);
}

inline std::optional<Entry> parse(std::string_view text, std::string id) {
  Entry entry;
  entry.id = std::move(id);
  bool inGroup = false;

  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    auto line = trim(text.substr(start, end == std::string_view::npos ? text.size() - start
                                                                     : end - start));
    start = end == std::string_view::npos ? text.size() + 1 : end + 1;
    if (line.empty() || line.front() == '#')
      continue;
    if (line.front() == '[') {
      inGroup = line == "[Desktop Entry]";
      continue;
    }
    if (!inGroup)
      continue;

    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const auto rawKey = trim(line.substr(0, eq));
    if (rawKey.find('[') != std::string::npos)
      continue;
    const auto key = rawKey;
    const auto value = trim(line.substr(eq + 1));
    if (key == "Name")
      entry.name = value;
    else if (key == "GenericName")
      entry.genericName = value;
    else if (key == "Exec")
      entry.exec = sanitizeExec(value);
    else if (key == "Icon")
      entry.icon = value;
    else if (key == "StartupWMClass")
      entry.wmClass = value;
    else if (key == "Terminal")
      entry.terminal = truthy(value);
    else if (key == "NoDisplay")
      entry.noDisplay = truthy(value);
    else if (key == "Hidden")
      entry.hidden = truthy(value);
  }

  if (entry.name.empty() || entry.exec.empty() || entry.noDisplay || entry.hidden)
    return std::nullopt;
  return entry;
}

inline std::optional<Entry> readFile(const std::filesystem::path &file) {
  std::ifstream in(file);
  if (!in)
    return std::nullopt;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return parse(text, file.stem().string());
}

// XDG order: a user entry overrides the system entry with the same id, and is not shown twice.
inline std::vector<Entry> loadFromDirs(const std::vector<std::string> &dirs) {
  std::vector<Entry> out;
  std::unordered_set<std::string> seen;
  for (const auto &dir : dirs) {
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || it->path().extension() != ".desktop")
        continue;
      const auto id = it->path().stem().string();
      if (!seen.insert(id).second)
        continue;
      if (auto entry = readFile(it->path()))
        out.push_back(std::move(*entry));
    }
  }
  std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
    const auto an = lower(a.name), bn = lower(b.name);
    return an == bn ? a.id < b.id : an < bn;
  });
  return out;
}

inline std::vector<Entry> load() {
  std::vector<std::string> dirs{app_icon::dataHome() + "/applications"};
  for (const auto &dir : app_icon::dataDirs())
    dirs.push_back(dir + "/applications");
  return loadFromDirs(dirs);
}

} // namespace hyprview::pixel::desktop_entry

#pragma once

// Where a window class's icon lives on disk; the XDG order applies, absolute Icon= values too.

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
  std::string icon, wmClass, exec;
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
    else if (line.rfind("Exec=", 0) == 0)
      e.exec = line.substr(5);
  }
  return e;
}

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

inline std::string webAppHost(const std::string &cls) {
  static const char *const kEngines[] = {"chrome-", "chromium-", "brave-", "msedge-",
                                         "vivaldi-", "google-chrome-"};
  const std::string s = lower(cls);
  for (const char *engine : kEngines) {
    const std::string pre{engine};
    if (s.rfind(pre, 0) != 0)
      continue;
    const auto rest = s.substr(pre.size());
    const auto sep = rest.find("__");
    if (sep == std::string::npos || sep == 0)
      return {};
    std::string host = rest.substr(0, sep);
    if (host.find('_') != std::string::npos)
      return {};
    if (host.rfind("www.", 0) == 0)
      host = host.substr(4);
    return host;
  }
  return {};
}

inline std::string launchedSite(const std::string &exec) {
  const auto tokenAfter = [&](std::size_t from) {
    while (from < exec.size() && std::isspace(static_cast<unsigned char>(exec[from])))
      ++from;
    if (from < exec.size() && (exec[from] == '"' || exec[from] == '\''))
      ++from;
    std::size_t to = from;
    while (to < exec.size() && !std::isspace(static_cast<unsigned char>(exec[to])) &&
           exec[to] != '"' && exec[to] != '\'')
      ++to;
    return exec.substr(from, to - from);
  };
  std::string target;
  const std::string webapp = "omarchy-launch-webapp";
  if (const auto at = exec.find(webapp); at != std::string::npos)
    target = tokenAfter(at + webapp.size());
  if (target.empty())
    if (const auto at = exec.find("--app="); at != std::string::npos)
      target = tokenAfter(at + 6);
  const auto scheme = target.find("://");
  if (scheme == std::string::npos)
    return {};
  std::string host = target.substr(scheme + 3);
  host = host.substr(0, host.find_first_of("/:?#"));
  if (host.rfind("www.", 0) == 0)
    host = host.substr(4);
  return host;
}

inline std::string siteName(const std::string &host) {
  std::vector<std::string> labels;
  std::string cur;
  for (const char c : host) {
    if (c == '.') {
      labels.push_back(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  labels.push_back(cur);
  if (!labels.empty())
    labels.pop_back();
  static const char *const kSecondLevel[] = {"co", "com", "net", "org", "gov", "edu", "ac"};
  while (labels.size() > 1) {
    const auto &back = labels.back();
    bool second = false;
    for (const char *s : kSecondLevel)
      second = second || lower(back) == s;
    if (!second)
      break;
    labels.pop_back();
  }
  return labels.empty() ? std::string{} : labels.back();
}

inline std::vector<std::string> appKeys(const std::string &cls) {
  std::vector<std::string> keys;
  if (cls.empty())
    return keys;
  const auto add = [&](const std::string &name) {
    if (!name.empty() && std::find(keys.begin(), keys.end(), name) == keys.end())
      keys.push_back(name);
  };
  keys.push_back(cls);
  if (const auto host = webAppHost(cls); !host.empty()) {
    add(siteName(host));
    return keys;
  }
  std::vector<std::string> parts;
  std::string cur;
  for (const char c : cls) {
    if (c == '.') {
      if (!cur.empty())
        parts.push_back(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  if (!cur.empty())
    parts.push_back(cur);
  std::string last = cls;
  if (parts.size() >= 3) {
    last = parts.back();
    static const char *const kGeneric[] = {"app", "client", "desktop", "gui", "ui"};
    for (const char *g : kGeneric)
      if (lower(last) == g)
        last = parts[parts.size() - 2];
    add(last);
  }
  static const char *const kSuffixes[] = {"-app", "-client", "-desktop", "-gui", "-bin",
                                          "_app", "_client", "_desktop", "_gui", "_bin"};
  for (const char *suffix : kSuffixes) {
    const std::string s{suffix};
    if (last.size() > s.size() && lower(last).rfind(s) == last.size() - s.size()) {
      last = last.substr(0, last.size() - s.size());
      break;
    }
  }
  add(last);
  return keys;
}

// StartupWMClass is the entry's own statement; a file stem is a convention. The statement wins.
inline std::string appIconFile(const std::string &windowClass) {
  if (windowClass.empty())
    return {};
  const std::string want = lower(windowClass);
  const auto host = webAppHost(windowClass);
  const auto keys = appKeys(windowClass);
  const auto keyedStem = [&](const std::string &id) {
    for (const auto &k : keys)
      if (id == lower(k))
        return true;
    return false;
  };
  const auto keyedClass = [&](const std::string &wmClass) {
    if (wmClass.empty())
      return false;
    for (const auto &k : keys)
      if (lower(wmClass) == lower(k))
        return true;
    return false;
  };
  std::vector<std::string> dirs{dataHome() + "/applications"};
  for (const auto &d : dataDirs())
    dirs.push_back(d + "/applications");
  for (const auto &dir : dirs) {
    std::error_code ec;
    std::string byClass, bySite, byStem, byKeyed;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || it->path().extension() != ".desktop")
        continue;
      const auto e = readEntry(it->path());
      if (e.icon.empty())
        continue;
      if (byClass.empty() && !e.wmClass.empty() && lower(e.wmClass) == want)
        byClass = e.icon;
      if (bySite.empty() && !host.empty() && launchedSite(e.exec) == host)
        bySite = e.icon;
      if (byStem.empty() && keyedStem(lower(it->path().stem().string())))
        byStem = e.icon;
      if (byKeyed.empty() && keyedClass(e.wmClass))
        byKeyed = e.icon;
    }
    if (!byClass.empty())
      return iconFile(byClass);
    if (!bySite.empty())
      return iconFile(bySite);
    if (!byStem.empty())
      return iconFile(byStem);
    if (!byKeyed.empty())
      return iconFile(byKeyed);
  }
  for (const auto &k : keys)
    if (const auto file = iconFile(lower(k)); !file.empty())
      return file;
  return {};
}

} // namespace hyprview::pixel::app_icon

// src/model/ may not name a window.
//
// A tab has no window. Every type the model layer holds is something the run has to be
// able to hold for a TAB as well as for a window: the tile, the ghost of what just left,
// the pulse ring, the flight of a swap, the strip thumbnail, the dragged thing. A field
// typed on PHLWINDOWREF is not "a window field" -- it is a field that will be null for the
// other domain, and every reader then either crashes or skips, with nothing written down
// saying which.
//
// Workspaces are NOT covered by this rule, and the distinction is not arbitrary: a
// workspace is where a window lives, it has no tab equivalent, and StripItem::ws plus
// newWorkspaces() are workspace-shaped by nature rather than by accident. Generalising
// them is a separate job with a separate answer.
//
// The two adapter files are exempt by name and no further: WindowsContentSource is the one
// place allowed to know that a window is a pointer, and IWindowHost is the question it asks
// about one. Widening the exemption to a directory would put the exemption back where the
// problem was.

#include <cassert>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string &p) {
  std::ifstream f(p);
  assert(f && ("cannot open " + p).c_str());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Only real occurrences: skip comment-only lines, so a comment explaining the rule cannot
// hold it in place, and so PHLWINDOWREF mentioned in prose is not counted as a field.
bool namesWindow(const std::string &src, std::string *where) {
  std::istringstream in(src);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    const auto at = line.find("//");
    const auto code = at == std::string::npos ? line : line.substr(0, at);
    if (code.find("PHLWINDOW") == std::string::npos)
      continue;
    // A line that mentions the type only inside a comment is not a use.
    if (code.find_first_not_of(" \t") == std::string::npos)
      continue;
    *where = std::to_string(n) + ": " + code;
    return true;
  }
  return false;
}

bool exempt(const std::string &name) {
  return name == "window_source.hpp" || name == "window_source.cpp" ||
         name == "window_host.hpp";
}

} // namespace

int main(int argc, char **argv) {
  const std::string dir = std::string(argc > 1 ? argv[1] : ".") + "/src/model";
  std::vector<std::string> files;
  if (DIR *d = opendir(dir.c_str())) {
    while (const dirent *e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".hpp") == 0)
        files.push_back(n);
    }
    closedir(d);
  }
  assert(!files.empty() && "no headers found in src/model");

  int failures = 0;
  int checked  = 0;
  for (const auto &n : files) {
    if (exempt(n))
      continue;
    ++checked;
    std::string where;
    if (namesWindow(slurp(dir + "/" + n), &where)) {
      ++failures;
      std::printf("FAIL %s names a window (%s)\n", n.c_str(), where.c_str());
    }
  }

  if (failures) {
    std::printf("model_window_types: %d of %d file(s)\n", failures, checked);
    return 1;
  }
  std::printf("model_window_types: %d file(s) clean\n", checked);
  return 0;
}
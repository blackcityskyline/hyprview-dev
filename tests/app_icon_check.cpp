// Which file a window class's icon comes from.
//
// The lookup is pure filesystem, so it can be pointed at a tree that looks like a user's home:
// a desktop entry in ~/.local/share/applications overrides the system one, and its Icon= may be
// an absolute path. That is the case that was wrong on the screen: an override was ignored and
// the system icon was drawn in its place.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "../src/pixel/app_icon.hpp"

namespace fs = std::filesystem;
using hyprview::pixel::app_icon::appIconFile;

static int failures = 0;

static void want(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

static void put(const fs::path &file, const std::string &text) {
  fs::create_directories(file.parent_path());
  std::FILE *f = std::fopen(file.c_str(), "wb");
  if (!f)
    return;
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
}

int main() {
  const fs::path root = fs::temp_directory_path() / "hyprview_app_icon_check";
  fs::remove_all(root);
  const fs::path home = root / "home";
  const fs::path data = home / ".local/share";
  const fs::path system = root / "system/share";
  const fs::path icons = home / "Pictures/icons";
  setenv("HOME", home.c_str(), 1);
  setenv("XDG_DATA_HOME", data.c_str(), 1);
  setenv("XDG_DATA_DIRS", system.c_str(), 1);

  put(icons / "opencode.png", "png");
  put(icons / "brave.png", "png");
  put(system / "applications/brave-browser.desktop",
      "[Desktop Entry]\nName=Brave\nIcon=brave-desktop\n");
  put(system / "icons/hicolor/128x128/apps/brave-desktop.png", "png");
  put(system / "applications/stemapp.desktop", "[Desktop Entry]\nName=Stem\nIcon=stem-icon\n");
  put(system / "icons/hicolor/64x64/apps/stem-icon.png", "png");
  put(system / "applications/svgapp.desktop", "[Desktop Entry]\nName=Svg\nIcon=svg-icon\n");
  put(system / "icons/hicolor/scalable/apps/svg-icon.svg", "<svg/>");
  put(system / "applications/themeapp.desktop", "[Desktop Entry]\nName=Theme\nIcon=theme-icon\n");
  put(system / "icons/MacTahoe/256x256/apps/theme-icon.png", "png");

  // A user entry: its own StartupWMClass, an Icon= that is a path into the user's pictures.
  put(data / "applications/ai.opencode.desktop.desktop",
      "[Desktop Entry]\nName=OpenCode\nStartupWMClass=ai.opencode.desktop\nIcon=" +
          (icons / "opencode.png").string() + "\n");
  want(appIconFile("ai.opencode.desktop") == (icons / "opencode.png").string(),
       "an absolute Icon= is used as it stands, matched by StartupWMClass");

  want(appIconFile("BRAVE-BROWSER") == (system / "icons/hicolor/128x128/apps/brave-desktop.png").string(),
       "a name is resolved in hicolor, and the class match ignores case");

  // The same desktop file id in the user's directory: the user's copy is the one that counts.
  put(data / "applications/brave-browser.desktop",
      "[Desktop Entry]\nName=Brave\nIcon=" + (icons / "brave.png").string() + "\n");
  want(appIconFile("brave-browser") == (icons / "brave.png").string(),
       "a user override beats the system entry of the same name");

  want(appIconFile("stemapp") == (system / "icons/hicolor/64x64/apps/stem-icon.png").string(),
       "a file stem names a class when the entry says no StartupWMClass");
  want(appIconFile("svgapp") == (system / "icons/hicolor/scalable/apps/svg-icon.svg").string(),
       "an SVG-only icon is returned as the fallback");
  want(appIconFile("themeapp") == (system / "icons/MacTahoe/256x256/apps/theme-icon.png").string(),
       "an icon outside hicolor is found in its theme");
  want(appIconFile("no-such-app").empty(), "a class with no entry resolves to nothing");

  fs::remove_all(root);
  if (failures) {
    std::fprintf(stderr, "app_icon: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("app_icon: desktop entries in XDG order, hicolor, themes, SVG\n");
  return 0;
}

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
#include "../src/pixel/desktop_entry.hpp"

namespace fs = std::filesystem;
using hyprview::pixel::app_icon::appIconFile;
using hyprview::pixel::desktop_entry::loadFromDirs;
using hyprview::pixel::desktop_entry::parse;
using hyprview::pixel::desktop_entry::sanitizeExec;

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
      "[Desktop Entry]\nName=Brave\nExec=brave-browser\nIcon=" + (icons / "brave.png").string() +
          "\n");
  want(appIconFile("brave-browser") == (icons / "brave.png").string(),
       "a user override beats the system entry of the same name");

  want(appIconFile("stemapp") == (system / "icons/hicolor/64x64/apps/stem-icon.png").string(),
       "a file stem names a class when the entry says no StartupWMClass");
  want(appIconFile("svgapp") == (system / "icons/hicolor/scalable/apps/svg-icon.svg").string(),
       "an SVG-only icon is returned as the fallback");
  want(appIconFile("themeapp") == (system / "icons/MacTahoe/256x256/apps/theme-icon.png").string(),
       "an icon outside hicolor is found in its theme");
  want(appIconFile("no-such-app").empty(), "a class with no entry resolves to nothing");

  // .desktop parsing: only the main group counts, hidden entries are not applications, and Exec
  // field codes are not arguments.
  put(data / "applications/editor.desktop",
      "[Desktop Entry]\nName=Editor\nGenericName=Text Editor\nExec=editor %U\nIcon=editor\n"
      "StartupWMClass=Editor\nTerminal=true\n");
  put(system / "applications/hidden.desktop",
      "[Desktop Entry]\nName=Hidden\nExec=hidden\nNoDisplay=true\n");
  put(system / "applications/terminal.desktop",
      "[Desktop Entry]\nName=Terminal\nExec=term\nTerminal=true\n");
  put(system / "applications/section.desktop",
      "[Desktop Entry]\nName=Section\nExec=section\n[Desktop Action new]\nName=Wrong\nExec=wrong\n");
  put(system / "applications/locale.desktop",
      "[Desktop Entry]\nName=Locale\nName[ru]=Локаль\nExec=locale\n");

  const auto editor = parse("[Desktop Entry]\nName=Editor\nGenericName=Text Editor\n"
                            "Exec=editor %U\nIcon=editor\nStartupWMClass=Editor\nTerminal=true\n",
                            "editor");
  want(editor.has_value(), "a main group with Name and Exec is an application");
  want(editor->name == "Editor", "Name is read");
  want(editor->genericName == "Text Editor", "GenericName is read");
  want(editor->exec == "editor", "the Exec field code is dropped");
  want(editor->terminal, "Terminal=true is read");
  want(editor->wmClass == "Editor", "StartupWMClass is read");

  want(!parse("[Desktop Entry]\nName=Hidden\nExec=hidden\nNoDisplay=true\n", "hidden"),
       "NoDisplay=true is not an application");
  want(!parse("[Desktop Entry]\nName=Hidden\nExec=hidden\nHidden=true\n", "hidden"),
       "Hidden=true is not an application");
  want(!parse("[Desktop Entry]\nName=NoExec\n", "noexec"), "an entry without Exec is rejected");
  want(!parse("[Desktop Entry]\nExec=no-name\n", "noname"), "an entry without Name is rejected");
  want(!parse("[Other]\nName=Other\nExec=other\n", "other"),
       "only [Desktop Entry] is the application");
  want(sanitizeExec("app=%f%U%%") == "app=%", "all field codes are dropped and %% is a percent");
  want(sanitizeExec("app %U") == "app", "a trailing field code leaves no argument");

  const auto entries = loadFromDirs({(data / "applications").string(),
                                     (system / "applications").string()});
  const auto find = [&](const std::string &id) -> const hyprview::pixel::desktop_entry::Entry * {
    for (const auto &entry : entries)
      if (entry.id == id)
        return &entry;
    return nullptr;
  };
  want(find("editor") != nullptr, "the parser finds an installed application");
  want(find("editor")->exec == "editor", "with its Exec field already cleaned");
  want(find("hidden") == nullptr, "NoDisplay entries stay out of the launcher list");
  want(find("terminal") != nullptr, "a terminal application is parsed");
  want(find("terminal")->terminal, "and keeps its Terminal flag for the caller");
  const auto *section = find("section");
  want(section != nullptr, "the main group survives a later action group");
  if (section)
    want(section->name == "Section", "a later group cannot overwrite the main group");
  const auto *locale = find("locale");
  want(locale != nullptr, "a translated entry is parsed");
  if (locale)
    want(locale->name == "Locale", "a translated name does not replace the base name");
  const auto *brave = find("brave-browser");
  want(brave != nullptr, "the user override is present");
  if (brave)
    want(brave->icon == (icons / "brave.png").string(),
         "a user entry overrides the system entry with the same id");

  fs::remove_all(root);
  if (failures) {
    std::fprintf(stderr, "app_icon: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("app_icon: desktop entries in XDG order, hicolor, themes, SVG\n");
  return 0;
}

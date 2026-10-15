// Reading a PNG off disk, with no compositor and no GPU.
//
// The failures here are the ordinary ones, not exotic ones. The directory this reads from is
// written by something else while we look at it, so "absent", "half a file" and "not a PNG"
// are all normal states rather than exceptions. Every one of them must land on nullptr with
// nothing printed, nothing thrown and nothing half-built -- because the caller's only correct
// response is the same for all of them: draw the next thing down.

#include <cairo.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../src/pixel/png_read.hpp"

using gloview::pixel::readPngSurface;

namespace {

int failures = 0;

void want(bool ok, bool expected, const char *what) {
  if (ok == expected)
    return;
  ++failures;
  std::printf("FAIL: %s (expected %s)\n", what, expected ? "true" : "false");
}

std::string scratchDir() {
  char tpl[] = "/tmp/opencode/gloview_png_XXXXXX";
  return mkdtemp(tpl) ? std::string(tpl) : std::string("/tmp/opencode");
}

void put(const std::string &path, const std::string &bytes) {
  std::ofstream f(path, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A real PNG, written by cairo so the test does not carry a binary blob it cannot explain.
std::string makePng(const std::string &path, int w, int h) {
  auto *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  auto *cr   = cairo_create(surf);
  cairo_set_source_rgb(cr, 0.2, 0.4, 0.8);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_write_to_png(surf, path.c_str());
  cairo_surface_destroy(surf);
  return path;
}

void drop(cairo_surface_t *s) {
  if (s)
    cairo_surface_destroy(s);
}

} // namespace

int main() {
  const std::string dir = scratchDir();

  // --- a real one --------------------------------------------------------------------------------
  {
    const auto path = makePng(dir + "/ok.png", 48, 32);
    auto *s        = readPngSurface(path);
    want(s != nullptr, true, "a valid PNG decodes");
    if (s) {
      want(cairo_image_surface_get_width(s) == 48, true, "and keeps its width");
      want(cairo_image_surface_get_height(s) == 32, true, "and its height");
      // The point of checking status separately from the pointer: a surface in an ERROR state
      // is non-null and reports width 0, and that is the shape of the bug this guards.
      want(cairo_surface_status(s) == CAIRO_STATUS_SUCCESS, true, "and is not in error");
    }
    drop(s);
  }

  // --- the ordinary failures -----------------------------------------------------------------------
  {
    want(readPngSurface(dir + "/absent.png") == nullptr, true, "a file that is not there");
    want(readPngSurface("") == nullptr, true, "an empty path");
    want(readPngSurface(dir) == nullptr, true, "a directory is not a PNG");

    // Not a PNG at all -- the most likely thing in a directory something else is writing to.
    const auto junk = dir + "/junk.png";
    put(junk, "this is not a png, it is a sentence");
    want(readPngSurface(junk) == nullptr, true, "a file that is not a PNG");

    // Truncated: the first half of a real one. This is what a reader sees mid-write, and it
    // is the case a permissive decoder would return a half-image for.
    const auto whole = makePng(dir + "/whole.png", 64, 64);
    std::ifstream in(whole, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    put(dir + "/half.png", bytes.substr(0, bytes.size() / 2));
    want(readPngSurface(dir + "/half.png") == nullptr, true, "half a PNG");

    // Empty file: zero bytes, which is not the same as a missing one and must not be treated
    // as it -- an empty file is a write that started and gave up.
    put(dir + "/empty.png", "");
    want(readPngSurface(dir + "/empty.png") == nullptr, true, "an empty file");
    std::puts("failures: absent, not-a-png, truncated, empty, a directory -- all nullptr");
  }

  if (failures) {
    std::printf("png_read: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("png_read: decodes a real one, refuses everything else quietly");
  return 0;
}
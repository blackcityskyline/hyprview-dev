// How a tile is drawn: the grid, the run, the dragged card.
//
// The three paths assembled the look by hand, so a field set in one path and
// forgotten in another was a difference between them that nothing said so -- the
// run's cards are contained and dimmed, the grid's are not, and the counter rides
// with the centred card only. The builders are the only way to make a look now,
// and this says what each of them means.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/render/tile_look.hpp"

using hyprview::dragLook;
using hyprview::LRect;
using hyprview::gridLook;
using hyprview::runLook;
using hyprview::TileLook;

namespace {

int failures = 0;

void want(bool got, bool expected, const char *what) {
  if (got == expected)
    return;
  std::printf("FAIL %s: got %d, want %d\n", what, int(got), int(expected));
  ++failures;
}

void near(double got, double expected, const char *what) {
  if (std::fabs(got - expected) < 1e-9)
    return;
  std::printf("FAIL %s: got %g, want %g\n", what, got, expected);
  ++failures;
}

} // namespace

int main() {
  const LRect box{100.0, 200.0, 400.0, 300.0};

  // The grid: a plain tile. Framed, not contained, no tone, no counter.
  {
    const auto l = gridLook(box, 12, /*button=*/true);
    want(l.frame, true, "the grid tile is framed");
    want(l.contain, false, "the grid tile does not contain its content");
    want(l.counter, false, "the grid tile carries no counter");
    want(l.lift, false, "the grid tile is not lifted");
    want(l.button, true, "the button is as asked");
    near(l.tone, 0.0, "the grid tile is not dimmed");
    near(l.round, 12.0, "the radius is the config's");
  }

  // The radius is clamped to the box: a radius past half the shorter side inverts
  // the shape, and a small window's tile is small.
  {
    const auto l = gridLook(LRect{0.0, 0.0, 8.0, 8.0}, 12, false);
    near(l.round, 4.0, "the radius is clamped to half the short side");
  }

  // The run: the centred card is framed and carries the counter; the others are
  // contained and dimmed by their depth.
  {
    const auto centred = runLook(box, 0, 0.0, true, 12);
    want(centred.frame, true, "the centred card is framed");
    want(centred.counter, true, "the counter rides with the centred card");
    want(centred.contain, true, "the run contains its content");
    near(centred.tone, 0.0, "the centred card is not dimmed");

    const auto neighbour = runLook(box, 1, 0.125, true, 12);
    want(neighbour.frame, false, "a neighbour is not framed");
    want(neighbour.counter, false, "and carries no counter");
    near(neighbour.tone, 0.125, "a neighbour is dimmed by its depth");
  }

  // The dragged card: lifted, and the rest as it was.
  {
    const auto l = dragLook(box, 12);
    want(l.lift, true, "the dragged card is lifted");
    want(l.frame, true, "and still framed");
    want(l.contain, false, "and not contained");
    want(l.counter, false, "and carries no counter");
    want(l.button, false, "and no button");
  }

  if (failures) {
    std::printf("tile_look_check: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("tile_look_check: ok\n");
  return 0;
}

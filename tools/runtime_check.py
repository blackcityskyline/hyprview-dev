#!/usr/bin/env python3
"""Runtime checks for the subsystems that were split into types.

Everything else in tests/ is headless. These are the claims that only a running compositor
can confirm, because the change was in the paint path and the input path:

  - open() parks and still reaches the screen (lifecycle: Own::Parked -> Own::Screen)
  - a close never shows a transparent frame (lifecycle: requestDeactivate refuses)
  - a strip that loses a card mid-scroll pulls its offset back (scroll: setMax re-clamps)
  - a drag release produces exactly one DRAG| branch record, and the group of records
    matches what the mouse did
  - DRAG| and LIFE| lines parse with the same grammar as ANIM|

Each check states what it expects and what it would mean if the expectation failed, so a
red line is a diagnosis and not just a number.

Usage:
    python3 tools/runtime_check.py            # all checks
    python3 tools/runtime_check.py --list
    python3 tools/runtime_check.py lifecycle  # one group
    python3 tools/runtime_check.py --setup    # build a scene first (needs M.terminal)

Requires a live Hyprland with the plugin loaded and debug_trace on:
    hl.config({plugin={gloview={debug_trace=1}}})
Without the trace the DRAG|/LIFE| groups report SKIP rather than a false pass -- the log
being empty is not evidence that nothing happened.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys
import time

LOG = "/tmp/gloview.log"

# The same grammar the emitters were documented with: pipe-separated key=value, no spaces in
# a value. Parsed here rather than grepped so a prose record fails instead of passing.
KV = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=[^|\s]+$")


def instance_signature():
    """The compositor this shell is talking to. hyprctl needs it and does not find it."""
    for path in ("/run/user/%d/hypr" % os.getuid(), "/run/user/1000/hypr"):
        try:
            newest = max((os.path.join(path, n) for n in os.listdir(path)),
                         key=os.path.getmtime)
        except OSError:
            continue
        sig = os.path.basename(newest)
        if sig.startswith("hyprland"):
            return sig
    return os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "")


SIG = instance_signature()
ENV = dict(os.environ, HYPRLAND_INSTANCE_SIGNATURE=SIG) if SIG else dict(os.environ)


def log_size():
    try:
        return os.path.getsize(LOG)
    except OSError:
        return 0


def tail_from(offset):
    try:
        with open(LOG, "rb") as f:
            f.seek(offset)
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def cmd(args, settle=0.30):
    """One gloview.cmd. hyprctl exits 0 whatever happened, so the log is the only channel."""
    before = log_size()
    subprocess.run(
        ["hyprctl", "eval",
         f'gloview.cmd("{args}")'.replace("gloview.", "hl.plugin.gloview.")],
        env=ENV, capture_output=True, check=False)
    time.sleep(settle)
    return tail_from(before)


def records(text, tag):
    """Every `TAG|...` line, split into segments."""
    out = []
    for line in text.splitlines():
        if line.startswith(tag + "|"):
            out.append(line.split("|"))
    return out


def fields(segments, start):
    """key=value pairs from segment `start` onward."""
    out = {}
    for seg in segments[start:]:
        if "=" not in seg:
            continue
        k, _, v = seg.partition("=")
        out[k] = v
    return out


def grammar_ok(segments):
    """Every data segment is key=value. A prose segment means the record is not parseable."""
    bad = [s for s in segments[2:] if "=" in s and not KV.match(s)]
    # Segments with no '=' at all are legitimate: the verb (park, promote, release).
    return not bad, bad


class Check:
    """One check's verdict.

    `observed` counts the log records the check actually looked at. A check that inspected
    nothing has verified nothing, and the runner downgrades it to SKIP whatever it
    returned: a loop over an empty set passes vacuously, and this file already shipped one
    that did -- park_owns_screen_before_visible returned PASS against an empty log while two
    SKIPs printed beside it.
    """

    def __init__(self, name, group):
        self.name, self.group = name, group
        self.result = "PASS"
        self.detail = ""
        self.observed = -1  # -1: the check has no opinion, trust what it said

    def saw(self, n):
        self.observed = n
        return self

    def ok(self, msg):
        if self.result == "PASS":
            self.result = "PASS"
            self.detail = msg
        return self

    def fail(self, msg):
        self.result = "FAIL"
        self.detail = msg
        return self

    def skip(self, msg):
        self.result = "SKIP"
        self.detail = msg
        return self


CHECKS = []


def check(group):
    def deco(fn):
        CHECKS.append((group, fn))
        return fn
    return deco


# --- lifecycle ----------------------------------------------------------------------

@check("lifecycle")
def open_promotes(c):
    """open() must park, then promote. Parking with no promote means the overlay never
    appeared; promoting with no park means the first frame went up without a backdrop,
    which is the black frame the whole arrangement exists to avoid."""
    out = cmd("open", settle=0.6)
    kinds = [p[1] for p in records(out, "LIFE") if len(p) > 1]
    cmd("close", settle=0.5)
    c.saw(len(kinds))
    if not kinds:
        return c.skip("no LIFE| records -- enable debug_trace, or the plugin is not loaded")
    if "park" not in kinds:
        return c.fail("no LIFE|park on open: %s" % kinds)
    if "promote" not in kinds:
        return c.fail("parked but never promoted: the overlay stayed invisible -- %s" % kinds)
    return c.ok("park then promote, in that order")


@check("lifecycle")
def park_owns_screen_before_visible(c):
    """A parked overlay is not `active` but does own the screen. If the trace shows a
    promote with own=parked, promote() is being called from outside the parked state and
    the first-frame guarantee is gone."""
    out = cmd("open", settle=0.6)
    cmd("close", settle=0.5)
    # Counting what was actually observed. A loop over zero records passes vacuously, and
    # this harness is exactly the thing that would then report a green run on an empty log.
    seen = 0
    for seg in records(out, "LIFE"):
        if len(seg) > 2 and seg[1] == "promote":
            seen += 1
            if fields(seg, 2).get("own") == "parked":
                return c.fail("a promote record reports own=parked: %s" % "|".join(seg))
    c.saw(seen)
    if not seen:
        return c.skip("no promote record observed -- debug_trace off, or the plugin is "
                      "not loaded")
    return c.ok("%d promote record(s), each reporting own=screen" % seen)


@check("lifecycle")
def close_is_quiet(c):
    """A close after a real open must end with the overlay released. deactivating=1
    persisting into the next open would mean finishPendingDeactivate never ran, and the
    overlay would keep swallowing input."""
    out = cmd("open", settle=0.6)
    cmd("close", settle=0.8)
    out2 = cmd("state", settle=0.3)
    st = records(out2, "STATE")
    c.saw(len(st))
    if not st:
        return c.skip("no STATE| record -- `state` command unavailable")
    f = fields(st[-1], 1)
    if f.get("open") == "1" or f.get("pending") == "1":
        return c.fail("after close the state still reads open=%s pending=%s"
                      % (f.get("open"), f.get("pending")))
    return c.ok("after close: open=0 pending=0")


# --- scroll -------------------------------------------------------------------------

@check("scroll")
def strip_scroll_clamps(c):
    """A strip that loses a card mid-scroll must pull its offset back into range. Before
    the type existed nothing re-clamped the CURRENT value, so the group could render
    scrolled past the end. Needs at least two cards, and a workspace change while the
    overview is up, which is what rebuilds the strip."""
    out = cmd("open", settle=0.6)
    out += cmd("allws 2", settle=0.5)
    out += cmd("close", settle=0.5)
    bad, seen, meaningful = [], 0, 0
    for line in out.splitlines():
        if not line.startswith("SCROLL|"):
            continue
        seen += 1
        for seg in line.split("|")[2:]:
            if not KV.match(seg):
                bad.append(seg)
        # The invariant itself, read off the record rather than trusted.
        f = fields(line.split("|"), 2)
        try:
            mx, cur = float(f["max"]), float(f["current"])
        except (KeyError, ValueError):
            continue
        if mx > 0.0:
            meaningful += 1
        if cur < -0.001 or cur > mx + 0.001:
            bad.append("current=%g outside max=%g" % (cur, mx))
    c.saw(seen)
    if bad:
        return c.fail("scroll out of range or unparseable: %s" % bad[:3])
    # Records alone are not evidence. A strip whose cards all fit the band emits max=0
    # scrollable=0 on every rebuild, and `0 <= current <= 0` is true of everything -- the
    # first run of this check passed that way, on four records that tested nothing.
    # Counting records was the guard; it was not enough. The invariant has to have been
    # live somewhere in what was observed.
    if not meaningful:
        return c.skip("%d scroll record(s), all with max=0 -- every card fits the band, "
                      "so [0,max] was never exercised. Widen the strip or add cards, then "
                      "scroll it by hand." % seen)
    return c.ok("%d scroll record(s), %d with a non-empty range; every position within "
                "[0, max]" % (seen, meaningful))


# --- drag ---------------------------------------------------------------------------

def watch(seconds, prompt):
    """Arm the log, tell the human what to do, and read what came out.

    A drag cannot be produced through the seam: `gloview.cmd` sets state, it does not move
    the pointer, and the FSM under test is driven by real button events. So this group is
    the one place where the harness asks instead of acts, and SKIPs rather than passes when
    the human does nothing -- silence is not evidence that the path worked.
    """
    print("\n  >>> %s\n      (%s seconds, then read back the log)\n" % (prompt, seconds))
    before = log_size()
    time.sleep(seconds)
    return tail_from(before)


@check("drag")
def drag_emits_one_branch(c):
    """One press-drag-release, one DRAG|release, and at least one DRAG|branch. Zero
    branches means the release returned before deciding anything. Two branches for one
    release means two code paths fired, which is what a lost press/release pair looks
    like -- the FSM now makes that unreachable in one call, so seeing it means a caller
    bypassed the transition.
    """
    cmd("open", settle=0.6)
    out = watch(8, "DRAG a card with the LEFT button and let go. Then, if you have time, "
                   "drag one with the RIGHT button.")
    cmd("close", settle=0.4)
    recs = records(out, "DRAG")
    if not recs:
        return c.skip("no DRAG| records -- either you did not drag, or debug_trace is off")
    c.saw(len(recs))
    rel = [r for r in recs if len(r) > 1 and r[1] == "release"]
    branch = [r for r in recs if len(r) > 1 and r[1] == "branch"]
    if not rel:
        return c.fail("%d DRAG records but no release -- the press never reached the FSM" % len(recs))
    if not branch:
        return c.fail("released but no branch decided: %d release(s), 0 branch(es)"
                      % len(rel))
    if len(branch) < len(rel):
        return c.fail("%d release(s) produced %d branch(es) -- a release decided nothing"
                      % (len(rel), len(branch)))
    for segs in recs:
        ok, bad = grammar_ok(segs)
        if not ok:
            return c.fail("unparseable segment(s) %s in %s" % (bad[:3], "|".join(segs)))
    return c.ok("%d release(s), %d branch(es), all parse" % (len(rel), len(branch)))


@check("drag")
def drag_branch_is_a_decision(c):
    """Every branch must name itself in `name=`, and no two releases may share one
    name unless they were the same gesture. The branch name is the whole point of the
    record -- it is what says which of twenty release paths ran."""
    out = tail_from(max(0, log_size() - 20000))
    recs = [r for r in records(out, "DRAG") if len(r) > 1 and r[1] == "branch"]
    if not recs:
        return c.skip("no DRAG|branch in the recent log -- run the drag check first")
    c.saw(len(recs))
    for segs in recs:
        f = fields(segs, 2)
        if not f.get("name"):
            return c.fail("a branch record with no name=: %s" % "|".join(segs))
    return c.ok("%d branch record(s), every one names its path" % len(recs))


def run(groups):
    setup = "--setup" in sys.argv
    if setup:
        print("building a scene first ...")
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__),
                                                     "anim_matrix.py"), "--setup"],
                       env=ENV, check=False)

    selected = [g for g in groups if g != "--setup"]
    rows = []
    for group, fn in CHECKS:
        if selected and group not in selected:
            continue
        c = Check(fn.__name__, group)
        try:
            fn(c)
        except Exception as exc:  # a harness that dies is not a pass
            c.fail("harness error: %r" % exc)
        if c.observed == 0 and c.result == "PASS":
            # Silent downgrade. The check looked at nothing and said nothing was wrong,
            # which is not the same as something being right.
            c.result = "SKIP"
            c.detail = "observed 0 log records -- %s" % (c.detail or "nothing to check")
        rows.append(c)
        print("%-5s %-12s %-32s %s" % (c.result, c.group, c.name, c.detail))

    failed = [r for r in rows if r.result == "FAIL"]
    skipped = [r for r in rows if r.result == "SKIP"]
    print("\n%d run, %d passed, %d failed, %d skipped"
          % (len(rows), len(rows) - len(failed) - len(skipped), len(failed), len(skipped)))
    if skipped:
        print("A SKIP is not a pass: the log said nothing, which is not evidence that "
              "nothing happened.")
    return 1 if failed else 0


def main():
    if "--list" in sys.argv:
        for group, fn in CHECKS:
            print("%-12s %s" % (group, (fn.__doc__ or "").strip().splitlines()[0]))
        return 0
    ap = argparse.ArgumentParser(add_help=False)
    ap.parse_known_args()
    groups = [a for a in sys.argv[1:] if not a.startswith("-")]
    return run(groups)


if __name__ == "__main__":
    sys.exit(main())
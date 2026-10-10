#!/usr/bin/env python3
"""Run the animation matrix and compare what was asked for against what ran.

    python3 tools/anim_matrix.py                 # full matrix
    python3 tools/anim_matrix.py --quick         # one style, one duration
    python3 tools/anim_matrix.py --list          # show the matrix, run nothing

What it drives
--------------
`gloview.cmd` (src/actions/actions.cpp) over Lua, because under a Lua config
`hyprctl dispatch` does not reach plugin dispatchers. Each command acknowledges
itself in /tmp/gloview.log as CMD|ok / CMD|err -- `hyprctl eval` swallows Lua
return values, so the log is the only channel that reports success.

What it compares
----------------
For every (style, duration, surface) cell the trace must show an animation that
matches what was configured:

    asked   style=X ms=N surface=grid
    reality ANIM|begin style=X ms=N surface=grid from!=to
            ANIM|end   ms within tolerance of N
            no ANIM|reject

`from != to` is the load-bearing check: a flight whose target equals its origin
resolves to zero distance and draws nothing moving, and that is exactly the bug
class this matrix exists to catch. Measured duration is wall-clock from the
flight's own stamp, not a sum of configured numbers.

It never creates windows. With fewer than two tiles it says so and exits rather
than reporting a matrix of failures that are really a precondition.
"""

import argparse
import os
import re
import subprocess
import sys
import time

LOG = "/tmp/gloview.log"

# Durations in ms. Deliberately spread: a swap that only looks right at 320ms is
# wrong at 120, and the cheap one is where a curve bug hides.
DURATIONS = [120, 320, 700]
STYLES = ["horizontal", "slidevert", "fade", "pop"]
SURFACES = ["grid", "strip"]


def sig():
    """The live Hyprland socket. A stale one reloads into the void and the run
    silently measures the previous session's plugin (REFACTORING.md)."""
    try:
        newest = max(
            (os.path.join("/run/user/1000/hypr", d)
             for d in os.listdir("/run/user/1000/hypr")),
            key=os.path.getmtime,
        )
        return os.path.basename(newest)
    except (OSError, ValueError):
        return ""


ENV = dict(os.environ)
if _sig := sig():
    ENV["HYPRLAND_INSTANCE_SIGNATURE"] = _sig


def cmd(args, settle=0.30):
    """One gloview.cmd. hyprctl always exits 0, so success is read from the log."""
    before = log_size()
    subprocess.run(["hyprctl", "eval", f'gloview.cmd("{args}")'.replace('gloview.', 'hl.plugin.gloview.')],
                   env=ENV, capture_output=True, check=False)
    time.sleep(settle)
    return tail_from(before)


def log_size():
    try:
        return os.path.getsize(LOG)
    except OSError:
        return 0


def tail_from(offset):
    """Everything appended to the log since `offset` -- the only per-run channel."""
    try:
        with open(LOG, "rb") as f:
            f.seek(offset)
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def fields(line):
    return dict(p.split("=", 1) for p in line.split("|")[2:] if "=" in p)


def state():
    out = cmd("state")
    st = {}
    for line in out.splitlines():
        if line.startswith("STATE|"):
            st = fields(line)
        elif line.startswith("TILE|"):
            _, i, cls, ws, box = line.split("|")[:5]
            # d=<depth>, present only when the run is active. Depth is what the
            # painter sorts on, so a tile whose box moved while its depth stayed put
            # is exactly the overlap bug; the trace has to carry both to show it.
            depth = -1
            for part in line.split("|")[5:]:
                if part.startswith("d="):
                    depth = int(part[2:])
            st[f"tile{i}"] = (cls, ws, box, depth)
    return st


def setup():
    subprocess.run(["hyprctl", "eval",
                    'hl.config({plugin = {gloview = {debug_trace = 1}}})'],
                   env=ENV, capture_output=True, check=False)
    out = cmd("state")
    if "CMD|err" in out:
        print("gloview is not answering commands:", out.strip().splitlines()[-1])
        return False
    cmd("open", settle=1.0)
    # all-workspaces, because a single workspace usually holds one window and the
    # matrix needs two to have something to exchange. It is also the mode both
    # surfaces are visible in at once, which is what the strip half of the matrix
    # actually tests.
    cmd("allws 1", settle=0.6)
    return True


def preconditions():
    st = state()
    tiles = int(st.get("tiles", 0))
    if tiles < 2:
        print(f"need at least 2 tiles to swap, have {tiles}.")
        print("Open more windows (or switch to a workspace with some), then re-run.")
        return None
    return st


def check_depth(st):
    """Depth must rank the geometry it belongs to.

    The run paints far-to-near, so the card nearest the middle is depth 0 and is
    drawn last, over everything. A tile whose box moved while its depth stayed put
    inverts that: the periphery keeps depth 0 and covers the centre. The trace could
    not see this before depth was dumped -- it recorded geometry only, and geometry
    alone is consistent with both the right and the wrong painting.
    """
    # k[4:].isdigit(), not startswith("tile"): the STATE line carries `tiles=<count>`,
    # which matches the prefix and arrives as a bare string.
    tiles = {k: v for k, v in st.items()
             if k.startswith("tile") and k[4:].isdigit()}
    if not tiles or all(v[3] < 0 for v in tiles.values()):
        return None  # not a carousel run; nothing to assert
    live = [v for v in tiles.values() if v[3] >= 0]
    if len(live) < 2:
        return None
    try:
        mid = float(st.get("ccx", 0))
    except ValueError:
        return None
    centres = sorted(((abs(int(v[2].split(",")[0]) + int(v[2].split(",")[2]) / 2 - mid), v)
                      for v in live), key=lambda p: p[0])
    for rank, (_, v) in enumerate(centres):
        if v[3] != rank:
            return (f"OVERLAP INVERTED: the card nearest the middle has depth "
                    f"{v[3]}, expected {rank}")
    return True


def reset():
    """Every cell starts from the same scene.

    Without this the cells are not comparable: each swap moves the selection and the
    run's centred card, so cell 12 was testing a different arrangement than cell 3 and
    the same code passed in one run and failed in the next. A matrix whose cells are not
    independent reports noise.
    """
    cmd("close", settle=0.7)
    cmd("open", settle=0.9)
    cmd("allws 1", settle=0.5)


def one_cell(style, ms, surface, dry=False):
    """Configure, act, and judge one cell. Returns (verdict, [(ok, message)])."""
    findings = []
    ok = True
    reset()
    style_key = "grid_swap_style" if surface == "grid" else "strip_swap_style"
    # A swap moves TWO windows and both fly, so both leaves are pinned. Setting only
    # the initiator's tuning left the partner on the user's rule and the cell reported
    # two different durations for one gesture.
    leaves = ["swap_main", "swap_partner"]

    # speed is pinned to 1 alongside the duration: `<leaf>_ms` is the BASE, and the
    # user's own `<leaf>_speed` divides it on resolve (1.2 turned a requested 320 into
    # 266). Pinning it is what makes "expected == reality" a statement about the
    # duration rather than about the user's taste.
    writes = [(style_key, style)]
    for leaf in leaves:
        writes += [(f"{leaf}_ms", ms), (f"{leaf}_speed", 1)]
    for key, val in writes:
        out = cmd(f"set {key} {val}")
        if "CMD|err" in out:
            findings.append((False, f"set {key} {val} rejected"))
            return "FAIL", findings

    before = log_size()
    # swap 0 1 -- the drag gesture's endpoint. The harness cannot move a pointer,
    # so it drives the same call the release path makes.
    out = cmd("swap 0 1", settle=0.15)
    if "CMD|err" in out:
        findings.append((False, f"swap rejected: {out.split('CMD|err|')[-1]}"))
        return "FAIL", findings
    time.sleep(ms / 1000.0 + 0.45)
    run = tail_from(before)
    after = state()
    # None = not a run, True = fine, str = the bug. Sets ok, which the flight checks
    # below do not -- overlap is a property of the resting scene, not of the flight.
    depth_problem = check_depth(after)
    if depth_problem and depth_problem is not True:
        findings.append((False, depth_problem))
        ok = False

    begins = [fields(l) for l in run.splitlines() if l.startswith("ANIM|begin|")]
    ends = {fields(l)["seq"]: int(fields(l)["ms"])
            for l in run.splitlines() if l.startswith("ANIM|end|")}
    rejects = [l for l in run.splitlines() if l.startswith("ANIM|reject|")]

    if rejects:
        findings.append((False, f"flight rejected: {rejects[0].split('|')[-1]}"))
    if not begins:
        findings.append((False, "no ANIM|begin -- nothing animated"))
        return "FAIL", findings

    for b in begins:
        if b.get("surface") != surface:
            continue
        if b.get("style") != style:
            findings.append((False, f"style asked {style}, ran {b.get('style')}"))
            ok = False
        if b.get("ms") != str(ms):
            findings.append((False, f"ms asked {ms}, ran {b.get('ms')}"))
            ok = False
        if b.get("from") == b.get("to"):
            findings.append((False, f"ZERO DISTANCE from==to=={b.get('from')}"))
            ok = False
        end_ms = ends.get(b["seq"])
        if end_ms is None:
            findings.append((False, f"seq {b['seq']} never ended -- flight leaked"))
            ok = False
        else:
            # Cannot finish early; may overshoot by the frames the tween was polled at.
            lo, hi = ms * 0.85, ms + 150
            if not (lo <= end_ms <= hi):
                findings.append((False, f"measured {end_ms}ms outside [{lo:.0f},{hi:.0f}]"))
                ok = False
            else:
                findings.append((True, f"{end_ms}ms"))
    if not any(b.get("surface") == surface for b in begins):
        saw = sorted({b.get("surface", "?") for b in begins})
        findings.append((False, f"no flight on {surface} (saw {saw})"))
        ok = False

    return ("PASS" if ok else "FAIL"), findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="one style, one duration")
    ap.add_argument("--list", action="store_true", help="print the matrix and exit")
    ap.add_argument("--keep", action="store_true", help="leave tuning overrides behind")
    args = ap.parse_args()

    styles, durations = (STYLES[:1], DURATIONS[1:2]) if args.quick else (STYLES, DURATIONS)
    surfaces = ["grid"] if args.quick else SURFACES

    cells = [(s, m, f) for f in surfaces for s in styles for m in durations]
    if args.list:
        print(f"{len(cells)} cells: " + " ".join(f"{s}/{m}/{f}" for s, m, f in cells))
        return 0

    if not setup():
        return 2
    st = preconditions()
    if st is None:
        return 2
    if int(st.get("carousel", 0)) == 0:
        print("run is not active, so the overlap invariant cannot be checked; "
              "the matrix would be measuring a layout nobody sees.")
        return 2

    print(f"preconditions ok: {st.get('tiles')} tiles, allws={st.get('allws')}, "
          f"carousel={st.get('carousel')}")
    print(f"{len(cells)} cells\n")
    print(f"{'surface':8} {'style':11} {'ms':>5}  verdict  detail")
    fails = 0
    for style, ms, surface in cells:
        verdict, findings = one_cell(style, ms, surface)
        if verdict == "FAIL":
            fails += 1
        detail = "; ".join(m for _, m in findings) or "-"
        print(f"{surface:8} {style:11} {ms:5}  {verdict:7}  {detail}")

    if not args.keep:
        for key in ("grid_swap_style", "strip_swap_style", "swap_main_ms",
                    "swap_partner_ms", "swap_main_speed", "swap_partner_speed"):
            cmd(f"set {key} ")

    cmd("close", settle=0.8)
    print(f"\n{len(cells) - fails}/{len(cells)} passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
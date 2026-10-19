#!/usr/bin/env python3
"""The hyprview side of brotab: poll its mediators, write the files the plugin reads.

brotab already has what a browser half needs -- an extension for every Gecko and Chromium
browser, a tab list, the active tab, a screenshot, and the verbs -- served over HTTP by a
mediator process the extension spawns. This does not reimplement any of that. It asks.

Each mediator listens on 127.0.0.1 in a small port range, one per browser, and reports:

    /list_tabs       "windowId.tabId<TAB>title<TAB>url" per line
    /get_active_tabs "windowId.tabId,windowId.tabId" for the focused tabs
    /get_screenshot  a PNG of what is on screen right now
    /get_pid         this mediator's own pid

The pid is the part that matters and the reason this is 120 lines rather than a weekend. The
mediator is a child of the browser, so its PARENT is the browser's process -- and a Hyprland
window carries that same pid. That is how a browser window finds its compositor window: exactly,
with no title in it. A title cannot do it, because a browser appends its own name to a window's
title, so what the compositor holds and what the browser reports are different strings by
construction.

Written to, all under --dir (default $XDG_RUNTIME_DIR/hyprview):

    tabs                   the feed src/model/tab_feed.hpp parses
    previews/<tabId>.png   the screenshot

Every write is atomic. The plugin reads these while they change, and a reader that catches a
file mid-write sees a picture cairo refuses and a feed the parser rejects -- which degrades to
"nothing new" for a frame, but the rename means the question never arises.
"""

import argparse
import os
import sys
import urllib.error
import urllib.request

# brotab's own range: brotab/mediator/const.py, DEFAULT_MIN_HTTP_PORT..DEFAULT_MAX_HTTP_PORT.
# Duplicated rather than imported because brotab is not a dependency of the plugin -- it is
# something the user happens to have installed, and this must not import it to say "hello".
MIN_PORT = 4625
MAX_PORT = 4635

# How often to ask. brotab's CLI polls far slower; this is a compositor that paints at 60Hz and
# wants a tab list that is not a frame behind, but not a request per frame either.
POLL_SECONDS = 0.5

TIMEOUT = 1.0


def log(msg):
    sys.stderr.write("hyprview-bridge: %s\n" % msg)
    sys.stderr.flush()


def get(port, path):
    """One GET. Returns the body, or None for anything at all being wrong with it.

    Every failure -- no mediator, a dead one, a browser that closed mid-poll -- is the same
    answer, because the caller's response to all of them is the same: do not write, and try
    again in half a second. Distinguishing them would mean deciding which are worth a log line,
    and a browser starting up produces one of them every poll.
    """
    url = "http://127.0.0.1:%d%s" % (port, path)
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT) as r:
            return r.read()
    except (urllib.error.URLError, OSError, ValueError):
        return None


def parent_pid(pid):
    """The parent of a process, or None.

    /proc/<pid>/stat field 4. Parsed from the LAST ')' because the second field is the
    executable name in parentheses and may itself contain one -- which is how "the parent is
    None" happens on a process called something like `weird)name`.
    """
    try:
        with open("/proc/%d/stat" % pid, "rb") as f:
            data = f.read()
    except OSError:
        return None
    close = data.rfind(b")")
    if close < 0:
        return None
    fields = data[close + 2 :].split()
    if len(fields) < 2:
        return None
    try:
        return int(fields[1])
    except ValueError:
        return None


def atomic_write(path, data):
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def parse_tabs(body, active, browser_pid, generation):
    """The feed, from brotab's answers.

    `list_tabs` sorts by (windowId, tab index) already, so the order here is the browser's and
    is the order the strip shows -- see tab_feed.hpp on why order is not cosmetic.
    """
    active_ids = set()
    for token in active.split(b","):
        token = token.strip()
        if token:
            active_ids.add(token)

    lines = ["g %d" % generation]
    seen_windows = set()
    for raw in body.split(b"\n"):
        if not raw.strip():
            continue
        parts = raw.split(b"\t")
        if len(parts) < 2:
            continue
        ident, title = parts[0].strip(), parts[1]
        window, _, tab = ident.partition(b".")
        if not window.isdigit() or not tab.isdigit():
            continue
        # One 'p' line per window, not one per feed: several mediators can be up at once, each
        # describing a different browser, and their windows must not be attributed to each other.
        # Once per window, not once per tab. The map on the far side would not care, but a
        # browser with forty tabs would carry the same line forty times, and the grammar is
        # cheaper to read when it says what it means.
        if window not in seen_windows:
            seen_windows.add(window)
            lines.append("p %d %s" % (browser_pid, window.decode()))
        # A title is the rest of the feed's grammar and cannot contain the line's separators.
        text = title.replace(b"|", b" ").replace(b"\n", b" ").replace(b"\r", b" ")
        lines.append(
            "t %s %s %d %s"
            % (
                window.decode(),
                tab.decode(),
                1 if ident in active_ids else 0,
                text.decode("utf-8", "replace"),
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def poll_once(root, generation):
    """One sweep over the port range. Returns how many mediators answered."""
    os.makedirs(os.path.join(root, "previews"), exist_ok=True)
    live = 0
    for port in range(MIN_PORT, MAX_PORT + 1):
        raw_pid = get(port, "/get_pid")
        if raw_pid is None:
            continue
        try:
            mediator_pid = int(raw_pid.strip())
        except ValueError:
            continue
        browser_pid = parent_pid(mediator_pid)
        if not browser_pid:
            continue
        tabs = get(port, "/list_tabs")
        if tabs is None:
            continue
        active = get(port, "/get_active_tabs") or b""
        feed = parse_tabs(tabs, active, browser_pid, generation)

        if port == MIN_PORT:
            # One feed describes every browser there is, so the first mediator to answer writes
            # it and the rest contribute their windows into the SAME document. Writing per
            # mediator instead would mean the plugin reading a directory of feeds and having
            # to decide which one wins.
            feed = merge_feed(root, feed)
        else:
            feed = merge_feed(root, feed)
        atomic_write(os.path.join(root, "tabs"), feed)
        live += 1

        shot = get(port, "/get_screenshot")
        if shot and shot.startswith(b"\x89PNG"):
            for tab_id in active_tab_ids(active):
                atomic_write(os.path.join(root, "previews", "%s.png" % tab_id), shot)
    return live


def active_tab_ids(active):
    out = []
    for token in active.split(b","):
        token = token.strip()
        if not token:
            continue
        _, _, tab = token.partition(b".")
        if tab.isdigit():
            out.append(tab.decode())
    return out


def merge_feed(root, incoming):
    """Fold one browser's windows into the feed on disk, keeping the first 'g' line.

    The header is written once by whoever gets there first and belongs to the whole document,
    because generation is about the run of tabs and ids are only meaningful under one.
    """
    path = os.path.join(root, "tabs")
    header = None
    body = []
    if os.path.exists(path):
        with open(path, "rb") as f:
            existing = f.read().split(b"\n")
        for line in existing:
            if line.startswith(b"g "):
                header = line
            elif line.strip():
                body.append(line)
    lines = incoming.decode("utf-8", "replace").split("\n")
    for line in lines:
        if line.startswith("g "):
            continue
        if line.strip():
            body.append(line.encode("utf-8"))
    if header is None:
        for line in lines:
            if line.startswith("g "):
                header = line.encode("utf-8")
                break
    out = ([header] if header else []) + body
    return b"\n".join(out) + b"\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--dir", default=os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "hyprview")
    )
    ap.add_argument("--once", action="store_true", help="one sweep, then exit")
    ap.add_argument("--generation", type=int, default=1)
    args = ap.parse_args()

    log("polling %d-%d, writing %s" % (MIN_PORT, MAX_PORT, args.dir))
    while True:
        try:
            live = poll_once(args.dir, args.generation)
            if args.once:
                log("%d mediator(s) answered" % live)
                return 0
        except KeyboardInterrupt:
            return 0
        except OSError as e:
            log("poll failed: %s" % e)
        import time

        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    sys.exit(main())
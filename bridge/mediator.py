#!/usr/bin/env python3
"""The mediator: the browser half of gloview's tabs, and the only thing that writes.

Started by the extension through `runtime.connectNative`, which is what makes this work without
a binary anywhere. Native messaging is happy with an interpreter and a script; what it will not
take is a compiled helper, and there is no need for one.

Wired to stdio in the framing native messaging already specifies: a 4-byte native-endian length,
then that many bytes of JSON. No port, no handshake, no port range to scan.

Why a separate process at all, when the point was just to read tabs:

    The extension cannot report the browser's process id. It has no way to ask for one.

And it needs one. A browser appends its own name to a window's title, so what the compositor
holds for a window and what the browser reports for its active tab are different strings by
construction, always -- "(page) - YouTube -- Zen Browser" against "(page) - YouTube". A title
therefore cannot match a browser window to a compositor window, and matching by prefix is the
exact guess that puts one browser's tabs under another's window.

A process id can. This process is started BY the browser, so its parent is the browser, and a
Hyprland window carries that same pid. Exact, with nothing heuristic about it.

Writes, all under --dir (default $XDG_RUNTIME_DIR/gloview):

    tabs                   the feed src/model/tab_feed.hpp parses
    previews/<tabId>.png   a screenshot of what is on screen

Every write is atomic -- written beside the target and renamed over it -- because the plugin
reads these while they change, and a reader catching a file mid-write sees a picture cairo
refuses and a feed the parser rejects.
"""

import argparse
import json
import os
import struct
import sys

# Tab ids are reused -- close 41, 41 exists again a moment later pointing at another page -- so
# an id is only meaningful under the connection that issued it. This is the connection.
GENERATION = 1


def log(msg):
    # stderr, because stdout is the protocol channel and a stray line there is a corrupt frame.
    sys.stderr.write("gloview-mediator: %s\n" % msg)
    sys.stderr.flush()


def parent_pid(pid):
    """The parent of a process, or None.

    /proc/<pid>/stat field 4, parsed from the LAST ')' -- the second field is the executable
    name in parentheses and may contain one itself, which is how the parent comes back None on a
    process called something like `weird)name`.
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


def build_feed(command, browser_pid):
    """The feed, from one of the extension's reports.

    'p' once per window, not once per feed: an extension talks about ONE browser, but a user
    runs four, each with its own mediator, and the plugin reads a single feed. Whoever writes
    second folds their windows into the document on disk rather than replacing it.

    Once per window rather than once per tab because a browser with forty tabs would otherwise
    carry the line forty times, and the grammar is cheaper to read when it says what it means.
    """
    tabs = command.get("tabs") or []
    active = set(command.get("active") or [])

    lines = ["g %d" % GENERATION]
    seen = set()
    for tab in tabs:
        window = tab.get("windowId")
        ident = tab.get("id")
        if not isinstance(window, int) or not isinstance(ident, int):
            continue
        if window not in seen:
            seen.add(window)
            lines.append("p %d %d" % (browser_pid, window))
        # A title is the rest of the line and must not carry the grammar's own separators.
        title = (tab.get("title") or "").replace("|", " ").replace("\n", " ").replace("\r", " ")
        lines.append("t %d %d %d %s" % (window, ident, 1 if ident in active else 0, title))
    return ("\n".join(lines) + "\n").encode("utf-8")


def merge_into(root, incoming):
    """Fold this browser's windows into the feed on disk, keeping the first generation line.

    The header belongs to the whole document, because generation is about the run of tabs and
    every id in the file is only meaningful under one.
    """
    path = os.path.join(root, "tabs")
    header = None
    body = []
    if os.path.exists(path):
        with open(path, "rb") as f:
            for line in f.read().split(b"\n"):
                if line.startswith(b"g "):
                    if header is None:
                        header = line
                elif line.strip():
                    body.append(line)
    for line in incoming.split(b"\n"):
        if line.startswith(b"g "):
            if header is None:
                header = line
        elif line.strip():
            body.append(line)
    out = ([header] if header else []) + body
    atomic_write(path, b"\n".join(out) + b"\n")


def handle(command, root, own_pid):
    """One command in, one reply out. Never raises: the channel is the connection, and a
    mediator that dies on a malformed report takes the whole run of tabs with it."""
    name = command.get("name")

    if name == "report":
        browser_pid = parent_pid(own_pid)
        if not browser_pid:
            # Without a parent there is no match to be had, and a feed with p 0 in it would
            # attach tabs to whatever has no process. Say so and write nothing.
            log("cannot read a parent pid; not writing a feed")
            return {"ok": False, "reason": "no-parent-pid"}

        merge_into(root, build_feed(command, browser_pid))
        shot = command.get("screenshot")
        if isinstance(shot, str) and shot.startswith("data:image/png"):
            import base64

            raw = base64.b64decode(shot.split(",", 1)[1])
            for ident in command.get("active") or []:
                if isinstance(ident, int):
                    atomic_write(os.path.join(root, "previews", "%d.png" % ident), raw)
        return {"ok": True, "browserPid": browser_pid, "windows": len(command.get("active") or [])}

    if name == "activate_tab":
        return {"ok": False, "reason": "the browser switched the tab; nothing to do here"}

    return {"ok": False, "reason": "unknown command %r" % name}


def read_frame(stdin):
    """One length-prefixed JSON message, or None at end of stream."""
    head = stdin.read(4)
    if head is None or len(head) < 4:
        return None
    (length,) = struct.unpack("@I", head)
    if length > 64 * 1024 * 1024:
        # A screenshot is a few hundred kilobytes; anything past this is a corrupt length, and
        # honouring it would ask for all the memory this process has.
        return None
    body = stdin.read(length)
    if body is None or len(body) < length:
        return None
    try:
        return json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None


def write_frame(stdout, message):
    encoded = json.dumps(message).encode("utf-8")
    stdout.write(struct.pack("@I", len(encoded)))
    stdout.write(encoded)
    stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--dir", default=os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "gloview")
    )
    args = ap.parse_args()
    os.makedirs(os.path.join(args.dir, "previews"), exist_ok=True)

    own = os.getpid()
    stdin, stdout = sys.stdin.buffer, sys.stdout.buffer
    log("started, pid %d, parent %s, writing %s" % (own, parent_pid(own), args.dir))

    while True:
        command = read_frame(stdin)
        if command is None:
            return 0
        try:
            reply = handle(command, args.dir, own)
        except Exception as e:  # a report must never cost the connection
            log("command failed: %r" % e)
            reply = {"ok": False, "reason": str(e)}
        write_frame(stdout, reply)


if __name__ == "__main__":
    sys.exit(main())
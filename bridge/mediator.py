#!/usr/bin/python3
"""The mediator: the browser half of hyprview's tabs, and the only thing that writes.

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

Writes, all under --dir (default $XDG_RUNTIME_DIR/hyprview):

    tabs                   the feed src/model/tab_feed.hpp parses
    previews/<tabId>.png   a screenshot of what that tab was showing when it was on screen

Every write is atomic -- written beside the target and renamed over it -- because the plugin
reads these while they change, and a reader catching a file mid-write sees a picture cairo
refuses and a feed the parser rejects.
"""

import json
import os
import select
import struct
import sys
import time

# Tab ids are reused -- close 41, 41 exists again a moment later pointing at another page -- so
# an id is only meaningful under the connection that issued it. This is the connection.
GENERATION = 1


_LOGFILE = [None]


# Fixed absolute path, on purpose. The browser spawns the host with whatever environment it
# happens to have, which is not the one a shell has: XDG_RUNTIME_DIR may be absent, and then
# every path derived from it points somewhere nobody looks. A log that can move is a log that
# is not there.
DEBUG_LOG = "/tmp/hyprview_mediator.log"


def open_log(directory):
    """A file, because stderr from a native messaging host is invisible from outside.

    The browser owns the host's stderr and does not surface it, so when Zen refused to start
    the host the only thing observable was the port opening and dropping with no reason given.
    Best effort -- a read-only /tmp must not stop the tabs from being reported.
    """
    for path in (DEBUG_LOG, os.path.join(directory, "mediator.log")):
        try:
            _LOGFILE[0] = open(path, "a", buffering=1)
            return
        except OSError:
            continue
    _LOGFILE[0] = None


def log(msg):
    line = "hyprview-mediator: %s\n" % msg
    # stderr too, in case someone is watching it from the browser's own logs.
    try:
        sys.stderr.write(line)
        sys.stderr.flush()
    except (OSError, ValueError):
        pass
    if _LOGFILE[0] is not None:
        try:
            _LOGFILE[0].write(line)
        except (OSError, ValueError):
            pass


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
        # Bit 0 active, bit 1 discarded -- one number, so the title stays the rest of the line.
        flags = (1 if ident in active else 0) | (2 if tab.get("discarded") is True else 0)
        lines.append("t %d %d %d %s" % (window, ident, flags, title))
    return ("\n".join(lines) + "\n").encode("utf-8")


def merge_into(root, incoming):
    """Fold this browser's windows into the feed on disk, REPLACING the windows it covers.

    Replacement is per window, not per line and not per feed. A browser window's tab list is
    authoritative over itself -- it knows which of its tabs still exist -- and appending it
    made the feed grow without bound: 387 lines for 17 real tabs, one id 32 times over, and
    the plugin laid out 379 cards from a browser with seventeen.

    The `p` lines are keyed by (process, window) and a `t` line belongs to whichever window
    names it, so a window is dropped by name and everything else on disk survives. That is also
    what lets two browsers share one feed: neither writes over the other's windows.

    The generation header is written once by whoever gets there first and belongs to the whole
    document, because generation is about the run of tabs and every id in the file is only
    meaningful under one.
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

    incoming_lines = incoming.split(b"\n")
    covered = set()
    for line in incoming_lines:
        if line.startswith(b"t "):
            parts = line.split(b" ", 2)
            if len(parts) >= 3:
                covered.add(parts[1])
        elif line.startswith(b"p "):
            parts = line.split(b" ", 2)
            if len(parts) >= 3:
                covered.add(parts[2])

    out = [header] if header else []
    for line in body:
        parts = line.split(b" ", 2)
        # "t <window> <tab> <flags> <title>" names its window at field 1,
        # "p <pid> <window>" at field 2.
        window = None
        if line.startswith(b"t ") and len(parts) >= 3:
            window = parts[1]
        elif line.startswith(b"p ") and len(parts) >= 3:
            window = parts[2]
        if window is not None and window in covered:
            continue  # this browser window just told us what it actually has
        out.append(line)

    for line in incoming_lines:
        if line.startswith(b"g "):
            if header is None:
                header = line
                out.append(line)
            continue
        if line.strip():
            out.append(line)
    atomic_write(path, b"\n".join(out) + b"\n")


# A request the plugin wrote: "a <browser-pid> <window> <tab>". Consumed only by the mediator
# whose browser it names -- tab ids are per browser, and two mediators share one directory.
# Older than a few seconds and it is a request for a state nobody is in any more.
ACTIVATE_MAX_AGE = 5.0


def drop(path):
    try:
        os.unlink(path)
    except OSError:
        pass  # another browser's mediator took it, or the plugin is rewriting it


def pop_activation(root, browser_pid):
    path = os.path.join(root, "activate")
    try:
        age = time.time() - os.path.getmtime(path)
        with open(path, "rb") as f:
            line = f.read(200).decode("utf-8", "replace").strip()
    except OSError:
        return None
    parts = line.split()
    try:
        pid, window, tab = (int(v) for v in parts[1:])
    except (IndexError, ValueError):
        pid, window, tab = 0, 0, 0
    if age > ACTIVATE_MAX_AGE or len(parts) != 4 or parts[0] != "a":
        log("dropping a malformed or stale activation: %r" % line)
        drop(path)
        return None
    if pid != browser_pid:
        return None  # another browser's tab: its own mediator is the one that can open it
    drop(path)
    return {"name": "activate", "window": window, "tab": tab}


SHOT_MAX_AGE = 3.0


def pop_shot(root, browser_pid):
    """A fresh picture was asked for: the plugin writes shot.<pid> when the overview opens.

    The file on disk is a picture of the tab the last time it was on screen, and a page that a
    style-changing extension painted after that would keep its first look for good.
    """
    path = os.path.join(root, "shot.%d" % browser_pid)
    try:
        age = time.time() - os.path.getmtime(path)
    except OSError:
        return None
    drop(path)
    return {"name": "shot"} if age <= SHOT_MAX_AGE else None


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
    # argv is NOT ours. Native messaging launches the host as
    #     <manifest.json> <extension-id>
    # and argparse treats both as positional arguments it does not know, exits 2, and the host
    # dies on the spot -- which looks from the extension exactly like "the port opened and
    # dropped", on a five-second loop, with nothing in any log. Parsed by hand, ignoring
    # whatever we did not ask for, because the caller is the browser and it is not negotiable.
    # Both spellings: --dir=PATH and --dir PATH. Anything we did not ask for is ignored, since
    # the browser's own two arguments are not ours to reject.
    directory = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "hyprview")
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        if argv[i] == "--dir" and i + 1 < len(argv):
            directory = argv[i + 1]
            i += 2
        elif argv[i].startswith("--dir="):
            directory = argv[i].split("=", 1)[1]
            i += 1
        else:
            i += 1
    os.makedirs(os.path.join(directory, "previews"), exist_ok=True)
    open_log(directory)

    own = os.getpid()
    stdin, stdout = sys.stdin.buffer, sys.stdout.buffer
    log("ARGS=%r CWD=%s" % (sys.argv[1:], os.getcwd()))
    log("started, pid %d, parent %s, writing %s" % (own, parent_pid(own), directory))

    browser = parent_pid(own)
    while True:
        # Idle time is when a request from the compositor is picked up: native messaging is a
        # two-way stream, so the host may speak without being asked.
        ready, _, _ = select.select([stdin], [], [], 0.2)
        if not ready:
            request = None
            if browser:
                request = pop_shot(directory, browser) or pop_activation(directory, browser)
            if request:
                log("pushing an activation: %r" % request)
                write_frame(stdout, request)
            continue
        log("waiting for a command")
        command = read_frame(stdin)
        if command is None:
            log("stdin reached end of stream -- exiting")
            return 0
        log("got a command: %s" % (command or {}).get("name"))
        try:
            reply = handle(command, directory, own)
        except Exception as e:  # a report must never cost the connection
            log("command failed: %r" % e)
            reply = {"ok": False, "reason": str(e)}
        write_frame(stdout, reply)


if __name__ == "__main__":
    sys.exit(main())
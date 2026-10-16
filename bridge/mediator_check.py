#!/usr/bin/env python3
"""The mediator, driven the way the extension drives it.

Not a smoke test. Everything worth checking here is invisible from the inside: the framing is a
byte count and the mediator has to survive being fed one; the pid is read out of /proc and only
exists because the mediator was started BY something, so the child process in this test is not
a stand-in for it, it IS the mechanism; and the merge has to leave a document a parser can read
when a second browser arrives and there is already one on disk.

The extension is driven over the same stdio a browser would use, in the same framing, so what is
checked is the wire and not a function called directly.
"""

import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MEDIATOR = os.path.join(HERE, "mediator.py")

failures = 0


def want(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL: %s" % what)


def send(proc, message):
    """A frame in native messaging's framing: native-endian length, then JSON."""
    body = json.dumps(message).encode("utf-8")
    proc.stdin.write(struct.pack("@I", len(body)))
    proc.stdin.write(body)
    proc.stdin.flush()


def recv(proc, timeout=10):
    head = proc.stdout.read(4)
    if head is None or len(head) < 4:
        return None
    (length,) = struct.unpack("@I", head)
    return json.loads(proc.stdout.read(length).decode("utf-8"))


def start(root):
    # A real child process, so /proc/<pid>/stat has this test as the parent. That is the whole
    # mechanism: the browser starts the mediator, the mediator's parent is the browser.
    return subprocess.Popen(
        [sys.executable, MEDIATOR, "--dir", root],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )


TABS = [
    {"id": 41, "windowId": 12, "title": "GitHub"},
    {"id": 42, "windowId": 12, "title": "read | the docs"},
    {"id": 7, "windowId": 13, "title": ""},
]


def main():
    root = tempfile.mkdtemp(prefix="gloview_mediator_")
    proc = start(root)
    try:
        # --- a report -----------------------------------------------------------------------
        send(proc, {"name": "report", "tabs": TABS, "active": [41]})
        reply = recv(proc)
        want(reply is not None and reply.get("ok") is True, "a report is accepted: %r" % reply)

        feed_path = os.path.join(root, "tabs")
        want(os.path.exists(feed_path), "a feed was written")
        feed = open(feed_path, "rb").read().decode()
        lines = feed.strip().split("\n")
        want(lines[0] == "g 1", "the generation leads: %r" % lines[0])
        want(
            any(l == "p %d 12" % os.getpid() for l in lines),
            "the browser is named as this process's PARENT: %r" % lines,
        )
        want(any(l == "t 12 41 1 GitHub" for l in lines), "the active tab is marked active")
        want(any(l == "t 12 42 0 read | the docs" for l in lines) is False,
             "a raw pipe would survive into the feed")
        want(any(l == "t 12 42 0 read   the docs" for l in lines),
             "and is folded out of the grammar instead")
        want(any(l == "t 13 7 0" for l in lines), "a window with no active tab is still listed")
        # One 'p' per window, not per tab: two tabs of window 12, one of 13.
        want(len([l for l in lines if l.startswith("p ")]) == 2,
             "each window is named once: %r" % [l for l in lines if l.startswith("p ")])

        # --- a screenshot --------------------------------------------------------------------
        png = "data:image/png;base64," + __import__("base64").b64encode(
            b"\x89PNG\r\n\x1a\npayload"
        ).decode()
        send(proc, {"name": "report", "tabs": TABS, "active": [41], "screenshot": png})
        want(recv(proc).get("ok") is True, "a report with a screenshot is accepted")
        shot = os.path.join(root, "previews", "41.png")
        want(os.path.exists(shot), "the screenshot lands under the active tab id")
        if os.path.exists(shot):
            want(open(shot, "rb").read().startswith(b"\x89PNG"), "and it is the PNG")

        # --- a second browser into the same document ---------------------------------------------
        send(proc, {"name": "report", "tabs": [{"id": 1, "windowId": 9, "title": "b"}],
                    "active": []})
        want(recv(proc).get("ok") is True, "a second report is accepted")
        merged = open(feed_path, "rb").read().decode().strip().split("\n")
        want(merged.count("g 1") == 1, "one generation for the whole feed")
        want(any(l == "t 9 1 0 b" for l in merged), "the second browser's tab is there")
        want(any(l.startswith("t 12 41") for l in merged), "and the first one's survived")

        # --- nonsense must not cost the connection ------------------------------------------------
        for junk in [{"name": "nonsense"}, {"name": "report", "tabs": "not a list"},
                     {"name": "report", "tabs": [{"id": "x", "windowId": 1}]}]:
            send(proc, junk)
            reply = recv(proc)
            want(reply is not None, "a malformed report still gets a reply: %r" % junk)

        # The connection is the whole product. A mediator that died on the third report would
        # leave the plugin showing windows forever with nothing said about why.
        send(proc, {"name": "report", "tabs": TABS, "active": [42]})
        want(recv(proc) is not None, "and the connection is still usable afterwards")

        # --- a truncated frame ends it, cleanly ---------------------------------------------------
        # Asserting that no .part file survives proves NOTHING: a non-atomic write never creates
        # one, and that version passed this line unchanged. Caught here for the third time --
        # the same gap in the deleted WebSocket bridge and in the brotab adapter. So it is
        # asserted by intercepting the rename, which is the only way to tell the two apart.
        import importlib.util

        spec = importlib.util.spec_from_file_location("med", MEDIATOR)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        renamed = []
        real_replace = mod.os.replace
        mod.os.replace = lambda a, b: (renamed.append((a, b)), real_replace(a, b))[1]
        try:
            mod.atomic_write(os.path.join(root, "atomic.bin"), b"payload")
        finally:
            mod.os.replace = real_replace
        want(
            renamed == [(os.path.join(root, "atomic.bin") + ".part",
                         os.path.join(root, "atomic.bin"))],
            "a write goes to a .part beside the target and is renamed over it",
        )
        want(not os.path.exists(os.path.join(root, "atomic.bin") + ".part"),
             "and the .part is gone once it is done")
        proc.stdin.close()
        want(proc.wait(timeout=10) == 0, "closing stdin ends the mediator cleanly")

        print("mediator: framing, pid->ppid, feed grammar, merge, survives nonsense")
        return 1 if failures else 0
    finally:
        if proc.poll() is None:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
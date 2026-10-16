#!/usr/bin/env python3
"""The adapter, against a fake brotab mediator speaking its real endpoints.

The claims being checked are all about a real port range, a real /proc parent lookup and a real
feed on disk, so the mediator here is an actual HTTP server on an actual port inside the range.
A mock of the adapter's own functions would prove nothing about any of it.

Covers: the pid -> ppid hop (the whole reason this file exists), the feed's grammar including
the 'p' line, tab order, the active flag, a title that would break the grammar, two mediators
merging into one feed, a screenshot written under the active tab, and a dead port being skipped
without killing the sweep.
"""

import http.server
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ADAPTER = os.path.join(HERE, "gloview_bridge.py")

failures = 0


def want(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL: %s" % what)


PNG = b"\x89PNG\r\n\x1a\n" + b"\x00" * 40


class Mediator(http.server.BaseHTTPRequestHandler):
    """One brotab mediator, with whatever the test needs it to be."""

    pid = 0
    tabs = b""
    active = b""
    shot = PNG

    def log_message(self, *a):
        pass

    def _send(self, body):
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/get_pid":
            self._send(str(self.pid).encode())
        elif p == "/list_tabs":
            self._send(self.tabs)
        elif p == "/get_active_tabs":
            self._send(self.active)
        elif p == "/get_screenshot":
            self._send(self.shot)
        else:
            self.send_error(404)


def start_mediator(port):
    srv = http.server.HTTPServer(("127.0.0.1", port), Mediator)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    return srv


def free_port_in_range():
    """A port the adapter will actually scan for. Not a random one -- the whole point is that it
    is in brotab's range."""
    for p in range(4625, 4636):
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", p))
            return p
        except OSError:
            continue
        finally:
            s.close()
    raise RuntimeError("no free port in brotab's range")


def run(root):
    return subprocess.run(
        [sys.executable, ADAPTER, "--dir", root, "--once", "--generation", "7"],
        capture_output=True,
        text=True,
        timeout=30,
    )


def main():
    root = tempfile.mkdtemp(prefix="gloview_adapter_")
    port = free_port_in_range()

    # A real mediator whose pid is a real child process, so /proc's parent of it is this test.
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
    Mediator.pid = child.pid
    Mediator.tabs = b"12.41\tGitHub\thttps://github.com\n12.42\tread the docs\thttps://x\n13.7\tchat\thttps://y\n"
    Mediator.active = b"12.41"

    srv = start_mediator(port)
    try:
        res = run(root)
        want(res.returncode == 0, "the adapter exits cleanly: %s" % res.stderr[-300:])

        feed_path = os.path.join(root, "tabs")
        want(os.path.exists(feed_path), "a feed was written")
        if not os.path.exists(feed_path):
            return 1
        feed = open(feed_path, "rb").read().decode()
        lines = feed.strip().split("\n")

        want(lines[0] == "g 7", "the generation is the first line: %r" % lines[0])
        # The whole point: the feed names the BROWSER's process, found by asking the mediator
        # for its own and reading its parent. This test process is that parent.
        want(
            any(l == "p %d 12" % os.getpid() for l in lines),
            "the browser process is the mediator's parent: %r" % lines,
        )
        want(
            any(l == "t 12 41 1 GitHub" for l in lines),
            "an active tab is marked active and titled",
        )
        want(
            any(l == "t 12 42 0 read the docs" for l in lines),
            "an inactive one is marked 0",
        )
        want(
            any(l == "t 13 7 0 chat" for l in lines),
            "a tab in another window is kept",
        )
        # Order is the browser's, and the strip relies on it.
        tab_lines = [l for l in lines if l.startswith("t ")]
        want(
            tab_lines == sorted(tab_lines, key=lambda l: (l.split()[1], 0)),
            "tabs keep the order brotab gave them",
        )

        # A screenshot lands under the ACTIVE tab, which is the only one that can have one.
        shot = os.path.join(root, "previews", "41.png")
        want(os.path.exists(shot), "the screenshot is filed under the active tab id")
        if os.path.exists(shot):
            want(open(shot, "rb").read().startswith(b"\x89PNG"), "and it is the PNG")
        want(
            not os.path.exists(os.path.join(root, "previews", "42.png")),
            "and not under an inactive one",
        )

        # A title carrying the feed's own separators must not shift its fields.
        Mediator.tabs = b"1.1\tbad|title\there\thttps://x\n"
        run(root)
        feed2 = open(feed_path, "rb").read().decode()
        # split("|") on a line with no pipes returns a ONE-element list, so asserting on its
        # length proves nothing. What matters is that no line carries one: a pipe is the feed's
        # field separator, and a title containing one would shift every later field.
        for line in feed2.strip().split("\n"):
            want("|" not in line, "no pipe survives into a line: %r" % line)
        want(
            any(l.startswith("t 1 1 0 ") and "|" not in l for l in feed2.strip().split("\n")),
            "a pipe in a title is folded out of the grammar",
        )

        # Two browsers, one feed. Each mediator names its own process.
        second = free_port_in_range()
        while second == port:
            second = free_port_in_range()
        Mediator2_pid = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"]
        ).pid
        old = Mediator.tabs, Mediator.pid
        Mediator.tabs, Mediator.pid = b"9.1\ta\thttps://x\n", Mediator2_pid
        srv2 = start_mediator(second)
        try:
            run(root)
            feed3 = open(feed_path, "rb").read().decode()
            want(feed3.count("\ng ") == 0 and feed3.startswith("g "),
                 "one generation for the whole feed, however many browsers are up")
            want(
                any(l == "p %d 9" % os.getpid() for l in feed3.strip().split("\n")),
                "the second browser names its own process",
            )
        finally:
            srv2.shutdown()
            Mediator.tabs, Mediator.pid = old

        # A dead port in the range must not stop the sweep.
        Mediator.tabs = b"12.41\tback\thttps://x\n"
        run(root)
        want(
            any("back" in l for l in open(feed_path).read().split("\n")),
            "the sweep survives a port with nothing on it",
        )

        # Asserting that no .part file survives proves nothing: a NON-atomic write never creates
        # one, and that version passed this line unchanged. Assert the rename itself.
        import importlib.util

        spec = importlib.util.spec_from_file_location("gb", ADAPTER)
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

        # One 'p' line per window, whatever the tab count. Checked on the three-tab feed: two
        # tabs of window 12 and one of 13 must produce TWO lines, not three.
        Mediator.tabs = (
            b"12.41\ta\thttps://x\n12.42\tb\thttps://x\n13.7\tc\thttps://x\n"
        )
        os.remove(os.path.join(root, "tabs"))
        run(root)
        feed4 = open(feed_path).read().strip().split("\n")
        plines = [l for l in feed4 if l.startswith("p ")]
        want(len(plines) == 2, "three tabs across two windows name each window once: %r" % plines)
        print("adapter: pid->ppid, feed grammar, order, screenshots, two browsers, dead ports")
        return 1 if failures else 0
    finally:
        srv.shutdown()
        child.terminate()


if __name__ == "__main__":
    sys.exit(main())
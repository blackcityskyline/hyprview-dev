#!/usr/bin/env python3
"""The bridge, against a real client speaking real WebSocket frames.

Not a smoke test. Every claim the plugin makes about what arrives on disk is a claim about this
file's output, so the handshake, the framing, the masking a client must apply, the path built
from a peer's numbers and the atomicity of the write are all exercised here. A bridge that
"looks right" and writes `/etc/passwd` because it concatenated a filename out of whatever
arrived is exactly the failure this needs to catch.
"""

import base64
import hashlib
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE = os.path.join(HERE, "bridge.py")
GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

failures = 0


def want(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL: %s" % what)


def client_frame(opcode, payload):
    """A CLIENT frame, which must be masked. Sending unmasked is the mistake this catches."""
    header = bytes([0x80 | opcode])
    n = len(payload)
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", n)
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return header + mask + masked


def try_send(sock, frame):
    """A peer that drops the connection has still refused the frame.

    Sending used to raise straight out of the test, and everything after it silently never
    ran -- so a bridge that CRASHED on a traversal reported success. Refusal by disconnect is
    a refusal; the assertion belongs on the filesystem, not on the socket surviving.
    """
    try:
        sock.sendall(frame)
        return True
    except OSError:
        return False


def recv_frame(sock):
    head = sock.recv(2)
    if len(head) < 2:
        return None, None
    length = head[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", sock.recv(8))[0]
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            break
        payload += chunk
    return head[0] & 0x0F, payload


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    port = free_port()
    root = tempfile.mkdtemp(prefix="gloview_bridge_")
    proc = subprocess.Popen(
        [sys.executable, BRIDGE, "--port", str(port), "--dir", root],
        stderr=subprocess.PIPE,
    )
    try:
        # Wait for the listener rather than sleeping a guessed interval: a fixed sleep either
        # wastes time or flakes, and both have cost real sessions.
        for _ in range(100):
            try:
                sock = socket.create_connection(("127.0.0.1", port), 0.2)
                break
            except OSError:
                time.sleep(0.05)
        else:
            print("FAIL: bridge never listened")
            return 1

        # --- the handshake -----------------------------------------------------------------
        key = base64.b64encode(os.urandom(16))
        sock.sendall(
            b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
            b"Connection: Upgrade\r\nSec-WebSocket-Key: " + key + b"\r\n"
            b"Sec-WebSocket-Version: 13\r\n\r\n"
        )
        resp = b""
        while b"\r\n\r\n" not in resp:
            resp += sock.recv(4096)
        want(b"101" in resp.split(b"\r\n")[0], "the handshake is answered with 101")
        expected = base64.b64encode(hashlib.sha1(key + GUID).digest())
        want(expected in resp, "Sec-WebSocket-Accept is the right digest")

        # --- the feed, verbatim -------------------------------------------------------------
        feed = "g 7\nt 12 41 1 GitHub\nt 12 42 0 read the docs\n"
        sock.sendall(client_frame(0x1, feed.encode("utf-8")))
        tabspath = os.path.join(root, "tabs")
        for _ in range(100):
            if os.path.exists(tabspath):
                break
            time.sleep(0.05)
        want(os.path.exists(tabspath), "the text frame lands as the feed")
        if os.path.exists(tabspath):
            got = open(tabspath, "rb").read()
            want(got == feed.encode("utf-8"), "byte for byte, including the UTF-8 title")

        # --- a picture, and the path built out of a peer's number --------------------------------
        png = b"\x89PNG\r\n\x1a\n" + os.urandom(64)
        sock.sendall(client_frame(0x2, b"icon 42\n" + png))
        icon = os.path.join(root, "icons", "tab-42.png")
        for _ in range(100):
            if os.path.exists(icon):
                break
            time.sleep(0.05)
        want(os.path.exists(icon), "an icon lands under icons/tab-<id>.png")
        if os.path.exists(icon):
            want(open(icon, "rb").read() == png, "with its bytes intact")

        sock.sendall(client_frame(0x2, b"preview 41\n" + png))
        preview = os.path.join(root, "previews", "41.png")
        for _ in range(100):
            if os.path.exists(preview):
                break
            time.sleep(0.05)
        want(os.path.exists(preview), "a screenshot lands under previews/<id>.png")

        # --- refusals -----------------------------------------------------------------------
        # The paths checked afterwards are the paths a BAD id would actually produce, resolved
        # the way the filesystem would: "icons/../escape.png" lands beside icons/, not under it.
        # The first version of this checked "icons/..-escape.png", which nothing ever writes,
        # so the traversal check passed with the guard removed.
        escapes = []
        for bad in [
            b"icon ../escape\n",
            b"icon ../../escape2\n",
            b"icon sub/dir\n",
            b"icon .\n",
            b"icon 1/../2\n",
        ]:
            try_send(sock, client_frame(0x2, bad + png))
            kind, ident = bad.decode().split()[0], bad.decode().split()[1]
            escapes.append(os.path.normpath(os.path.join(root, "icons", ident + ".png")))
            escapes.append(os.path.normpath(os.path.join(root, "previews", ident + ".png")))
        time.sleep(0.3)
        for path in escapes:
            want(not os.path.exists(path),
                 "refused: %s" % os.path.relpath(path, root))
        want(not os.path.exists("/tmp/escape.png"), "and nothing outside the root")

        for bad, why in [
            (b"icon ../escape\n" + png, "a path traversal in the id"),
            (b"icon -1\n" + png, "a negative id"),
            (b"bogus 42\n" + png, "an unknown kind"),
            (b"icon 42 extra\n" + png, "extra fields in the header"),
            (b"no-newline" + png, "a header with no newline"),
        ]:
            try_send(sock, client_frame(0x2, bad))
        time.sleep(0.3)
        # NOT a case: tab id 0. It is a valid id -- entries_check pins that a tab with id 0 is
        # a tab and not a missing one -- so the bridge refuses nothing about it. Deciding which
        # ids mean something is the parser's question, not the one that builds a filename.
        for path in [
            os.path.join(root, "icons", "..-escape.png"),
            "/etc/escape.png",
            os.path.join(root, "icons", "tab--1.png"),
        ]:
            want(not os.path.exists(path), "refused: %s" % os.path.basename(path))
        want(
            not os.path.exists(os.path.join(root, "bogus")),
            "an unknown kind writes nothing",
        )

        # --- atomicity ---
        # Reconnect first: a crash during the refusal checks above may have dropped it, and
        # the rest of this file has to actually run.--------------------------------------------------------------------
        # Asserting that no .part file is left behind proves nothing: a NON-atomic write never
        # creates one, and that version passed this check unchanged. What has to be asserted is
        # the rename itself, so it is intercepted rather than inferred from the aftermath.
        import importlib.util

        spec = importlib.util.spec_from_file_location("bridge", BRIDGE)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        import socket as _socket

        try:
            sock.close()
        except OSError:
            pass
        sock = _socket.create_connection(("127.0.0.1", port), 2)
        key2 = base64.b64encode(os.urandom(16))
        sock.sendall(
            b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
            b"Connection: Upgrade\r\nSec-WebSocket-Key: " + key2 + b"\r\n\r\n"
        )
        r2 = b""
        while b"\r\n\r\n" not in r2:
            r2 += sock.recv(4096)
        want(b"101" in r2.split(b"\r\n")[0], "the bridge still serves a second peer")

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
        want(
            not os.path.exists(os.path.join(root, "atomic.bin") + ".part"),
            "and the .part is gone once it is done",
        )

        sock.close()
        print("bridge: handshake, feed, pictures, refusals, atomicity")
        return 1 if failures else 0
    finally:
        proc.terminate()
        try:
            proc.wait(3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
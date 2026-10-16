#!/usr/bin/env python3
"""The bridge between the browser extension and the compositor plugin.

The extension cannot write files, and neither of the two APIs that could make it do so is
available on both engines. So it dials this process over a WebSocket and this process is what
touches the disk. The plugin reads the file and never learns any of this existed.

Written against the standard library alone. A WebSocket server for a localhost peer sending a
handful of small frames is a SHA-1 handshake and a frame loop; taking a dependency for that
would add an install step to something that has to work on a machine where nothing is
installed yet.

Layout, all under --dir (default $XDG_RUNTIME_DIR/gloview):

    tabs                   the feed the plugin parses
    previews/<id>.png      a screenshot of the active tab of some window
    icons/tab-<id>.png     a tab's site icon

Every write is atomic -- written beside the target and renamed over it. The plugin reads these
while they change, and a reader that catches a file mid-write sees a picture cairo refuses and
a feed the parser rejects. That degrades to "nothing new" for one frame, which is survivable,
but the rename makes the question not arise, and it costs one syscall.
"""

import argparse
import base64
import hashlib
import os
import socket
import struct
import sys

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x1, 0x2, 0x8, 0x9, 0xA


def log(msg):
    sys.stderr.write("bridge: %s\n" % msg)
    sys.stderr.flush()


def atomic_write(path, data):
    """Write beside the target, then rename. Never a half-written file in sight."""
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def handshake(conn):
    """Answer the HTTP upgrade. Returns False if this was not a WebSocket request."""
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return False
        data += chunk
        if len(data) > 65536:
            return False

    key = None
    for line in data.split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"sec-websocket-key":
            key = value.strip()
    if key is None:
        conn.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
        return False

    accept = base64.b64encode(hashlib.sha1(key + GUID).digest())
    conn.sendall(
        b"HTTP/1.1 101 Switching Protocols\r\n"
        b"Upgrade: websocket\r\n"
        b"Connection: Upgrade\r\n"
        b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n"
    )
    return True


def read_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def read_frame(conn):
    """(opcode, payload) or None when the peer went away.

    Handles fragmentation by returning continuation frames under their own opcode; the peer
    here sends none, and pretending otherwise would be a claim this cannot back up.
    """
    head = read_exact(conn, 2)
    if head is None:
        return None
    fin = head[0] & 0x80
    opcode = head[0] & 0x0F
    masked = head[1] & 0x80
    length = head[1] & 0x7F

    if length == 126:
        ext = read_exact(conn, 2)
        if ext is None:
            return None
        length = struct.unpack(">H", ext)[0]
    elif length == 127:
        ext = read_exact(conn, 8)
        if ext is None:
            return None
        length = struct.unpack(">Q", ext)[0]

    # A frame larger than a screenshot could not be a screenshot. Bounded so a bad length from
    # a confused peer cannot ask this process for all the memory it has.
    if length > 64 * 1024 * 1024:
        return None

    mask = read_exact(conn, 4) if masked else b""
    if masked and mask is None:
        return None
    payload = read_exact(conn, length) if length else b""
    if payload is None:
        return None

    if masked:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

    if not fin:
        return opcode, payload
    return opcode, payload


def send_frame(conn, opcode, payload=b""):
    """Server frames are never masked."""
    header = bytes([0x80 | opcode])
    n = len(payload)
    if n < 126:
        header += bytes([n])
    elif n < 65536:
        header += bytes([126]) + struct.pack(">H", n)
    else:
        header += bytes([127]) + struct.pack(">Q", n)
    conn.sendall(header + payload)


def sanitize(name):
    """A path built from a peer's number. Anything else is refused rather than cleaned."""
    return name.isdigit() and 0 < len(name) <= 19


def handle_frame(opcode, payload, root):
    """What one frame means. Raises OSError on anything it cannot write, and the caller drops
    the frame rather than the connection."""
    if opcode == OP_TEXT:
        atomic_write(os.path.join(root, "tabs"), payload)
        return

    if opcode != OP_BINARY:
        return

    # "<kind> <id>\n" then the bytes. The id goes into a filename, so it is checked rather
    # than escaped: this peer is on localhost, and a path built by string concatenation out of
    # its numbers is a path nobody chose to open.
    header, sep, body = payload.partition(b"\n")
    if not sep:
        raise OSError("binary frame with no header")
    parts = header.decode("utf-8", "replace").split()
    if len(parts) != 2:
        raise OSError("header is not two fields")
    kind, ident = parts
    if not sanitize(ident):
        raise OSError("id is not a plain number: %r" % ident)
    if kind == "preview":
        target = os.path.join(root, "previews", ident + ".png")
    elif kind == "icon":
        target = os.path.join(root, "icons", "tab-" + ident + ".png")
    else:
        raise OSError("unknown kind: %r" % kind)
    atomic_write(target, body)


def serve(conn, root):
    if not handshake(conn):
        return
    log("connected")
    try:
        while True:
            frame = read_frame(conn)
            if frame is None:
                return
            opcode, payload = frame

            if opcode == OP_CLOSE:
                return
            if opcode == OP_PING:
                send_frame(conn, OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                continue

            # One bad frame must not cost the whole connection. It used to: a malformed id
            # raised out of serve(), the peer lost its socket, and the feed stopped arriving
            # until the extension reconnected -- a bug in the extension silently becoming a
            # dead plugin, with a crash as the only evidence.
            try:
                handle_frame(opcode, payload, root)
            except OSError as e:
                log("dropped a frame: %s" % e)
    finally:
        log("disconnected")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=27173)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument(
        "--dir",
        default=os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "gloview"),
    )
    args = ap.parse_args()

    # Loopback only. This process writes into a directory the plugin reads and there is no
    # reason for it to be reachable from the network; a browser on the same machine is the
    # whole audience.
    if args.host not in ("127.0.0.1", "localhost"):
        sys.exit("bridge: refusing to listen on anything but loopback")

    for sub in ("", "previews", "icons"):
        os.makedirs(os.path.join(args.dir, sub), exist_ok=True)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(4)
    log("listening on %s:%d, writing %s" % (args.host, args.port, args.dir))

    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            serve(conn, args.dir)
        except (ConnectionError, OSError):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass


if __name__ == "__main__":
    main()
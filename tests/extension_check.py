#!/usr/bin/env python3
"""The extension's two rules, which are invisible from the plugin side.

1. Every page must be reachable. A tab without the host permission gets no picture, and the
   card falls back to the title -- so a missing <all_urls> is a card with a grey plate on it,
   for every tab but the focused one.

2. The canvas is painted BEFORE the shot and unpainted after. The browser's snapshot draws the
   document but not the stylesheets an extension added, so a page with its own background comes
   back as a white sheet unless the canvas is painted first. The order is the rule: paint,
   shoot, unpaint.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXT = os.path.join(ROOT, "extension")

failures = 0


def want(ok, what):
    global failures
    if not ok:
        print("FAIL: %s" % what, file=sys.stderr)
        failures += 1


def body_of(text, header):
    start = text.find(header)
    if start < 0:
        return ""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return ""


def main():
    # (1) the manifests
    for name in ("manifest.json", "manifest.chromium.json", "manifest.gecko.json"):
        path = os.path.join(EXT, name)
        if not os.path.exists(path):
            want(False, "%s is missing" % name)
            continue
        with open(path) as f:
            manifest = json.load(f)
        perms = manifest.get("permissions", [])
        want("<all_urls>" in perms,
             "%s does not ask for <all_urls> -- a tab without it gets no picture" % name)

    # (2) the order inside the capture
    path = os.path.join(EXT, "background.js")
    if not os.path.exists(path):
        print("FAIL: extension/background.js is missing", file=sys.stderr)
        return 1
    with open(path) as f:
        js = f.read()

    want("function paintCanvas" in js, "background.js has no paintCanvas")
    want("function unpaintCanvas" in js, "background.js has no unpaintCanvas")

    # The canvas is a background, not a colour: a gradient or a photographed one is the same
    # omission, and copying only the colour leaves it flat.
    canvas = body_of(js, "function paintCanvas(")
    want("backgroundImage" in canvas,
         "paintCanvas copies only the colour -- a canvas that is an image comes back flat")

    capture = body_of(js, "async function capture(")
    want(capture != "", "background.js has no capture function")
    if capture:
        paint = capture.find("paintCanvas")
        shoot = capture.find("captureTab")
        unpaint = capture.find("unpaintCanvas")
        want(paint >= 0 and shoot >= 0 and paint < shoot,
             "the canvas is not painted before the shot -- the page comes back as a white sheet")
        want(unpaint >= 0 and unpaint > shoot,
             "the canvas is not unpainted after the shot -- the page keeps the painted layer")

    # (3) EVERY tab is photographed, not only the one on screen. A tab that was never active
    # has no picture at all, so most cards came up as a title on a plate.
    want("api.tabs.captureTab" in js,
         "background.js never calls tabs.captureTab -- only the visible tab gets a picture")
    queue = body_of(js, "function queueShots(")
    want(queue != "", "background.js has no shot queue: one picture per message, tab by tab")
    want("canCaptureTab" in queue and "active.includes" in queue,
         "the queue does not fall back to the visible tab on a browser without captureTab")

    # (4) the report NAMES the tab its picture belongs to. Writing it under every active id
    # put one tab's page on another tab's card.
    report = body_of(js, "async function reportOnce(")
    want("screenshotTab" in report,
         "the report does not name the tab its picture belongs to")

    # (5) the close verb: the compositor asks the browser to close a tab, because a tab is
    # not a window the compositor owns. Without it the card's x does nothing at all.
    want("api.tabs.remove(" in js, "background.js has no close verb -- a tab card's x does nothing")
    want("reply.name === 'close'" in js,
         "the close request is never answered by the extension")

    # (6) the unload verb: the tab stays, its page leaves memory. Discarding is the browser's,
    # so a reply nobody answers leaves the row looking like it worked.
    want("api.tabs.discard(" in js, "background.js has no unload verb -- an unload row does nothing")
    want("reply.name === 'unload'" in js,
         "the unload request is never answered by the extension")

    if failures:
        print("extension_check: %d failure(s)" % failures, file=sys.stderr)
        return 1
    print("the extension photographs every tab and paints the canvas around each shot")
    return 0


if __name__ == "__main__":
    sys.exit(main())

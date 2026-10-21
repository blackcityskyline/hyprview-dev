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

    capture = body_of(js, "async function capture(")
    want(capture != "", "background.js has no capture function")
    if capture:
        paint = capture.find("paintCanvas")
        shoot = capture.find("api.tabs.captureVisibleTab(windowId")
        unpaint = capture.find("unpaintCanvas")
        want(paint >= 0 and shoot >= 0 and paint < shoot,
             "the canvas is not painted before the shot -- the page comes back as a white sheet")
        want(unpaint >= 0 and unpaint > shoot,
             "the canvas is not unpainted after the shot -- the page keeps the painted layer")

    if failures:
        print("extension_check: %d failure(s)" % failures, file=sys.stderr)
        return 1
    print("the extension asks for every page and paints the canvas around the shot")
    return 0


if __name__ == "__main__":
    sys.exit(main())

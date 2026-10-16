# GloView tabs

Our own browser extension, one implementation for every Gecko and Chromium browser.

Not CDP (Chromium only) and not BiDi (Chromium only, Firefox partial) — either would leave
half the browsers unable to report at all. The WebExtensions subset both implement is what makes
"all of them" true, and `background.js` never names a namespace: it picks `browser` or `chrome`
at load.

## Why there is a mediator at all

An extension cannot write files: `downloads` prompts and will not overwrite, and the File System
Access API does not exist in Gecko. So it reports to a separate process over **native
messaging**, and the mediator is what touches the disk. Native messaging accepts an interpreter
and a script — no compiled helper anywhere, which was a requirement.

The mediator earns its existence by one more thing, and it is the part that makes the feature
possible at all:

    An extension cannot report the browser's process id. The mediator can, because the browser
    started it — its parent is the browser.

A pid is needed because a browser appends its own name to a window's title. What the compositor
holds for a window is `(page) — YouTube — Zen Browser`; what the extension can report for its
active tab is `(page) — YouTube`. Different strings by construction, on every browser that does
it, so a title cannot match a browser window to a compositor window, and matching by prefix is
the exact guess that files one browser's tabs under another's window.

A compositor window carries the same pid as its browser, so the match is exact.

## Install

| Engine | Manifest | Native messaging manifest goes to |
|---|---|---|
| Chromium, Chrome, Brave, Vivaldi, Edge | `manifest.chromium.json` | `~/.config/chromium/NativeMessagingHosts/` |
| Firefox, Zen, LibreWolf, Floorp, Waterfox | `manifest.gecko.json` | `~/.mozilla/native-messaging-hosts/` |

1. Copy this directory somewhere permanent and copy the right manifest over `manifest.json`.
2. Put `path` in the native messaging manifest to the **absolute** path of `bridge/mediator.py`,
   and set `allowed_extensions` to the extension's id:
   - Chromium MV3: the `key` from the installed extension's page, base64, on one line
   - Gecko: `gloview@localhost`
3. Make the mediator executable: `chmod +x bridge/mediator.py`
4. Load it as an unpacked / temporary extension:
   - `chrome://extensions` → developer mode → load unpacked
   - `about:debugging#/runtime/this-firefox` → load temporary add-on → pick `manifest.json`

## Permissions

| Permission | Why | Without it |
|---|---|---|
| `tabs` | tab ids, window ids and titles | no cards at all |
| `nativeMessaging` | reaching the mediator | nothing is reported |
| `activeTab` | screenshot in Chromium | no screenshot there |
| `<all_urls>` (optional) | screenshot in **Firefox MV3** | no screenshot anywhere in Firefox |

The last is the thing to know. Firefox will not hand an extension a screenshot of a page until
the user grants `<all_urls>`, and that is not negotiable. So in Firefox an active tab shows its
caption with no picture until the grant is made — which is a supported state, not a failure.

## What it sends

One message: the tab list, which tabs are active in the focused window, and at most one
screenshot. The mediator writes:

    tabs                   g <generation> / p <pid> <window> / t <window> <tab> <active> <title>
    previews/<tabId>.png

`p` is the **parent** of the mediator — the browser. That single line is the whole correlation.

## What it does not do

- It opens no sockets. It talks to one local process over stdio and never listens.
- It sends no URLs to the mediator, no history, no cookies.
- It reports nothing from a private window unless the extension is granted there, which it is
  not.
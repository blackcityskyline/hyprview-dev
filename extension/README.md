# HyprView tabs

One WebExtensions implementation for Gecko and Chromium. `background.js` never
names a namespace: it picks `browser` or `chrome` at load. CDP and BiDi were
rejected because each covers only half the browsers.

## Why a mediator

An extension cannot write files: `downloads` prompts and will not overwrite, and
the File System Access API does not exist in Gecko. So it reports over native
messaging to `bridge/mediator.py`, which touches the disk and supplies the one
thing the extension cannot: the browser's pid. The mediator's parent process is
the browser, and the compositor window carries the same pid, so the match is
exact. A title cannot be the key — the browser appends its own name to the
window title (AGENTS.md 2.26).

## Install

`hyprview.xpi` in the repository root is the packaged Gecko build.

| Engine | Manifest | Native messaging manifest goes to |
|---|---|---|
| Chromium, Chrome, Brave, Vivaldi, Edge | `manifest.chromium.json` | `~/.config/chromium/NativeMessagingHosts/` |
| Firefox, Zen, LibreWolf, Floorp, Waterfox | `manifest.gecko.json` | `~/.mozilla/native-messaging-hosts/` |

1. Install `hyprview.xpi`, or load this directory unpacked with the right
   manifest copied over `manifest.json`.
2. Put the **absolute** path of `bridge/mediator.py` in the native messaging
   manifest and set `allowed_extensions` to the extension's id:
   - Gecko: `hyprview@localhost`
   - Chromium MV3: the `key` from the installed extension's page, base64, on one line
3. Make the mediator executable: `chmod +x bridge/mediator.py`.
4. Load it as an unpacked / temporary extension:
   - `chrome://extensions` → developer mode → load unpacked
   - `about:debugging#/runtime/this-firefox` → load temporary add-on → pick `manifest.json`

A temporary add-on in a release Firefox build cannot open a native-messaging
port; that needs the installed XPI.

5. Optional, Firefox and Zen: copy the `zen-mod/` directory to
   `<profile>/chrome/sine-mods/hyprview-previews/` and restart. It fills the
   browser's thumbnail store for tabs the extension cannot photograph, but it
   runs only under a chrome-JS mod loader (Sine); Zen's own mods are CSS only.

Each report carries the tab's address as well as its title, because the mediator
finds a background tab's picture by hashing that address against the browser's
own thumbnail store. The address is hashed and never written to disk.

## Permissions

| Permission | Why | Without it |
|---|---|---|
| `tabs` | tab ids, window ids and titles | no cards at all |
| `nativeMessaging` | reaching the mediator | nothing is reported |
| `activeTab` | screenshot in Chromium | no screenshot there |
| `<all_urls>` | a picture of the visible tab | cards show a stored thumbnail or a caption |

Gecko hands out a screenshot only to an extension holding `<all_urls>`; `activeTab`
is granted by a click on the extension, and this one has no button. Chromium is
the other way round — its `activeTab` covers a service-worker extension — but the
permission is asked for on both so one manifest description is true everywhere.

## What it sends

One message: the tab list, which tabs are active in the focused window, and at
most one screenshot. The mediator writes:

    tabs                   g <generation> / p <pid> <window> / t <window> <tab> <flags> <title>
    previews/<tabId>.png

`flags` is one number: bit 0 active, bit 1 discarded (open, not in memory). The
compositor lists discarded tabs only when asked (`show_discarded_tabs`).

`p` is the **parent** of the mediator — the browser. That line is the whole
correlation.

## What it does

- Reports the tab list, the active tabs of the focused window, and at most one screenshot.
- **Opens a tab on request.** The compositor writes `a <pid> <window> <tab>` under
  `$XDG_RUNTIME_DIR/hyprview`; the mediator whose parent is that pid forwards it, and the
  extension makes the tab active (reloading it if it was discarded) and focuses its window.
  Nothing else ever asks the extension to do anything.

## What it does not do

- It opens no sockets. It talks to one local process over stdio and never listens.
- It sends no URLs to the mediator, no history, no cookies.
- It reports nothing from a private window unless the extension is granted there, which it is
  not.

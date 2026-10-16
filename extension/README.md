# GloView tabs — the browser side

One implementation for every Gecko and Chromium browser. That is possible because it uses only
the WebExtensions subset both implement, and because it never names a namespace: `background.js`
picks `browser` or `chrome` at load.

It is deliberately **not** CDP (Chromium only) and **not** BiDi (Chromium only, Firefox
partial). Both would have left half the browsers the user actually has unable to report.

## It cannot write files

Neither `downloads` (it prompts, and will not overwrite silently) nor the File System Access
API (absent from Gecko) can do that. So everything goes to a **bridge process** over a
WebSocket on `127.0.0.1:27173`, and the bridge is what touches the disk.

The plugin is not involved and does not know this exists — it reads a file. That is the seam
doing its job: the transport above it can be replaced without touching the compositor.

## Install

Both engines need the manifest chosen for them; the code is identical.

| Engine | Manifest |
|---|---|
| Chromium / Chrome / Brave / Vivaldi / Edge | `manifest.chromium.json` |
| Firefox, Zen, LibreWolf, Floorp, Waterfox | `manifest.gecko.json` |

Copy the directory somewhere permanent, copy the right manifest over `manifest.json`, and load
it as an unpacked extension:

- Chromium: `chrome://extensions` → developer mode → load unpacked
- Gecko: `about:debugging#/runtime/this-firefox` → load temporary add-on → pick `manifest.json`

## Permissions, and what each is for

| Permission | Why | Without it |
|---|---|---|
| `tabs` | tab titles, which is what the plugin captions a card with | cards fall back to the window title |
| `favicon` | the browser's own favicon endpoint, served from its cache | no icons on inactive tabs |
| `activeTab` | screenshot in Chromium | no screenshot there either |
| `<all_urls>` (optional) | screenshot in **Firefox MV3** | no screenshot anywhere in Firefox |

The last one is the thing worth knowing. Firefox will not hand an extension a screenshot of a
page until the user grants `<all_urls>`, and there is no way around it. So in Firefox an active
tab shows its icon, not a picture, until that grant is made — and that is why icons are not
optional and are fetched for every tab, rather than the screenshot being the only path.

## What it sends

| Channel | Payload |
|---|---|
| text frame | the whole `tabs` file, byte for byte |
| binary frame | `preview <tabId>\n` + PNG, `icon <tabId>\n` + PNG |

The text frame is exactly what `src/model/tab_feed.hpp` parses. There is no second format and
no translation step.

`generation` is bumped on every start and never rolled back: tab ids are reused, so an id is
only meaningful under the connection that issued it, and two connections must never share one.

## What it does not do

- It does not open sockets to anything but localhost. It dials out to the bridge; it never
  listens.
- It sends nothing but what a tile needs: which window, which tab, active or not, and a title.
- It never sends history, cookies, URLs beyond the favicon lookup the browser itself performs,
  or anything from a private window — `tabs.query` returns nothing for private windows unless
  the extension is allowed there, and it is not.
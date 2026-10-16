// GloView tab bridge — the browser side. One implementation, both engines.
//
// Works on Gecko and Chromium because it uses only the WebExtensions subset both
// implement, and because it never assumes a namespace: `browser` is Gecko's promise-based
// one, `chrome` is Chromium's, and they are close enough that everything below is written
// against whichever is present. That is the whole reason this is a WebExtension and not CDP
// (Chromium only) or BiDi (Chromium only, Firefox partial).
//
// It cannot write files. Neither `downloads` (it prompts, and it will not overwrite) nor the
// File System Access API (absent from Gecko) can do that, so it sends everything to a bridge
// process over a WebSocket on localhost, and the bridge is what touches the disk. The plugin
// reads a file and never learns any of this existed — which is the seam that was chosen for
// exactly this reason.
//
// Protocol, deliberately tiny and shared with the plugin's parser:
//
//   text frame   -> the whole contents of the `tabs` file, verbatim
//   binary frame -> "<kind> <id>\n" + bytes
//                   kind: "preview" (a screenshot of the active tab)
//                         "icon"    (the tab's site icon)
//                   id:   the tab id, which is what the filename is built from
//
// The text frame is byte-identical to what the plugin parses, so there is no second format
// and no translation step. Adding a field to the feed does not touch this file's counterpart.

const api = globalThis.browser ?? globalThis.chrome;

const BRIDGE = 'ws://127.0.0.1:27173';

// How the tab list is put together. The generation identifies this connection: tab ids are
// reused (close 41, 41 exists again on another page), so an id is only meaningful under the
// connection that issued it. The plugin treats a new generation as "everything before it is
// gone", which is why it is bumped on every start and never rolled back.
let generation = Date.now() % 0xffffffff;

let socket = null;
let reconnectIn = 250;

// A screenshot is worth several hundred kilobytes and a favicon a few hundred bytes; neither
// is worth sending on every event. This is the throttle, and it is the only state beyond the
// connection itself.
const QUIET_MS = 700;
let lastSend = 0;
let sendTimer = null;

// ---------------------------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------------------------

function connect() {
  try {
    socket = new WebSocket(BRIDGE);
  } catch (e) {
    return scheduleReconnect();
  }
  socket.binaryType = 'arraybuffer';
  socket.onopen = () => scheduleSend(0);
  socket.onclose = () => {
    socket = null;
    scheduleReconnect();
  };
  socket.onerror = () => {};
}

function scheduleReconnect() {
  // Doubling, capped: a bridge that is not running yet is the normal state on a fresh login,
  // and a fixed short delay would hammer it forever.
  reconnectIn = Math.min(reconnectIn * 2, 15000);
  setTimeout(connect, reconnectIn);
}

function scheduleSend(delay) {
  if (sendTimer !== null)
    clearTimeout(sendTimer);
  sendTimer = setTimeout(send, delay);
}

function send() {
  sendTimer = null;
  if (!socket || socket.readyState !== 1)
    return;
  lastSend = Date.now();
  collect().then((payload) => {
    if (!socket || socket.readyState !== 1)
      return;
    // The feed first, then the pictures. The plugin may read the feed between the two, and a
    // tab with no picture yet is a card with its caption and no image -- which is the ordinary
    // state of this directory, not an error.
    socket.send(payload.feed);
    for (const [kind, id, bytes] of payload.pictures)
      socket.send(new Uint8Array([...new TextEncoder().encode(`${kind} ${id}\n`), ...bytes]));
  });
}

async function collect() {
  const [windows, tabs] = await Promise.all([api.windows.getAll({}), api.tabs.query({})]);

  // Grouped by window id, which is what the plugin matches a browser window to a compositor
  // window by, and what a tab's GroupRef is.
  const byWindow = new Map();
  const pictures = [];

  for (const tab of tabs) {
    if (tab.id === undefined || tab.windowId === undefined)
      continue;
    if (!byWindow.has(tab.windowId))
      byWindow.set(tab.windowId, []);
    byWindow.get(tab.windowId).push(tab);

    const icon = await faviconBytes(tab);
    if (icon)
      pictures.push(['icon', tab.id, icon]);
  }

  // Only the ACTIVE tab of each window can ever have a screenshot -- browsers do not keep one
  // for an inactive tab, on any engine. Everything else is an icon or nothing.
  for (const window of windows) {
    const active = tabs.find((t) => t.windowId === window.id && t.active);
    if (!active || active.id === undefined)
      continue;
    const shot = await screenshotBytes(active.windowId);
    if (shot)
      pictures.push(['preview', active.id, shot]);
  }

  return { feed: buildFeed(byWindow), pictures };
}

function buildFeed(byWindow) {
  const lines = [`g ${generation}`];
  for (const [windowId, tabs] of byWindow) {
    for (const tab of tabs) {
      const title = (tab.title ?? '').replace(/[\r\n|]/g, ' ');
      lines.push(`t ${windowId} ${tab.id} ${tab.active ? 1 : 0} ${title}`);
    }
  }
  return lines.join('\n') + '\n';
}

// ---------------------------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------------------------

// The site icon, fetched through the extension's own favicon endpoint -- which both engines
// expose at the same path, under the same `favicon` permission, and which reads the browser's
// CACHE rather than the network. So an inactive tab's icon costs nothing and no favicon is ever
// re-requested from a site.
async function faviconBytes(tab) {
  if (!tab.url || !tab.url.startsWith('http'))
    return null;
  try {
    const url = api.runtime.getURL(
      `/_favicon/?pageUrl=${encodeURIComponent(tab.url)}&size=32`
    );
    const res = await fetch(url);
    if (!res.ok)
      return null;
    return new Uint8Array(await res.arrayBuffer());
  } catch (e) {
    return null;
  }
}

// A screenshot of what a window is showing right now.
//
// In Chromium this works with `activeTab` or a host permission. In Firefox MV3 it is
// `undefined` until the user grants the optional `<all_urls>` permission -- not a bug, a policy
// Firefox will not negotiate. So this can return nothing forever, and the card falls back to
// the icon, which is why the icon is not optional and comes first.
async function screenshotBytes(windowId) {
  try {
    if (typeof api.tabs.captureVisibleTab !== 'function')
      return null;
    const dataUrl = await api.tabs.captureVisibleTab(windowId, { format: 'png' });
    if (typeof dataUrl !== 'string' || !dataUrl.startsWith('data:image/png'))
      return null;
    const base64 = dataUrl.slice(dataUrl.indexOf(',') + 1);
    const bin = atob(base64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++)
      out[i] = bin.charCodeAt(i);
    return out;
  } catch (e) {
    return null;
  }
}

// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

// Every one of these funnels into the same throttled send. A browser firing tab events while
// the user switches five tabs should produce one feed a moment later, not five.
function changed() {
  if (socket && socket.readyState === 1) {
    const since = Date.now() - lastSend;
    scheduleSend(since >= QUIET_MS ? 0 : QUIET_MS - since);
  }
}

connect();

api.tabs.onCreated.addListener(changed);
api.tabs.onRemoved.addListener(changed);
api.tabs.onUpdated.addListener(changed);
api.windows.onRemoved.addListener(changed);
api.runtime.onStartup.addListener(changed);
api.runtime.onInstalled.addListener(changed);

// A new generation on every start, so a plugin that kept an old connection's ids cannot answer
// with them. Never rolled back: two connections must never share one.
api.runtime.onStartup.addListener(() => {
  generation = Date.now() % 0xffffffff;
});
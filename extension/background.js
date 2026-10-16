// GloView tabs — the browser side. Our own extension, one implementation for every Gecko and
// Chromium browser.
//
// It works on both because it uses only the WebExtensions subset both implement and because it
// never names a namespace: `browser` is Gecko's, `chrome` is Chromium's, and everything below
// is written against whichever exists. That is why this is not CDP (Chromium only) and not BiDi
// (Chromium only, Firefox partial) — either would leave half the user's browsers unable to
// report at all.
//
// It cannot write files, and neither can any extension: `downloads` prompts and will not
// overwrite, and the File System Access API does not exist in Gecko. So it reports to a mediator
// process over native messaging, and the mediator is what touches the disk. That is also what
// makes the pid trick available, which is the only reason a mediator exists at all:
//
//     An extension cannot ask for the browser's process id. The mediator can, because the
//     browser started it — its parent is the browser.
//
// And the pid is needed because a browser appends its own name to a window's title, so what the
// compositor holds for a window and what this can report for its active tab are different
// strings by construction. A title cannot match them; a process id matches exactly.
//
// The framing is what native messaging already specifies on stdio: a 4-byte native-endian
// length, then that many bytes of JSON. No port, no handshake, nothing to scan.

const api = globalThis.browser ?? globalThis.chrome;
const NATIVE_APP = 'gloview_mediator';

let port = null;

// Tab events arrive in bursts -- switching five tabs is five events -- and a compositor that
// paints at 60Hz wants a report that is not a frame behind. This is the only throttle in the
// file, and it is what makes a burst one report.
const QUIET_MS = 600;
let timer = null;

function connect() {
  try {
    port = api.runtime.connectNative(NATIVE_APP);
  } catch (e) {
    return;
  }
  port.onMessage.addListener((reply) => {
    if (reply && reply.ok === false)
      console.warn('gloview mediator refused:', reply.reason);
  });
  port.onDisconnect.addListener(() => {
    port = null;
    // The mediator is gone when the browser updates, when it crashes, or when it was never
    // installed. Retrying on a fixed short delay hammers a host that is not coming back.
    setTimeout(connect, 5000);
  });
  report();
}

// Everything the mediator needs, in one message. Asking for the window list too is deliberate:
// the pid is per-browser but the windows are per-profile, and the two are not always the same
// set once private windows are involved.
async function report() {
  if (!port) return;
  let tabs = [];
  let active = [];
  try {
    tabs = await api.tabs.query({});
    const focused = await api.windows.getLastFocused({});
    active = tabs
      .filter((t) => t.windowId === focused.id && t.active)
      .map((t) => t.id);
  } catch (e) {
    return;
  }
  if (active.length === 0) {
    // No focused tab means nothing is being shown, so there is nothing to photograph. Sending
    // an empty active list is correct and is not a failure state worth retrying.
    try {
      port.postMessage({ name: 'report', tabs: simplify(tabs), active: [] });
    } catch (e) {}
    return;
  }
  const screenshot = await capture(focused.id);
  try {
    port.postMessage({
      name: 'report',
      tabs: simplify(tabs),
      active,
      screenshot: screenshot || undefined,
    });
  } catch (e) {}
}

function simplify(tabs) {
  return tabs
    .filter((t) => typeof t.id === 'number' && typeof t.windowId === 'number')
    .map((t) => ({ id: t.id, windowId: t.windowId, title: t.title || '' }));
}

// A picture of what this window is showing right now.
//
// In Chromium this works with `activeTab`. In Firefox MV3 it is `undefined` until the user
// grants the optional `<all_urls>` permission, and there is no way to negotiate past that — so
// this can return nothing forever, which is why the card falls back to its caption rather than
// treating a missing picture as a bug.
async function capture(windowId) {
  try {
    if (typeof api.tabs.captureVisibleTab !== 'function') return null;
    const url = await api.tabs.captureVisibleTab(windowId, { format: 'png' });
    return typeof url === 'string' && url.startsWith('data:image/png') ? url : null;
  } catch (e) {
    return null;
  }
}

function schedule() {
  if (timer !== null) clearTimeout(timer);
  timer = setTimeout(() => {
    timer = null;
    report();
  }, QUIET_MS);
}

connect();

api.tabs.onCreated.addListener(schedule);
api.tabs.onRemoved.addListener(schedule);
api.tabs.onUpdated.addListener(schedule);
api.tabs.onActivated.addListener(schedule);
api.windows.onFocusChanged.addListener(schedule);
api.windows.onRemoved.addListener(schedule);
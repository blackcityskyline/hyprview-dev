// HyprView tabs — the browser side. Our own extension, one implementation for every Gecko and
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
const NATIVE_APP = 'hyprview_mediator';

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
    // LOGGED, not swallowed. A bare `return` here is what cost an afternoon: the port never
    // opened, nothing was written, and the console stayed empty -- so every conclusion after
    // that was a guess about a failure whose actual message was thrown away one frame above it.
    console.error('hyprview: connectNative("' + NATIVE_APP + '") failed:', e && e.message ? e.message : e);
    return;
  }
  console.log('hyprview: native port opened to ' + NATIVE_APP);
  port.onMessage.addListener((reply) => {
    if (reply && reply.name === 'activate')
      activate(reply);
    else if (reply && reply.ok === false)
      console.warn('hyprview mediator refused:', reply.reason);
  });
  // The handler is GIVEN the port, and that port carries the reason. Reading only "it
  // disconnected" is what turned this into an afternoon of guessing at manifests: the port
  // opens, Zen refuses the host, and the refusal arrives right here, ignored.
  port.onDisconnect.addListener((dead) => {
    const why = dead && dead.error ? dead.error : '(no reason given)';
    console.warn('hyprview: native port disconnected -- ' + why);
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
// One report at a time. A capture is a screenshot encode, and it routinely outlasts the
// debounce above; without this the next event starts a second capture over the first, they
// finish out of order and the pile grows for as long as the page keeps updating.
let reporting = false;

async function report() {
  if (reporting)
    return;
  reporting = true;
  try {
    await reportOnce();
  } finally {
    reporting = false;
  }
}

async function reportOnce() {
  if (!port)
    connect(); // the event page may have been restarted with no port and no top-level run
  if (!port)
    return;
  let tabs = [];
  let active = [];
  // Declared HERE, not in the try below. A `const` inside a block is scoped to that block, and
  // using it after the block is a ReferenceError at runtime that no linter on a background
  // script will catch before it fires: the whole report function died on every event, so the
  // port connected, the send never happened, and there was nothing on disk to look at.
  let focused = null;
  try {
    tabs = await api.tabs.query({});
    focused = await api.windows.getLastFocused({});
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
    } catch (e) {
      console.error('hyprview: postMessage failed:', e && e.message ? e.message : e);
    }
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
  } catch (e) {
    console.error('hyprview: postMessage failed:', e && e.message ? e.message : e);
  }
}

function simplify(tabs) {
  return tabs
    .filter((t) => typeof t.id === 'number' && typeof t.windowId === 'number')
    .map((t) => ({
      id: t.id,
      windowId: t.windowId,
      title: t.title || '',
      // The mediator needs the address to find the page in the browser's own thumbnail store;
      // it never leaves this machine and is only ever hashed.
      url: t.url || '',
      // Unloaded: still open, not in memory. The compositor hides these unless asked.
      discarded: t.discarded === true,
    }));
}

// The compositor picked a tab card. Making the tab active is also what reloads a discarded
// tab; focusing the window is what puts the browser in front of the user.
async function activate(command) {
  try {
    const tab = await api.tabs.update(command.tab, { active: true });
    const windowId = tab && typeof tab.windowId === 'number' ? tab.windowId : command.window;
    if (typeof windowId === 'number')
      await api.windows.update(windowId, { focused: true });
  } catch (e) {
    console.error('hyprview: activate failed:', e && e.message ? e.message : e);
  }
}

// A picture of what this window is showing right now.
//
// Gecko hands out a screenshot only to an extension holding `<all_urls>`: `activeTab` is
// granted by a click on the extension and this one has no button. So the manifest asks for it
// up front, and a browser that refuses anyway returns nothing -- the card then shows a picture
// from the browser's thumbnail store or its caption, never a bug.
const SHOT_WIDTH = 960;

// A page at full resolution is routinely past a megabyte, base64 adds a third of that, and a
// card is a few hundred pixels wide. Scaling down keeps one report well inside the size a
// native message can carry.
async function shrink(dataUrl) {
  // A Chromium MV3 worker has no DOM and therefore no canvas; there the picture goes as it is.
  if (typeof document === 'undefined') return dataUrl;
  try {
    const img = new Image();
    img.src = dataUrl;
    await img.decode();
    if (!img.naturalWidth || img.naturalWidth <= SHOT_WIDTH) return dataUrl;
    const canvas = document.createElement('canvas');
    canvas.width = SHOT_WIDTH;
    canvas.height = Math.round(img.naturalHeight * (SHOT_WIDTH / img.naturalWidth));
    canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
    return canvas.toDataURL('image/png');
  } catch (e) {
    return dataUrl;
  }
}

async function capture(windowId) {
  try {
    if (typeof api.tabs.captureVisibleTab !== 'function') return null;
    const url = await api.tabs.captureVisibleTab(windowId, { format: 'png' });
    if (typeof url !== 'string' || !url.startsWith('data:image/png')) return null;
    return await shrink(url);
  } catch (e) {
    return null;
  }
}

function schedule() {
  // Connect here too, not only at load. In MV3 the background is an EVENT PAGE: it starts
  // when something happens, so a connect() that only ran once at top level never ran at all
  // on a browser where nothing had happened yet. Every path that needs the port therefore has
  // to be able to open it, not just the one that happened to be written first.
  if (!port)
    connect();
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
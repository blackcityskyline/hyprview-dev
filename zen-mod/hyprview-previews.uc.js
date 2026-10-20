// Ask Firefox to store a thumbnail of every loaded tab, the way Ctrl+Tab does.
//
// A WebExtension cannot photograph a background tab: tabs.captureTab does not exist in Gecko,
// and captureVisibleTab photographs one tab per window. Firefox does have the picture -- it
// paints any tab's content for its own Ctrl+Tab panel -- and it keeps thumbnails/<md5(url)>.png
// in the local profile, but only for tabs that were shown and then left. So the active tab has
// no stored picture, and a tab opened and never left has none either.
//
// This mod closes that gap: it asks for the same store entry for every loaded tab. hyprview's
// mediator already hashes the address the extension reports and copies the file it finds, so
// nothing else has to change. Install as chrome/sine-mods/hyprview-previews/hyprview-previews.uc.js
// and restart the browser.
(() => {
  "use strict";

  const { PageThumbs } = ChromeUtils.importESModule(
    "resource://gre/modules/PageThumbs.sys.mjs"
  );

  // A tab is captured once per address; the selected one is refreshed on every tab event so
  // the card of the tab on screen is never older than the last switch.
  const captured = new WeakMap();
  let timer = 0;

  const captureAll = async () => {
    const gBrowser = window.gBrowser;
    if (!gBrowser) return;
    for (const tab of gBrowser.tabs) {
      const b = tab.linkedBrowser;
      if (!b || !b.currentURI || tab.hasAttribute("pending") || tab.hasAttribute("busy")) {
        continue;
      }
      const selected = tab === gBrowser.selectedTab;
      if (!selected && captured.get(tab) === b.currentURI.spec) continue;
      captured.set(tab, b.currentURI.spec);
      try {
        await PageThumbs.captureAndStore(b);
      } catch (e) {
        // A tab that refuses a capture (private, privileged page, crashed) is not an error
        // worth surfacing: it keeps falling back to whatever it had before.
      }
    }
  };

  const schedule = () => {
    clearTimeout(timer);
    timer = setTimeout(captureAll, 1500);
  };

  const start = () => {
    const gBrowser = window.gBrowser;
    if (!gBrowser || !gBrowser.tabContainer) return setTimeout(start, 1000);
    for (const name of ["TabSelect", "TabOpen", "TabClose"]) {
      gBrowser.tabContainer.addEventListener(name, schedule);
    }
    window.addEventListener("SSTabRestored", schedule, true);
    // Tabs restored at startup are still loading when this runs; wait them out. The second
    // pass catches whatever was still busy at the first.
    setTimeout(captureAll, 6000);
    setTimeout(captureAll, 20000);
  };

  start();
})();

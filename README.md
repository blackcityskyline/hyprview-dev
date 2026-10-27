# HyprView
[![license](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://github.com/blackcityskyline/hyprview/blob/main/LICENSE)

A better macOS Mission Control-style overview plugin for Hyprland

## Architecture

```
src/
├── main.cpp              plugin entry: registers the config schema, actions, Lua API
├── overview.hpp          Session: owns the three stores (Model / Clocks / Pixels)
├── session/              lifecycle: open/close/deactivate, shouldRenderWindow + damageSurface hooks, animation pump
├── build/                Model construction: windows -> tiles/strip, reflow, sync
├── input/                pointer events -> Model (drag FSM), keyboard, alt-tab
├── actions/              commands: drop/swap/workspace/focus (dispatcher + hyprctl + Lua from one table)
├── render/               the painter (one pass element, fixed z-slots), backdrop blur cache,
│                         window-content leaf (immediate surface drawing), tile/strip views, fx
├── anim.{hpp,cpp}        the animation registry: enum Id, config keys, defaults, shape
│                         params, Lua rules. Also where the Lua C API is bridged (extern "C")
├── anim_types.hpp        shared animation types: cardHides, tileExit, toggleIntent
├── anim_resolve.cpp      resolve()/resolveParam(), split out of anim.cpp for testability
├── clock/                time domain: Tween, per-effect clocks, curve registry (native + Lua)
├── pixel/                caches: labels, close glyph, entry snapshots
├── config/               the typed option schema, grouped by domain; live handles
├── view/                 the view-state shared by input and render
├── debug/                gated log channel (/tmp/hyprview.log)
├── model/                Model data structures (Tile/StripItem/Drag) and the tab feed/match
├── layout.{hpp,cpp}      pure geometry: no Hyprland, no GL
├── blur.{hpp,cpp}        self-contained dual-Kawase + gaussian, independent of decoration:blur
└── cursor.{hpp,cpp}      HW/SW cursor drawn on top (a consequence of RENDER_LAST_MOMENT)

Layering rule: arrows point one way — main wires the domains; domains
(session/build/input/actions/render) read the primitives (model/config/clock/
anim/pixel/layout/gl_util); the painter only reads Model+Clocks+Pixels and mutates
nothing. Whatever sits ON TOP of content draws AFTER it.
```

## Install

Via hyprpm:

```sh
hyprpm add https://github.com/blackcityskyline/hyprview
hyprpm enable hyprview
```

### Arch (AUR)

```sh
yay -S hyprview-git
```

### NixOS

```nix
inputs.hyprview = { url = "github:blackcityskyline/hyprview"; inputs.hyprland.follows = "hyprland"; };
```
```nix
wayland.windowManager.hyprland = {
  enable = true;
  plugins = [ inputs.hyprview.packages.${pkgs.system}.hyprview ];
  settings.bind = [ "SUPER, TAB, hyprview:toggle" ];
};
```

## Manual build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Produces `build/hyprview.so`. The ABI must match the running Hyprland exactly —
build against the same headers, or a version skew gives a `.so` that crashes on
load. While iterating, `cmake --build build --target reload` rebuilds and
hot-reloads into the running Hyprland.

## Usage

Dispatchers: `hyprview:toggle`, `hyprview:open`, `hyprview:close`, `hyprview:desktop`,
`hyprview:allworkspaces` (the all-workspaces "expo" view — opens into it if closed),
`hyprview:togglecarousel`, `hyprview:alttab` / `hyprview:alttabback` (alt-tab cycling —
see `alt_tab_*` below). The hyprctl equivalents are `hyprview` / `hyprviewclose` /
`hyprviewdesktop` / `hyprviewall` / `hyprviewcarousel` / `hyprviewalttab` /
`hyprviewalttabback`; `hyprviewunload` is the unload-safe close.

`hyprview:toggle` is not a plain close: closed it opens, open with a card focused it
confirms that card (switches to its workspace and focuses it — the same thing Enter does),
and open with nothing focused it cancels. Use `hyprview:close` for an unconditional cancel.

A card's caption is the window title — or, for a tab, the page title — cut at 50
characters with `...` at the end, the dots included. The limit counts characters, so a
Cyrillic title is cut between letters and not through one.

On entry the desktop recedes by 10% over the whole entry. The ring that opens around it
is the frame's own edge pixels stretched outward, so the seam is a colour match and there
is no border line. A close grows it back to exactly 1:1 playing the same ease-out forward:
a time-reversed playback stalls for half the close, then snaps into the cut. Works with
`blur = 0` too.

Lua:

```lua
hl.bind("SUPER + TAB", hl.plugin.hyprview.toggle)
hl.bind("SUPER + SHIFT + TAB", hl.plugin.hyprview.desktop)
hl.bind("SUPER + CTRL + TAB", hl.plugin.hyprview.allworkspaces)
```

```ini
bind = SUPER, TAB, hyprview:toggle
bind = SUPER SHIFT, TAB, hyprview:desktop
bind = SUPER CTRL, TAB, hyprview:allworkspaces
```

### Alt-Tab

`hyprview:alttab` / `hyprview:alttabback` are meant to be bound to a single modifier+key
combo, the same way you'd normally bind a real alt-tab — bind the SAME key you might
otherwise give `hyprview:allworkspaces`:

```ini
bind = SUPER, TAB, hyprview:alttab
bind = SUPER SHIFT, TAB, hyprview:alttabback
```

Closed → opens straight into the all-workspaces view with the selection on the first tile.
Already open → tapping the same bound key again (while still holding the modifier) advances
the cycle — Hyprland re-invokes the dispatcher on every physical press, exactly like holding
Alt and tapping Tab. Tiles keep their normal spatial grid order; cycling just walks the
selection through them. Set `alt_tab_modifier` to whatever modifier is in that bind (`alt`
by default) so releasing it can commit the selection — see the config table.

### Browser tabs

`tile_mode = tabs` or `both` draws a card per browser tab. Tabs are reported by
the extension in `extension/` through the native-messaging host
`bridge/mediator.py`; install and permissions are in `extension/README.md`.
The mediator writes the tab list and previews under `$XDG_RUNTIME_DIR/hyprview`,
and the plugin matches a browser window to a compositor window by the browser
process id, one to one. Titles are never used: a browser appends its own name to
the window title, so the two sides do not spell it the same way. A browser
window that has not reported yet is shown as a window; `key_browser` (`b`)
flips windows/tabs live.

The browser view covers every desktop, not only the one the overview opened on:
entering it turns all-workspaces on, and leaving it puts the mode back the way it
was (`tile_mode = tabs` therefore opens with all-workspaces on as well). A browser
on another workspace is still a browser.

Only loaded tabs get a card: an unloaded (discarded) tab is still open in the
browser but not in memory, so it is listed only when asked for
(`show_discarded_tabs`, the `key_discarded` key (`u`) in the browser-tabs view, or the
`discarded` command to flip it while running).
Activating a tab card — Enter or a click — makes that tab the browser's active
one, which also reloads it if it was discarded, and focuses the browser window.

A tab card shows the page that tab was showing when the overview last opened on
it: the picture is taken when the compositor asks for one, and the mediator
keeps it under that tab's id. Shooting on every tab event would mean painting a
layer over the page each time, which reads as a flash on every load and switch;
an event report carries the tab list only.

The screenshot needs `<all_urls>`: Gecko grants `activeTab` only to an extension
the user clicked, and this one has no button.

Opening the overview asks for a fresh picture of the tab on screen, because the
one on disk is from the last time that tab changed and a page restyled after
that (dark mode, a theme add-on) would otherwise keep its first look. A page the
browser draws itself — `about:addons`, `about:config` — cannot be photographed:
the capture comes back as the quarter-scale stored thumbnail, so it is refused
and the card keeps no picture. A page whose root background is transparent is shot with a
fixed layer of the `Canvas` system colour under its content, for the duration of the capture:
the browser's snapshot draws the document but not the canvas, and without it a dark page came
back as dark controls on a white sheet. A tab that navigated, and a tab the extension may not
photograph, is named in the report's `drop` list: the picture under that id is of another page,
so it is deleted and the card keeps no picture. A tab without one shows its title. The site's favicon sits in the title pill: the
extension fetches it, normalises it to a 32px PNG and the mediator writes `icons/<tabId>.png`.
The corner of the card carries the browser's own icon, looked up from the window class's
desktop entry.

## Config

All keys live under `plugin:hyprview:*`. Colors are strings — see [Colors](#colors).

- **`rows`** (default) — macOS-like: previews keep their aspect ratio and are packed into balanced rows, with the row count chosen to make the previews as large as possible. Reads spatially like the real desktop.
- **`grid`** — uniform cells, one preview centered in each.
- **`natural`** — keeps each window's real on-screen position, uniformly scaling the whole arrangement to fit.
- **`carousel`** — a switch, not a layout: it lays the main area out as an overlapped card run over any layout. All-workspaces view only; see [Carousel mode](#carousel-mode).

| Option | Type | Default | Description |
|---|---|---|---|
| `layout` | `rows` \| `grid` \| `natural` | `rows` | Main-area preview layout engine |
| `gap` | int (px) | `34` | Min spacing between window previews |
| `padding` | int (px) | `80` | Left/right outer margin of the preview area |
| `padding_top` | int (px) | `40` | Extra gap between the strip and the previews |
| `padding_bottom` | int (px) | `70` | Bottom outer margin |
| `max_scale` | float | `1.0` | Never enlarge a preview past real size × this |
| `preview_round` | int (px) | `12` | Corner radius for every window-shaped preview — main grid tiles AND strip card previews (with their borders/shadows) — clamped down automatically on the smaller strip thumbnails |
| `preview_round_power` | float | `2.0` | Corner curve exponent (`2` = circular, higher = squarer "squircle"), applied consistently to the same set of elements as `preview_round` |
| `blur` | float `0`..`1` | `1.0` | Backdrop + strip blur strength (`0` = off; fractions allowed) |
| `blur_passes` | int `1`..`16` | `3` | Backdrop blur gaussian iterations (more = softer) |
| `blur_size` | int (px) | `8` | Backdrop blur radius in screen pixels |
| `blur_resolution` | int `1`..`32` | `4` | Backdrop blur buffer = `1/N` monitor resolution (lower = sharper/cleaner, slightly more GPU; the blur is computed once per overview open, so the cost is negligible) |
| `anchor` | `top` \| `bottom` \| `left` \| `right` | `""` (= `top`) | Edge the workspace strip attaches to |
| `bar_position` | `top` \| `bottom` | `top` | Deprecated alias of `anchor`, used only while `anchor` is empty |
| `strip_offset` | int (px) | `0` | Inset from the anchored edge (0 = flush, no gap) |
| `strip_height` | int (px) | `150` | Strip band thickness, label included |
| `strip_margin` | int (px) | `22` | Padding around the strip |
| `strip_gap` | int (px) | `18` | Spacing between workspace cards |
| `strip_card_round` | int (px) | `10` | Workspace card corner radius |
| `backdrop_color` | color/role | `"0x73070a10"` | Dim + blur fill over the desktop |
| `surface_color` | color/role | `"0x1e1e26"` | The plate under a carousel card: the card is a frame, the window fits inside it, and the rest is this colour |
| `strip_band_color` | color/role | `"0x24ffffff"` | Band behind the cards |
| `strip_band_opacity` | int `0`..`100` | `70` | Strip band opacity, in percent — the band's only alpha source; `strip_band_color` supplies RGB |
| `strip_card_color` | color/role | `"0x3a0e131c"` | Inactive workspace card fill |
| `strip_active_color` | color/role | `"0x4d1c2c44"` | Active workspace card fill |
| `strip_active_border` | color/role | `"0xf0ffffff"` | Active card outline |
| `strip_hover_border` | color/role | `"0x80ffffff"` | Hovered card outline |
| `strip_plus_color` | color/role | `"0xd0eef4ff"` | The "+" glyph |
| `strip_all_color` | color/role | `"0xd0eef4ff"` | The "All workspaces" 2×2 glyph — own key, independent of `strip_plus_color` |
| `shadow_color` | color/role | `"0x70000000"` | Window preview drop shadow |
| `hover_border` | color/role | `"0xf0ffffff"` | Hovered window preview outline — a real border stroke, drawn only around the tile, never a fill over it |
| `select_border` | color/role | `"0xf066ccff"` | Keyboard-selected preview outline (Alt-Tab's cursor) — same real-stroke rendering as `hover_border` |
| `select_border_size` | int (px) | `3` | Keyboard-selected preview outline thickness — `0` disables it entirely |
| `hover_border_size` | int (px) | `3` | Hovered/focused preview outline thickness — `0` disables it entirely |
| `show_focus_border` | bool (0/1) | `1` | Whether hover/selection draw `hover_border`/`select_border` at all — see "Border modes" below |
| `show_border` | bool (0/1) | `0` | An always-on base ring on every tile, independent of hover/selection — see "Border modes" below |
| `show_tab_icons` | bool (0/1) | `1` | Site favicon in the tab's title pill and the browser's icon in the card's corner |
| `show_window_icons` | bool (0/1) | `1` | The window's app icon in the card's corner |
| `border_color` | color/role | `"0x50ffffff"` | `show_border`'s ring color |
| `border_size` | int (px) | `2` | `show_border`'s ring thickness — `0` disables it |
| `focus_follows_mouse` | bool (0/1) | `1` | Keyboard selection tracks the hovered preview |
| `scroll_switches_workspace` | bool (0/1) | `1` | Wheel over the main area steps prev/next workspace. A carousel run pages on its own instead, see `carousel_scroll_speed`. The desktop view is its own exception: over the grid the wheel scrolls the rows that hang below the screen (the strip still answers only its own hover) |
| `passthrough_keys` | bool (0/1) | `1` | Let keys the overview doesn't use reach Hyprland (keybinds keep working) |
| `key_close` | key names | `escape` | Keys that dismiss |
| `key_next_workspace` | key names | `tab` | Cycle the displayed workspace forward (wraps); `""` to disable. Held modifiers match exactly, so e.g. a `SUPER+Tab` toggle bind still passes through and closes |
| `key_prev_workspace` | key names | `shift+tab` | Cycle the displayed workspace backward (`mod+key` combos supported: `shift`/`ctrl`/`alt`/`super`) |
| `key_activate` | key names | `enter` | Keys that focus the selected preview |
| `key_close_window` | key names | `d` | Keys that close the selected preview's window (overview stays open); `""` to disable |
| `key_left` / `key_right` / `key_up` / `key_down` | key names | `left` / `right` / `up` / `down` | Move the keyboard selection (e.g. set `h`/`l`/`k`/`j` for vim nav) |
| `key_desktop` | key names | `shift` | Flip canvas↔grid |
| `key_all_workspaces` | key names | `a` | Toggle the all-workspaces (expo) view; `""` to disable |
| `key_workspace` | key names | `1,2,3,4,5,6,7,8,9,0` | Key at position N switches DIRECTLY to workspace N+1 (so `0` is always workspace 10) — creates it first if needed, independent of what's currently on the strip |
| `key_workspace_mode` | `switch` \| `jump` | `switch` | `switch`: a digit changes the displayed workspace and the overview stays open — `jump`: a digit switches AND immediately closes the overview; `Ctrl+digit` keeps the overview open in both modes |
| `alt_tab_modifier` | `alt` \| `ctrl` \| `shift` \| `super` | `alt` | Which modifier's release commits the Alt-Tab selection (match whatever you bound `hyprview:alttab` with) |
| `alt_tab_commit_on_release` | bool (0/1) | `1` | Releasing `alt_tab_modifier` focuses the selection & closes, like a normal alt-tab — off: releasing does nothing, confirm with `key_activate`/click instead |
| `exit_on_click` | bool (0/1) | `1` | Click on empty space dismisses the overview |
| `exit_on_switch` | bool (0/1) | `0` | Dismiss when the live workspace changes underneath (e.g. a keybind) |
| `show_all_workspaces` | bool (0/1) | `0` | Main area shows every window on the monitor (expo), not just the displayed workspace. Toggle live with `hyprview:allworkspaces`, the `key_all_workspaces` key, or the strip's "All" card |
| `strip_wallpaper` | bool (0/1) | `1` | Empty workspace cards show a cover-fit wallpaper thumbnail (the same source the backdrop uses) instead of a flat body |
| `strip_empty_mode` | `show` \| `neighbors` \| `hide` | `show` | `show`: every numeric workspace up to the highest one in use (at least 1-10) gets a strip card, even ones that were never created — `neighbors`: only occupied workspaces plus the displayed one's immediate numeric neighbors (reveals a run of empties one hop at a time as you navigate into it) — `hide`: only occupied workspaces, no empty ones at all. `show`/`neighbors` cards for a workspace that doesn't exist yet are created lazily on click/drop, same as `+` but at that specific number. |
| `show_special` | bool (0/1) | `0` | Include the special (scratchpad) workspace as a strip card |
| `show_discarded_tabs` | bool (0/1) | `0` | List unloaded (discarded) browser tabs as cards too. The `discarded` command flips the same thing at runtime; a browser left with nothing to show falls back to its window card |
| `tile_mode` | string | `"windows"` | What the main area shows: `windows` — one card per window; `tabs` — one card per browser tab: a browser not heard from yet is shown as a window, a browser that reported zero tabs contributes nothing, and non-browser windows are not shown; `both` — the window's card plus one per tab. An unrecognised value is `windows`, not `tabs` |
| `alttab_advances` | bool (0/1) | `0` | Alt-tab: the entry is aimed at the NEXT card, so the run travels to one target from its first frame and lands on it — one motion, taking exactly the entry's own time. `0` opens on the focused window and a second press steps |
| `strip_all_card` | bool (0/1) | `0` | Show a leading "All workspaces" card on the strip that toggles the expo view |
| `drag_to_swap` | bool (0/1) | `1` | Grid mode: dropping a preview onto another swaps the two windows' places — works across workspaces too in the all-workspaces (expo) view |
| `switch_on_drop` | bool (0/1) | `0` | Dropping a window on a card also follows it to that workspace |
| — | — | — | Dragging a preview (grid or strip) onto a workspace **card** with the left button moves it there; the **right** button swaps it with that workspace's last-focused window instead |
| `switch_on_new_workspace` | bool (0/1) | `1` | Clicking `+` follows the display to the new workspace |
| `hold_lift_ms` | int (ms) | `400` | Hold a dragged preview still this long before it lifts and starts scaling — separates a hold from a drag |
| `tile_layout` | `none` \| `dwindle` | `none` | `dwindle`: after a swap, re-run Hyprland's dwindle layout on the target workspace so neighbours fill the freed space; `none` leaves the layout alone |
| `new_workspace_mode` | `fill` \| `linear` | `fill` | `fill`: `+` takes the lowest free workspace id (backfills a gap left by a closed one) — `linear`: always appends past the highest existing id, never backfills |
| `close_button_color` | color/role | `"0xe6e23b3b"` | `✕` close-button fill — per-window, and per-workspace (closes every window on it) |
| `close_glyph_color` | color | `"0xffffffff"` | the `✕` glyph itself, drawn on top of `close_button_color` |
| `label_color` | color | `"0xf2ffffff"` | window/workspace name text under tiles and strip cards |
| `title_pill_color` | color | `"0xcc11151c"` | the caption pill behind a tile's title label in desktop/canvas mode |
| `title_font_size` | int | `12` | font size of a tile's title label |
| `title_label_color` | color | `"0xf2ffffff"` | the title's text colour (both layouts) |
| `title_position` | string | `"bottom"` | `"top"` or `"bottom"`: which edge of the card the whole caption hangs off |
| `title_round` | float | `-1.0` | caption pill corner radius in px; negative = capsule (half the height), `0` = square |
| `counter_pill_color` | color | `"0xcc11151c"` | the `n / total` pill under the caption in carousel view |
| `counter_font_size` | int | `12` | font size of the `n / total` counter |
| `counter_label_color` | color | `"0xf2ffffff"` | the counter's text colour |
| `counter_round` | float | `-1.0` | counter pill corner radius in px, same convention as `title_round` |
| `counter_position` | string | `""` | `"top"` or `"bottom"`: which card EDGE the counter hangs off; on the title's edge it always sits further out than the title. Empty follows `title_position` |
| `counter_pad_x` | float | `11.0` | horizontal padding inside the counter pill |
| `counter_pad_y` | float | `5.0` | vertical padding inside the counter pill |
| `counter_gap` | float | `8.0` | vertical gap between the caption pill and the counter pill |
| `counter_margin` | float | `6.0` | screen-edge margin kept for the counter pill |
| `kill_zone` | int | `1` | closing a window by dragging it to the screen edge OPPOSITE the strip; `0` makes the edge a no-op |
| `kill_zone_size` | float | `40` | thickness of that edge zone, px |
| `kill_zone_falloff` | float | `150` | how far from the zone its glow reaches, px |
| `kill_zone_reach` | int (px) | `160` | how far from the screen edge the drop target reaches |
| `click_window_ms` | int | `400` | window in which two clicks count as a double-click (`close_trigger=doubleclick`) |
| `strip_label_band` | float | `26` | height of the label strip under a strip card |
| `strip_label_extra` | float | `24` | how far a strip card widens to fit its label |
| `strip_slide_frac` | float | `0.55` | fraction of the band thickness the slide-out covers on an empty workspace; over windows the band always leaves by the full thickness |
| `strip_offset_max` | float | `0.42` | hard ceiling on `strip_offset`, as a fraction of the cross axis |
| `shadow_radius` | float | `6` | tile shadow blur at rest |
| `shadow_radius_lift` | float | `14` | tile shadow blur while dragged |
| `shadow_alpha` | float | `0.18` | tile shadow opacity multiplier |
| `desktop_grid_cols` | int | `4` | The desktop view's grid: columns of workspaces across |
| `desktop_tile_scale` | float | `1.125` | How much taller than its fit the desktop grid may be. Above 1 the tiles grow; the width fit always binds, so a wider grid keeps the tiles smaller |
| `desktop_grid_rows` | int | `3` | The desktop view's grid: rows down. The groups past the grid hang below the screen |
| `icon_shadow_offset` | float | `0.045` | card icon shadow drop, as a share of the icon's side (colour is `shadow_color`) |
| `icon_shadow_blur` | float | `0.1` | card icon shadow falloff, as a share of the icon's side |
| `icon_shadow_alpha` | float | `0.55` | card icon shadow opacity |
| `title_pad_x` | float | `11` | horizontal padding inside the title pill |
| `title_pad_y` | float | `5` | vertical padding inside the title pill |
| `title_gap` | float | `10` | gap between a tile and its title pill |
| `title_margin` | float | `6` | screen margin kept around the title pill |
| `close_glyph_fill` | float | `0.62` | fraction of the close button the glyph fills |
| `drop_hint_color` | color | `"0x38ffffff"` | drop-zone highlight flash on a workspace card while dragging |
| `close_button` | bool (0/1) | `1` | The global switch on every x: `0` removes close buttons everywhere, strip band included, whatever `close_button_visibility` says |
| `close_button_hover_only` | bool (0/1) | `1` | `1`: the x appears only while the cursor is over that surface (tile, deck card, band card) — `0`: as soon as its visibility rule allows it |
| `close_button_visibility` | `desktop` \| `everywhere` \| `none` | `desktop` | Where the x lives: `desktop` — tiles, cards and the carousel deck in desktop/canvas mode (`key_desktop`) only — `everywhere`: the same surfaces in every view — `none`: nowhere. The strip band follows this rule too |
| `close_button_icon` | string | `✕` | Glyph drawn in the close buttons |
| `close_button_size` | float | `1.0` | Scale multiplier over the computed base button size |
| `drag_size` | float (0.15–1.0) | `0.55` | Drag-phase preview size as a fraction of its source slot — applies to grid tiles and strip thumbs alike; `1.0` keeps the full-size preview under the cursor |
| `drag_offset_x` | float (px) | `46.0` | Drag preview offset from the pointer, x — keeps the drop zones visible under it |
| `drag_offset_y` | float (px) | `64.0` | Drag preview offset from the pointer, y |
| `swap_lift` | float (px) | `12.0` | vertical lift of a `slidevert` swap |
| `close_button_position` | `top-right` \| `top-left` \| `bottom-right` \| `bottom-left` | `top-right` | Corner the close button sits in |
| `close_trigger` | `button` \| `doubleclick` | `button` | `button` (default): the "✕" close button — `doubleclick`: double-click/double-tap directly on a tile closes it instead (the "✕" is hidden); `key_close_window` and the strip card's whole-workspace "✕"/middle-click are unaffected either way |
| `hide_top_layers` | bool (0/1) | `0` | Fade out Top layer surfaces (bars, e.g. Waybar) while open |
| `hide_overlay_layers` | bool (0/1) | `0` | Fade out Overlay layer surfaces (popups/notifications) while open |
| `above_namespaces` | string | `""` | Comma/space list of layer namespaces to draw *above* the overview (trailing `*` glob; a namespace containing `aboveoverview` always qualifies) |
| `preview_mode` | `live` \| `snapshot` | `live` | `live` (default): each tile renders its window's actual surface every frame — video/animations keep running, at the cost of GPU per visible preview — `snapshot`: tiles show a frozen texture captured at open time (no per-frame surface rendering; cheaper GPU, but content is static under the overlay) |
| `cursor_mode` | `auto` \| `software` | `auto` | `auto` (default): prefers Hyprland's hardware cursor (KMS cursor plane) when the driver exposes one — the cursor plane is composited above the framebuffer, so it stays visible over the dim backdrop with zero framebuffer writes, zero erase cycles, and zero GPU cost per mouse move — `software`: forces hyprview to draw the cursor itself, first erasing the previous position with a fully-opaque backdrop-colored rect (use only if you observe stale cursor artifacts with hardware mode) |
| `debug_logs` | bool (0/1) | `0` | Verbose `[hyprview]` logging |
| `debug_trace` | int mask | `0` | Machine-readable records in `/tmp/hyprview.log`: `1` ANIM, `2` DRAG, `4` LIFE, `8` SCROLL, `16` SWAP; `1` turns all on. `CMD|`, `TUNING|` and `STATE|` are never masked. Rotates at 8 MB, keeping three generations |

`top`/`bottom` give a horizontal strip, `left`/`right` a vertical one. `anchor`
supersedes the older `bar_position` (top/bottom only); set `anchor` and it wins.

### Animations

One master switch (`animations_enabled`, 0 = the whole plugin is instant) and
a set of LEAVES — one per ACTION. Every leaf reads four options:

| field | type | meaning |
|---|---|---|
| `<leaf>_enabled` | 0/1 | kill-switch for that one motion |
| `<leaf>_ms` | int (ms) | absolute duration; defaults to the leaf's built-in base |
| `<leaf>_speed` | float | multiplier over the duration above — `2.0` is twice as fast, `0.5` twice as slow (floor: one frame) |
| `<leaf>_curve` | name | any registry curve: native (`linear`/`easeout`/`easeinout`/`back`) or registered from Lua |

Motions with selectable TYPES also read a `<thing>_style` key (listed below).

The imperative twin of these keys is the Lua API
`hyprview.animation{ leaf = "...", enabled = bool, speed = n, ms = n, curve = "...", style = "..." }`
— whatever it sets wins over the static keys; unset fields fall through.

| Leaf | base ms | default curve | What it drives |
|---|---|---|---|
| `open` | 500 | easeout | entry reveal (chrome + tiles) |
| `close` | 500 | easeout | exit collapse |
| `glide` | 300 | easeout | tile movement slot ↔ slot (drops, syncs, close-home) |
| `appear` | 250 | easeout | staggered entry of brand-new tiles |
| `card` | 250 | back | "+" card pop-in |
| `pulse` | 180 | back | success ring after swaps/moves |
| `strip_step` | 200 | easeinout | animated strip scroll per workspace step |
| `ws_enter` | 250 | easeout | incoming tiles' timing on a workspace switch (style: `ws_enter_style`) |
| `ws_exit` | 250 | easeout | outgoing ghosts on a workspace switch (style: `ws_exit_style`) |
| `expo_in` | 250 | easeout | one→all spread during the all↔one flip (`hyprview:allworkspaces`) |
| `expo_out` | 250 | easeout | all→one collapse during the flip |
| `jump_in` | 180 | easeout | one→all flip started from the keyboard (`jump_style`) |
| `jump_out` | 220 | easeout | all→one flip started from the keyboard |
| `swap_main` | 320 | easeinout | the swap INITIATOR's flight |
| `swap_partner` | 320 | easeinout | the swap PARTNER's flight |
| `drop` | 400 | easeout | the landing of a preview released into a slot |
| `swap_fly` | 400 | easeout | the fly-between-slots of a swap |
| `lift` | 400 | easeout | the raise of a preview at pickup (see `lift_scale`, `lift_rise`) |
| `carousel_step` | 300 | easeout | the carousel run's selection step |
| `new_card` | 250 | easeout | a workspace card that appeared on its own |

Shape parameters — not timings, but "how it looks", so they are plain floats
rather than per-leaf keys:

| Key | default | meaning |
|---|---|---|
| `appear_stagger` | `0.08` | fraction of `appear` each tile waits before starting |
| `appear_stagger_per_tile` | `0.015` | extra stagger per tile index |
| `carousel_step_collapse` | `0.5` | fraction of the step over which unfocused cards collapse |
| `pulse_ring_scale` | `0.10` | ring growth per pulse frame |
| `lift_scale` | `0.07` | how much a lifted preview grows |
| `lift_rise` | `5.0` | px a lifted preview rises |
| `appear_pop_scale` | `0.15` | how much a tile is smaller at the start of its scale-in (used by `ws_enter_style = pop`) |
| `shadow_offset` | `16.0` | tile shadow offset at rest, px |
| `lift_shadow_offset` | `30.0` | tile shadow offset while dragging, px |

Style keys:

| Key | values | default |
|---|---|---|
| `ws_enter_style` | `slide` \| `slidevert` \| `fade` \| `pop` | `slide` |
| `ws_exit_style` | same set | `slide` |
| `grid_swap_style` | same set, plus the `horizontal` alias | `horizontal` |
| `strip_swap_style` | same set, plus the `horizontal` alias | `horizontal` |
| `jump_style` | `instant` \| same set | `instant` |

`slide` travels along the horizontal axis; `slidevert` (also `slide-vertical`) selects the
vertical one; `horizontal` is a legacy alias for `slide`. `fade` is alpha only, with no
movement.

**Custom curves (Lua).** Register a named curve once (e.g. next to the config
in `plugins/hyprview.lua`) — either as a function or as a cubic bezier:

```lua
-- function form: receives t in 0..1, returns shaped progress (overshoot OK)
hyprview.curve("snap", function(t) return 1 - (1 - t)^3 end)

-- bezier form (CSS-style control points), both spellings work:
hyprview.curve("fluent_decel", { type = "bezier", points = { {0, 0.2}, {0.4, 1} } })
hyprview.curve("easeOutCubic", { 0.33, 1, 0.68, 1 })

-- then use it anywhere:
hyprview.animation({ leaf = "glide", curve = "fluent_decel" })
```

### Carousel mode

`carousel = 1` lays the main area out as one row of overlapped cards with the
selected window centred. It is a switch over `layout`, not a layout of its own:
`layout` keeps deciding everything the run does not, and the strip, workspace
switching, the all<->one flip, alt-tab, drag-and-drop and canvas mode keep
working.

It applies in the **all-workspaces view only** - a single workspace has nothing
to arrange against. `key_carousel` (`c`) switches to all-workspaces with the run
on; from all-workspaces it flips the run only. `carousel_on_open` decides the
state when the view is opened from outside; the key ignores it.

```
┌───┐┌─┐┌───┐┌─┐┌───┐
│   ││ ││   ││ ││   │      selected card centred,
└───┘└─┘└───┘└─┘└───┘      neighbours overlap its edges
    (  2 / 7  )            counter under the centred card
```

- Every card is the same fullscreen-window box; a smaller window is fitted
  inside it whole, never deformed. The box does not depend on the window, so the
  run's geometry cannot shift with the selection - only the content moves.
- The focused card is centred on the free space left by the strip, not on the
  screen, so a left or right strip shifts it.
- The run tapers 100% -> 76% -> 56% -> x0.8 per step, each step 81.6% of the
  neighbour's own width; it is symmetric above and below the focused card.
- Preview windows are always opaque; depth comes from size, overlap and
  `carousel_tone_*`.
- The chrome is the ordinary tile chrome. A title wider than its card is
  shortened with `...`; the `n / total` counter hangs off the centred card's pill.
- Cards paint far -> centred, so the focused card and its ring sit on top. Only
  the centred card carries a ring.
- Windows beyond `carousel_cards` are not drawn; the run scales down to fit
  (`carousel_scale` overrides, and above 1 it deliberately overflows).
- Click an off-centre card to bring it to centre; click the centred card to
  commit. Hover only swaps the ring - the slots are built around the selection.
- Left/Right step the run circularly; Tab/Shift-Tab, the digits and
  `hyprview:allworkspaces` keep their workspace meaning. Enter or a click
  commits; the close glyph closes that window.
- A drop zone on the edge opposite the strip's anchor closes a dragged window,
  in the carousel and in any plain layout. It spans the whole monitor width, is
  invisible at rest and glows as the cursor approaches.
- Canvas mode overrides the carousel (`Shift`), as it does over `layout`.

| Option | Type | Default | Description |
|---|---|---|---|
| `carousel` | int | `0` | Card run instead of `layout`; all-workspaces only |
| `carousel_on_open` | bool (0/1) | `1` | Run state when the view is opened from outside |
| `key_carousel` | key | `c` | One-workspace: all-workspaces with the run on; all-workspaces: flip the run |
| `key_browser` | key | `b` | Flip the main area between windows and browser tabs |
| `key_discarded` | key | `u` | Show/hide unloaded (discarded) tabs; browser-tabs view only |
| `carousel_cards` | int | `5` | Cards in the run |
| `carousel_card_width` | int (px) | `0` | Narrow the standard box; `0` keeps the fullscreen box |
| `carousel_scale` | float | `1.0` | Zoom the whole run; above 1 it overflows |
| `carousel_scroll_speed` | float | `1.0` | Wheel sensitivity: notches x speed = cards moved |
| `carousel_tone_near` | float | `0.125` | Dim of the nearest unfocused card |
| `carousel_tone_far` | float | `0.25` | Dim of the farthest unfocused card |
| `carousel_depth_taper` | float | `0.8` | Multiplier per step beyond the farthest |

### Border modes

`show_border` and `show_focus_border` are independent toggles, not a fixed set of modes — combine
them however you like:

| `show_border` | `show_focus_border` | Result |
|---|---|---|
| `0` | `0` | No borders at all |
| `0` | `1` (default) | Borders only on hover/keyboard-selection |
| `1` | `0` | A constant ring on every tile that never changes |
| `1` | `1` | A constant ring that visibly changes color/thickness on focus, since `hover_border`/`select_border` draw right over it |

`border_color`/`border_size` style the always-on ring; `hover_border`/`select_border` and their
`_size` options style the focus ring.

### Colors

Every color option takes ONE string: a hex literal in any accepted form, or a
palette-resolved hex produced Lua-side from your theme module — hyprview itself
has zero coupling to any scheme engine:

| Value | Meaning |
|---|---|
| `"0xAARRGGBB"` / `"0xRRGGBB"` / `"#AARRGGBB"` / `"#RRGGBB"` / bare hex | Manual color (written as a **string**) |
| `col("primary")` etc. | Resolved by YOUR Lua config via `require(...)` — see below |

Scheme integration is fully optional and lives in the config:

```lua
-- any Lua module that exposes color roles as hex strings
local ok_c, c = pcall(require, "my-colors")
local function col(key, fallback)
    if ok_c and type(c[key]) == "string" then return c[key] end
    return fallback
end
local function with_alpha(color_str, alpha_hex)
    local hex = string.match(color_str, "#?(%x%x%x%x%x%x)")
    if not hex then return color_str end
    return "#" .. alpha_hex .. hex
end

hover_border        = with_alpha(col("primary", "ffffff"), "f0"),
close_button_color  = with_alpha(col("error",   "e23b3b"), "e6"),
strip_band_color    = with_alpha(col("primary", "ffffff"), "24"),
```

A value that doesn't parse falls through to that option's documented default,
so a missing module or a typo degrades gracefully instead of going black. If
your module rewrites itself on a theme change, re-evaluating `hl.config{}`
applies the new colors live.

### Lua

Every key from the tables above goes under `plugin.hyprview`. Keep the call in
`pcall`: one unknown key otherwise aborts the whole config load.

```lua
pcall(function()
    hl.config({
        plugin = {
            hyprview = {
                layout         = "rows",
                gap            = 34,
                blur           = 1,
                anchor         = "top",
                key_close      = "escape",
                backdrop_color = "0x73070a10",
            },
        },
    })
end)
```

### hyprland.conf

```ini
plugin {
    hyprview {
        layout = rows
        gap = 34
        blur = 1
        anchor = top
        key_close = escape
        backdrop_color = 0x73070a10
    }
}
```

Keys you leave out keep their defaults from the tables above.

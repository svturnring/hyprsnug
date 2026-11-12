# hyprsnug

A Hyprland plugin that makes columns in the built-in **scrolling** layout
inherit their width from the window's own width.

Works for four events:

- **opening** a window directly into the layout (its `float` rule overridden to
  `false`): the fresh column inherits the window's natural size right away
  instead of the default width,
- **embedding** a window into the layout (floating → tiled, e.g. with a custom
  `Super+V` bind),
- **moving** a window with the mouse (the usual `Super+LMB` tiled DnD drag),
- **moving** a window to another workspace (e.g. `movetoworkspace`): the column
  created on the destination workspace inherits the window's pre-move width, so
  the window does not snap to the default column width.

Instead of the new column snapping to the configured default (`scrolling:column_width`,
usually `0.5`), the column is sized so that it wraps the window "without margins".

## Why a plugin?

The embed part can be approximated from Lua with `layoutmsg colresize`, but the
drag part cannot:

- during a tiled DnD drag the window is floated; on drop it is re-tiled inside
  `CScrollingAlgorithm::newTarget()`, which creates the new column at the
  default width,
- Hyprland exposes no Lua-visible event for that drop, and the re-tile happens
  before any script could react.

A native plugin can hook the exact moment (`window.floating` + the drag
controller's `ended` signal) and reach the live `ScrollingAlgorithm` instance,
whose `usableArea()` is the *same* area the layout itself uses.

## How it works

The plugin listens to the following signals:

1. `window.open` — fires when a window is first mapped. When a window is opened
   straight into the strip (tiled, because its `float` rule was overridden to
   `false`), the layout puts it in a fresh column at the *default* width and
   scales it down to fit. The plugin fits that column to the window's size right
   inside the open event (before the entrance animation starts), so the window
   never visibly sits at the default column width first. The fitted size is the
   `size` rule's value when a size rule is set, otherwise the window's natural
   requested size.

2. `window.floating` — fires whenever a window changes floating state. When a
   window becomes tiled in a scrolling workspace and is the only member of its
   column (fresh embeds always get their own column), its column width is set to
   the window's width fraction.

3. `dragController()->m_events.ended` — fires right after a drag. A tiled drag
   restores the window's original floating size only after the drop finishes,
   so the drag case is handled from here (the floating event merely remembers
   which window was dragged).

4. `window.moveToWorkspace` + `tick` — a window moved to another workspace is
   re-tiled into a fresh column created at the *default* width. The
   `moveToWorkspace` event fires before that re-tile, so the plugin remembers
   the window and its pre-move width and re-fits the destination column on the
   next compositor tick.

In all cases the new width is a column *fraction* of the workspace's usable
width (Hyprland's scrolling layout has no pixel-based column width; it derives
pixels as `usableWidth * fraction`). In `exact` mode the window is then made to
round-trip to precisely its floating pixel width:

- the fraction is set, the layout recalculated, and the window's resulting goal
  width is read back and the fraction corrected by the measured error; a couple
  of passes converge to a pixel-exact width regardless of gaps/border or
  per-workspace `gaps_in` rules.

Only "own" columns are fitted: if a dragged window is dropped *into* an
existing column, that column and its other windows are left untouched.

## Build

Requires the Hyprland headers (matching your running Hyprland version):

```sh
make
# or: cmake -B build && cmake --build build
```

Produces `hyprsnug.so`.

## Install / load

### via hyprpm

The repository ships a `hyprpm.toml` manifest, so hyprpm builds the plugin from
source against the headers of your running Hyprland and keeps it up to date:

```sh
hyprpm update            # once: fetch/build the headers of your Hyprland (needs sudo)
hyprpm add <repository-url>
hyprpm enable hyprsnug
hyprpm reload            # load now; the next Hyprland start also picks it up
```

`hyprpm list` shows the repository and whether the plugin is enabled;
`hyprpm update` rebuilds it after a Hyprland update. hyprpm keeps its own copy
of the built plugin under `~/.local/share/hyprpm/`, so this is an alternative to
the manual install below, not an addition to it — load the plugin exactly once.

### manually

The plugin lives in the user plugin directory, not in `/usr/lib/hyprland-plugins/`.

```sh
# 1. keep the previous build as a backup
cp ~/.config/hypr/plugins/hyprsnug.so ~/.config/hypr/plugins/hyprsnug.so.bak-$(date +%Y%m%d)
# 2. install the fresh build
cp hyprsnug.so ~/.config/hypr/plugins/hyprsnug.so
# 3. load (or just unload; the config reloads the plugin from plugins.lua)
hyprctl plugin load ~/.config/hypr/plugins/hyprsnug.so
```

To load it automatically at startup, add to your Lua config:

```lua
hl.plugin.load("/home/svtrn/.config/hypr/plugins/hyprsnug.so")
```

Paths must be absolute.

## Configuration

| option                            | type    | default   | description |
| --------------------------------- | ------- | --------- | ----------- |
| `plugin:hyprsnug:enabled`         | int     | `1`       | master switch |
| `plugin:hyprsnug:mode`            | string  | `exact`   | see below |
| `plugin:hyprsnug:sync_pseudosize` | int     | `1`       | keep a pseudo window's size unchanged on embed/drag (see below) |
| `plugin:hyprsnug:fix_drag_ghost`  | int     | `1`       | keep a dragged pseudo window at its pseudo size (see below) |

`mode`:

- `exact` — makes the window keep **exactly** its floating pixel width. The
  fraction is set, the layout recalculated, the window's real goal width is read
  back, and the fraction is corrected by the measured error until it converges
  to the exact pixel width (no assumptions about gaps/border, so it also works
  with per-workspace `gaps_in` rules).
- `inherit` — the column fraction is exactly `windowWidth / usableWidth` (the
  window then appears slightly narrower inside the column because of gaps and
  the border).

Since Hyprland 0.57 the **pseudo size** (`WindowTarget::m_pseudoSize`, rendered
by tiled pseudo windows) is a separate quantity from the **floating size**. That
causes two visible glitches when working with embed / drag:

- **size change on embed** — a stale pseudo size would render the window at a
  different size the moment it is (re)tiled. `sync_pseudosize` pins the pseudo
  size to the window's current floating size on every embed / drag, so the
  window never changes size (verified: a floating window keeps both its width
  and its height). The same pin happens for a window opened directly into the
  strip, whose pseudo size is assigned at map and is either the `size` rule's
  value or a stale copy of the default column geometry.
- **drag "ghost" jump** — when you drag a pseudo window (Super+LMB), the drag
  controller floats it using a box derived from the whole column, so the ghost
  briefly renders as tall as the column. `fix_drag_ghost` forces the ghost to
  the window's pseudo size for the duration of the drag.

Set either to `0` to disable that particular fix.

Example (Lua config):

```lua
hl.config({
    plugin = {
        hyprsnug = {
            enabled          = true,
            mode             = "exact",
            sync_pseudosize  = true,
            fix_drag_ghost   = true,
        },
    },
})
```

## Unload

```sh
hyprctl plugin unload ~/.config/hypr/plugins/hyprsnug.so
```

## Compatibility

Built for Hyprland `0.57.1` (commit `acc1aa0bfd4bd575dcb0f76c35ccf12b660a1ab9`,
API version `0.2`). Recompile against your own headers — the plugin ABI is tied
to the Hyprland build. The plugin reads/writes internals of the built-in
`ScrollingAlgorithm` and will likely need adjustments on newer versions.
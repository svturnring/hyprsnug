# hyprsnug

A window-first overhaul of Hyprland's built-in **scrolling** layout. It turns the
strip into a sequence of applications that keep their own size, instead of a set
of columns that every application has to fit into.

## Demo

https://github.com/user-attachments/assets/088b2c83-0f0f-4a2e-9733-1abeb41c7115

[Vimeo](https://vimeo.com/1246319728)

## Why

Hyprland keeps several independent sizes for one and the same window: the
free-floating size, the size inside the layout, and the compact size. Every
switch between those modes can change the window's size — and for many
applications that is harmful. Browsers and web applications reflow their content
and the user loses the place they were reading; applications designed for a fixed
or modest size become unusable when stretched across the whole column; some
windows are intentionally small.

hyprsnug reverses the relationship: **a column adapts to the window, not the
other way around.** The window's free-floating size is the primary one, and the
window keeps it in every mode:

- the compact size follows the free-floating size in both directions;
- a window does not change size when it is opened, embedded into the strip, made
  compact, or moved;
- a column wraps the window tightly, without forcing it to a default width;
- opening a new window does not shift the strip or resize the windows that are
  already open.

The result is predictable: applications keep their layout, their content does not
re-render because of a window-manager resize, and the strip reads as applications
at their own sizes.

## Automatic column width

The width of a column follows the width of the window inside it. When a window
enters the strip — by being opened into it, embedded from floating, or moved —
the column is sized from that window instead of the default column width. The
same holds when a window is moved out of a stacked column into a column of its
own, by `promote` or by a directional move at the edge of the strip. Each
application keeps its size without affecting its neighbors, so scrolling through
many open windows leaves every one of them as it was.

## Install

### hyprpm (recommended)

```sh
hyprpm update
hyprpm add https://github.com/svturnring/hyprsnug
hyprpm enable hyprsnug
hyprpm reload
```

hyprpm builds the plugin against the headers of your running Hyprland, so it
always matches the compositor it runs in. After a Hyprland update, rebuild it
with `hyprpm update`.

### From source

```sh
make
cp hyprsnug.so ~/.config/hypr/plugins/hyprsnug.so
```

Load it with `hyprctl plugin load ~/.config/hypr/plugins/hyprsnug.so`, or from
your Hyprland config so that it survives a restart. Use either hyprpm or a manual
install — never both.

## Configuration

| option                            | type   | default | description |
| --------------------------------- | ------ | ------- | ----------- |
| `plugin:hyprsnug:enabled`         | int    | `1`     | master switch |
| `plugin:hyprsnug:mode`            | string | `exact` | how the column wraps the window (see below) |
| `plugin:hyprsnug:sync_pseudosize` | int    | `1`     | keep the compact size equal to the floating size |
| `plugin:hyprsnug:fix_drag_ghost`  | int    | `1`     | keep a dragged compact window at its own size |
| `plugin:hyprsnug:disable_drop_into_column` | bool | `true` | a dropped window lands in its own column instead of merging |

`mode`:

- `exact` — the column wraps the window's exact size, gaps and border included,
  so the visible size does not change at all.
- `inherit` — the column takes the bare window width without compensating gaps
  and border; the window then renders slightly narrower inside it.

`sync_pseudosize` pins the compact size to the current free-floating size when a
window is embedded or dragged, so it never changes size on the way in or out.
`fix_drag_ghost` keeps a dragged compact window at its compact size instead of
letting it briefly grow to the whole column.

`disable_drop_into_column` turns off the layout's native "drop into the column"
merge: dropping a window onto another window gives the dropped window a column of
its own, on the side of the drop, and the windows already in the strip keep their
sizes. Set it to `false` to restore the native merging.

Set either fix to `0` to turn it off.

Example (Lua config):

```lua
hl.config({
    plugin = {
        hyprsnug = {
            enabled                  = true,
            mode                     = "exact",
            sync_pseudosize          = true,
            fix_drag_ghost           = true,
            disable_drop_into_column = true,
        },
    },
})
```

Options can also be changed at runtime, without a rebuild, through the Lua config
manager:

```sh
hyprctl eval 'hl.config({plugin={hyprsnug={enabled=0}}})'
hyprctl getoption plugin:hyprsnug:enabled
```

## Unload

```sh
hyprctl plugin unload ~/.config/hypr/plugins/hyprsnug.so   # manual install
hyprpm disable hyprsnug                                   # hyprpm install
```

## Compatibility

Requires Hyprland's built-in scrolling layout (introduced in 0.57). The plugin is
built against the headers of a specific Hyprland build and is tied to its ABI, so
it must be rebuilt after every Hyprland update. It also relies on the internals of
the scrolling algorithm, which means a Hyprland update may require code changes in
the plugin as well.

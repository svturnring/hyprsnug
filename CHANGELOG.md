# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html). The current
version lives in [`VERSION`](./VERSION).

## [Unreleased]

### Added

- Event handlers run behind an exception barrier: an error thrown inside a
  handler is logged and the event is skipped, instead of escaping into the
  compositor's event dispatch and aborting the session.
- `plugin:hyprsnug:disable_drop_into_column` (default `true`): a window dropped
  onto another window lands in a column of its own, on the side of the drop,
  instead of merging into that window's column (BR-22, BR-23).
- `make dev-reload` / `make dev-done`: the smoke-test loop that unloads the
  plugin, rebuilds it, and hot-loads it back into the running Hyprland, then
  returns to the hyprpm-managed install. `make dev-reload` now also disables
  the hyprpm copy first: two loaded copies share the `plugin:hyprsnug:*` config
  keys, and their coexistence was able to abort the compositor when one of
  them was unloaded.

### Changed

- Dropping a tiled window onto another window no longer merges the two into one
  column by default: the dropped window gets a fresh column next to the one it
  was dropped on, and the windows already in the strip keep their sizes. The
  native merging is available by setting `plugin:hyprsnug:disable_drop_into_column`
  to `false`.
- `README.md` rewritten around the product itself: what the plugin changes about
  the scrolling layout, why a window should keep its size across modes, and how
  to install and configure it. Implementation notes and keybinds are gone.
- `README.md` opens with a demo: the 35-second recording plays inline as a
  player, through a video uploaded as a GitHub attachment, next to a link to the
  full-resolution Vimeo copy. The clip is not stored in the repository, so
  cloning for `hyprpm` stays small.

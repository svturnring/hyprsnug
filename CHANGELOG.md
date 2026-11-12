# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html). The current
version lives in [`VERSION`](./VERSION).

## [Unreleased]

### Added

- `make dev-reload` / `make dev-done`: the smoke-test loop that unloads the
  plugin, rebuilds it, and hot-loads it back into the running Hyprland, then
  returns to the hyprpm-managed install.

### Changed

- `README.md` rewritten around the product itself: what the plugin changes about
  the scrolling layout, why a window should keep its size across modes, and how
  to install and configure it. Implementation notes and keybinds are gone.

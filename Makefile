# Versioning: Semantic Versioning 2.0.0, see "Versioning" in AGENTS.md.
# VERSION (repository root) is the single source of truth for the plugin
# version; it is compiled into the binary and never hardcoded here.
VERSION := $(strip $(shell cat VERSION 2>/dev/null))
ifeq ($(VERSION),)
$(error VERSION file is missing or empty)
endif

# Else exist specifically for clang
ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -DHYPRSNUG_VERSION='"$(VERSION)"'

HYPRCTL    ?= hyprctl
HYPRPM     ?= hyprpm
PLUGIN_SO  := $(CURDIR)/hyprsnug.so

.PHONY: all install clean dev-reload dev-done

all:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) main.cpp -o hyprsnug.so `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon`

install:
	install -Dm755 hyprsnug.so "$(DESTDIR)$(PREFIX)/lib/hyprland-plugins/hyprsnug.so"

clean:
	rm -f ./hyprsnug.so

# Smoke testing after a change: see "Smoke testing after a change" in AGENTS.md.
# The order is mandatory — unload, build, load. Rebuilding a loaded .so means
# overwriting a memory-mapped file, which crashes the running compositor.
dev-reload:
	@$(HYPRCTL) plugin unload "$(PLUGIN_SO)" >/dev/null 2>&1 || true
	$(MAKE) all
	@$(HYPRCTL) plugin load "$(PLUGIN_SO)"

# Leave the dev loop and go back to the hyprpm-managed install.
dev-done:
	@$(HYPRCTL) plugin unload "$(PLUGIN_SO)" >/dev/null 2>&1 || true
	@$(HYPRPM) enable hyprsnug
	@$(HYPRPM) reload


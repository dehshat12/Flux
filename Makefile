CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

BUILD_DIR ?= build
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/flux
INSTALL ?= install

CFLAGS ?= -O0 -g
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -DWLR_USE_UNSTABLE
WARNFLAGS ?= -Wall -Wextra
BASE_CFLAGS := $(WARNFLAGS) -std=c11

WLROOTS_PC := $(strip $(shell \
	for mod in wlroots wlroots-0.18 wlroots-0.17; do \
		if $(PKG_CONFIG) --exists $$mod; then \
			echo $$mod; \
			exit 0; \
		fi; \
	done))

ifeq ($(WLROOTS_PC),)
$(error wlroots pkg-config module not found (tried wlroots, wlroots-0.18, wlroots-0.17))
endif

WAYLAND_PROTOCOLS_DIR := $(strip $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols))
ifeq ($(WAYLAND_PROTOCOLS_DIR),)
$(error failed to query wayland-protocols pkgdatadir via pkg-config)
endif

FLUX_SRCS := \
	src/core/main.c \
	src/core/theme.c \
	src/core/logging.c \
	src/core/config.c \
	src/core/launch.c \
	src/wm/view.c \
	src/compositor/cursor.c \
	src/compositor/output.c \
	src/compositor/input.c \
	src/wm/xdg.c \
	src/wm/taskbar.c

FLUX_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(FLUX_SRCS))
KPROBE_SRC := tools/kprobe.c
KPROBE_OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KPROBE_SRC))

FLUX_PKGS := $(WLROOTS_PC) wayland-server wayland-protocols xkbcommon libinput libdrm libpng
KPROBE_PKGS := libdrm

FLUX_PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(FLUX_PKGS))
FLUX_PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(FLUX_PKGS))
KPROBE_PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(KPROBE_PKGS))
KPROBE_PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(KPROBE_PKGS))

XDG_SHELL_XML := $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
CURSOR_SHAPE_XML := $(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml
PROTO_HEADERS := \
	$(BUILD_DIR)/xdg-shell-protocol.h \
	$(BUILD_DIR)/cursor-shape-v1-protocol.h

DEPS := $(FLUX_OBJS:.o=.d) $(KPROBE_OBJ:.o=.d)

.PHONY: all flux kprobe install uninstall clean
.DEFAULT_GOAL := all

all: flux kprobe

flux: $(BUILD_DIR)/flux

kprobe: $(BUILD_DIR)/kprobe

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(BUILD_DIR)/flux $(DESTDIR)$(BINDIR)/flux
	$(INSTALL) -m 0755 $(BUILD_DIR)/kprobe $(DESTDIR)$(BINDIR)/kprobe
	$(INSTALL) -d $(DESTDIR)$(DATADIR)/mouse
	$(INSTALL) -m 0644 mouse/mouse.png $(DESTDIR)$(DATADIR)/mouse/mouse.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flux
	rm -f $(DESTDIR)$(BINDIR)/kprobe
	rm -f $(DESTDIR)$(DATADIR)/mouse/mouse.png
	rmdir $(DESTDIR)$(DATADIR)/mouse 2>/dev/null || true
	rmdir $(DESTDIR)$(DATADIR) 2>/dev/null || true

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/xdg-shell-protocol.h: $(XDG_SHELL_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(BUILD_DIR)/cursor-shape-v1-protocol.h: $(CURSOR_SHAPE_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

$(BUILD_DIR)/src/%.o: src/%.c $(PROTO_HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(FLUX_PKG_CFLAGS) -I. -I$(BUILD_DIR) -pthread -MMD -MP -c $< -o $@

$(BUILD_DIR)/tools/%.o: tools/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(KPROBE_PKG_CFLAGS) -I. -MMD -MP -c $< -o $@

$(BUILD_DIR)/flux: $(FLUX_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(FLUX_OBJS) $(FLUX_PKG_LIBS) -lm -pthread

$(BUILD_DIR)/kprobe: $(KPROBE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(KPROBE_OBJ) $(KPROBE_PKG_LIBS)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

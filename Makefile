# mwm - Motif-style window manager for X11

PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
PKGS = x11 xft xrandr

ifeq ($(filter clean uninstall,$(MAKECMDGOALS)),)
MISSING := $(foreach p,$(PKGS),\
	$(if $(shell $(PKG_CONFIG) --exists $(p) && echo ok),,$(p)))
ifneq ($(strip $(MISSING)),)
$(error Missing build dependencies: $(strip $(MISSING)) -- see README.md for the apt install line)
endif
endif

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit \
	$(shell $(PKG_CONFIG) --cflags $(PKGS))
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))

SRC = src/main.c src/client.c src/config.c src/ewmh.c src/frame.c \
	src/menu.c src/input.c src/draw.c
OBJ = $(SRC:src/%.c=build/%.o)

all: mwm

build:
	mkdir -p build

build/%.o: src/%.c src/mwm.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

mwm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# The session file points at the installed binary, so a distribution's
# Motif mwm in /usr/bin cannot shadow this one.
install: mwm
	install -Dm755 mwm $(DESTDIR)$(PREFIX)/bin/mwm
	mkdir -p $(DESTDIR)/usr/share/xsessions
	sed 's|@BINDIR@|$(PREFIX)/bin|' data/mwm-x11.desktop.in \
		> $(DESTDIR)/usr/share/xsessions/mwm-x11.desktop
	chmod 644 $(DESTDIR)/usr/share/xsessions/mwm-x11.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mwm \
		$(DESTDIR)/usr/share/xsessions/mwm-x11.desktop

clean:
	rm -rf build mwm

.PHONY: all install uninstall clean

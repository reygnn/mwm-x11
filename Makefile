# mwm - Motif-style window manager for X11

PREFIX ?= /usr/local
XSESSIONSDIR ?= /usr/share/xsessions
PKG_CONFIG ?= pkg-config
PKGS = x11 xext xft xrandr

ifeq ($(filter clean uninstall,$(MAKECMDGOALS)),)
MISSING := $(foreach p,$(PKGS),\
	$(if $(shell $(PKG_CONFIG) --exists $(p) && echo ok),,$(p)))
ifneq ($(strip $(MISSING)),)
$(error Missing build dependencies: $(strip $(MISSING)) -- see README.md for the apt install line)
endif
endif

# CFLAGS and LDLIBS stay the user's (make CFLAGS="-O0 -g" works); what
# the build needs regardless comes in through separate variables.
CFLAGS ?= -O2 -g
MWM_CFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit \
	$(shell $(PKG_CONFIG) --cflags $(PKGS))
MWM_LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

SRC = src/main.c src/client.c src/config.c src/ewmh.c src/frame.c \
	src/menu.c src/input.c src/snap.c src/draw.c \
	src/outline.c
OBJ = $(SRC:src/%.c=build/%.o)

all: mwm

build:
	mkdir -p build

build/%.o: src/%.c src/mwm.h | build
	$(CC) $(CPPFLAGS) $(MWM_CFLAGS) $(CFLAGS) -c -o $@ $<

mwm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(MWM_LIBS) $(LDLIBS)

# The session file points at the installed binary, so a distribution's
# Motif mwm in /usr/bin cannot shadow this one.
install: mwm
	install -Dm755 mwm $(DESTDIR)$(PREFIX)/bin/mwm
	mkdir -p $(DESTDIR)$(XSESSIONSDIR)
	sed 's|@BINDIR@|$(PREFIX)/bin|' data/mwm-x11.desktop.in \
		> $(DESTDIR)$(XSESSIONSDIR)/mwm-x11.desktop
	chmod 644 $(DESTDIR)$(XSESSIONSDIR)/mwm-x11.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mwm \
		$(DESTDIR)$(XSESSIONSDIR)/mwm-x11.desktop

clean:
	rm -rf build mwm

.PHONY: all install uninstall clean

# mwm — Motif-style window manager for X11

A small reparenting X11 window manager that draws the classic Motif look:
3D bevels, a resize border with corner grooves, the window menu button,
minimize/maximize buttons, desktop icons, and mwm's menus, key bindings and
move/size feedback box. It is the X11 counterpart of
[mwc](https://github.com/reygnn/mwc), the Wayland compositor with the same look.

Plain Xlib + Xft + XRandR, with no toolkit and no cairo.

## Standards

**ICCCM 2.0**
- `WM_S0` manager selection: `-r` replaces a running window manager, and
  mwm exits cleanly when another one replaces it.
- `WM_STATE`, plus iconify via `WM_CHANGE_STATE` and withdrawal by synthetic
  `UnmapNotify`.
- All four input models (`WM_HINTS.input`, `WM_TAKE_FOCUS`) with real
  timestamps.
- `WM_DELETE_WINDOW`; clients without it are killed with `XKillClient`.
- `WM_NORMAL_HINTS`: min/max/base size, resize increments, aspect ratio, and
  `win_gravity` for placement and configure requests.
- Synthetic `ConfigureNotify` after moves, plus the save-set and restored
  border width when a window is released.
- `WM_TRANSIENT_FOR`: dialogs stay above their parent and iconify with it.

**EWMH 1.5**
- `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`,
  `_NET_CLIENT_LIST(_STACKING)`, `_NET_ACTIVE_WINDOW`, `_NET_WORKAREA`, and
  one desktop.
- `_NET_WM_STATE`: maximized vert/horz, fullscreen, hidden, above, below,
  sticky, modal, skip taskbar/pager, demands attention, focused.
- `_NET_WM_WINDOW_TYPE`: normal, dialog, utility/toolbar/menu, splash, dock,
  desktop, notification.
- `_NET_WM_STRUT(_PARTIAL)`, per monitor.
- `_NET_WM_MOVERESIZE` (GTK header bars), `_NET_MOVERESIZE_WINDOW`,
  `_NET_RESTACK_WINDOW`, `_NET_CLOSE_WINDOW`, `_NET_REQUEST_FRAME_EXTENTS`,
  `_NET_FRAME_EXTENTS`, `_NET_WM_ALLOWED_ACTIONS`.
- `_NET_WM_PING`: a hung client is killed on the second close.
- `_NET_WM_USER_TIME`, and `_NET_WM_ICON` shown in desktop icons.

**Motif:** `_MOTIF_WM_HINTS` decides which decorations and functions a
window gets, so a window without resize handles gets a thin border and
client-side-decorated GTK apps get no frame. `_MOTIF_WM_INFO` is set for
Motif applications.

## Build (Ubuntu 24.04 / 26.04)

    sudo apt install build-essential pkg-config libx11-dev libxext-dev \
        libxft-dev libxrandr-dev xterm
    make
    sudo make install   # /usr/local/bin/mwm + X session entry "Mwm (Motif, X11)"

Debian/Ubuntu also ship the original Motif `mwm` in `/usr/bin`. The session
entry calls `/usr/local/bin/mwm` explicitly.

## Run

- **Nested** for testing, inside another X session:
  `Xephyr :1 -screen 1280x800 & DISPLAY=:1 ./mwm -s xterm`
- **startx**: put `exec /usr/local/bin/mwm` in `~/.xinitrc`.
- **Display manager**: pick "Mwm (Motif, X11)" (an Xorg session).
- **Headless server over SSH**: see [docs/HEADLESS-SETUP.md](docs/HEADLESS-SETUP.md).

`-r` replaces a running window manager. `-d` logs debug output.
`~/.config/mwm/autostart` (executable) runs on startup unless `-s` is given.
`SIGHUP` or "Restart..." in the root menu re-executes mwm and keeps all
windows.

## Mouse

| Where              | Action                                              |
|--------------------|-----------------------------------------------------|
| Title bar          | drag: move · double-click: maximize · btn2: lower · btn3: menu |
| Menu button        | click: window menu · double-click: close            |
| Border / corners   | resize                                              |
| Icon               | double-click: restore · btn3: window menu           |
| Desktop            | btn1/btn3: root menu                                |
| Alt + btn1/2/3     | move / lower / resize anywhere in the window        |

## Snapping (as in Windows 11)

Drag a window by its title (or with Alt+btn1) until the pointer touches a
monitor edge. An outline in the window's colour shows where it will go,
and it snaps there when you let go:

| Pointer at …            | Window fills …                     |
|-------------------------|------------------------------------|
| left / right edge       | left / right half                  |
| a corner (64 px zone)   | that quarter                       |
| top edge                | the whole work area (maximized)    |

Dragging a snapped or maximized window away gives it back its previous
size, still under the pointer. Escape during the drag cancels. Snapped
halves report `_NET_WM_STATE_MAXIMIZED_VERT`, and panels and docks stay
uncovered.

Keyboard: Super+Left/Right snap to a half (pressing the other direction
restores the window). Super+Up goes from a half to its top quarter, or
maximizes. Super+Down goes from a half to its bottom quarter, from a
maximized window back to normal, and from a normal window to its icon.

## Keys (mwm defaults)

Alt+Tab / Alt+Shift+Tab cycle · Alt+Space or Shift+Esc window menu ·
Alt+F3 lower · Alt+F4 close · Alt+F5 restore · Alt+F7 move · Alt+F8 size ·
Alt+F9 minimize · Alt+F10 maximize · Alt+F11 fullscreen ·
Ctrl+Alt+Return / Super+Return terminal · Ctrl+Alt+Backspace quit ·
Super+arrows snap (see above).

During a keyboard move or resize, the arrow keys nudge the window (Shift
for 1 px steps), Return confirms and Escape cancels.

## Configuration

`~/.config/mwm/mwmrc` uses Motif resource syntax. Each program can get its
own frame colour, matched by `WM_CLASS` (instance or class name, ignoring
case; run `xprop WM_CLASS` and click the window to see it):

```
! ~/.config/mwm/mwmrc
Mwm*XTerm*activeBackground:    #8a0303
Mwm*firefox*activeBackground:  gold

! global defaults
Mwm*activeBackground:          #7387ad
Mwm*background:                #aeb2c3
Mwm*fontList:                  Sans:bold:size=10
```

| Resource | Meaning |
|---|---|
| `activeBackground` | frame colour while focused |
| `background` | frame colour while unfocused (globally: also menus and icons) |
| `activeForeground`, `foreground` | title text; default is black or white, whichever contrasts |
| `fontList` | fontconfig pattern for titles and menus (global only) |
| `rootBackground` | desktop colour (global only) |

A program with its own `activeBackground` keeps its colour when unfocused,
in a paler shade (mixed with the global `background`), and so does its
desktop icon. Set `Mwm*Name*background` to choose the unfocused colour
yourself. Colours are anything `XParseColor` accepts: `#rrggbb`, names like
`gold` or `firebrick`, `rgb:rr/gg/bb`. Lines starting with `!` or `#` are
comments, and later lines win. The file is read at startup and on
"Restart..." in the root menu.

The environment overrides the global entries:

| Variable          | Default             |                                   |
|-------------------|---------------------|-----------------------------------|
| `MWM_ACTIVE`      | `#7387ad`           | focused frame colour              |
| `MWM_INACTIVE`    | `#aeb2c3`           | other frames, menus, icons        |
| `MWM_ROOT`        | `#5b7792`           | desktop colour                    |
| `MWM_FONT`        | `Sans:bold:size=10` | fontconfig pattern                |
| `MWM_TERMINAL`    | first of xterm, uxterm, x-terminal-emulator, … | "New Window" |

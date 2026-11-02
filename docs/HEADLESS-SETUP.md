# Headless setup: mwm on an Ubuntu 24.04 server over SSH

This is for a server that has **no X11 at all**: no Xorg, no display manager
and no GPU. You only reach it with `ssh`. The server runs a virtual X display
with a built-in VNC server (TigerVNC's `Xtigervnc`), with mwm and your X
applications on it. You view it through a VNC viewer on your own machine,
tunnelled through SSH.

```
your machine                              server (Ubuntu 24.04)
┌──────────────────────┐   ssh -L 5901    ┌──────────────────────────────┐
│ VNC viewer           │ ═══════════════> │ Xtigervnc :1                 │
│ localhost:5901       │  (encrypted)     │  listens on 127.0.0.1:5901   │
└──────────────────────┘                  │  ├─ mwm                      │
                                          │  └─ xterm, other X apps      │
                                          └──────────────────────────────┘
```

The VNC port is bound to localhost only, so no firewall changes are needed
and nothing is exposed to the network. The SSH tunnel is the only way in.

> Why not `ssh -X`? X forwarding shows remote windows on *your* display,
> where your own window manager is already running. A window manager needs
> an X display of its own, and that is what `Xtigervnc` provides.

## 1. Install packages (server)

```sh
sudo apt update
sudo apt install --no-install-recommends \
    build-essential pkg-config git \
    libx11-dev libxft-dev libxrandr-dev \
    tigervnc-standalone-server tigervnc-tools \
    xterm xfonts-base fonts-dejavu-core \
    x11-utils x11-xserver-utils x11-xkb-utils dbus-x11
```

What each group does:

| Packages | Purpose |
|---|---|
| `build-essential pkg-config git lib*-dev` | build mwm |
| `tigervnc-standalone-server` | `Xtigervnc`: X server and VNC server in one, no GPU needed |
| `tigervnc-tools` | `tigervncpasswd` |
| `xterm xfonts-base` | a terminal (xterm needs the core `fixed` font from `xfonts-base`) |
| `fonts-dejavu-core` | the TrueType font that mwm's titles and menus use through Xft |
| `x11-utils x11-xserver-utils x11-xkb-utils` | `xdpyinfo`, `xprop`, `xrandr`, `xsetroot`, `setxkbmap` |
| `dbus-x11` | `dbus-launch`, which many X applications expect |

`--no-install-recommends` keeps the installation small: no Xorg server, no
desktop, no display manager.

## 2. Build and install mwm (server)

```sh
git clone https://github.com/reygnn/mwm-x11.git
cd mwm-x11
make
sudo make install          # /usr/local/bin/mwm
```

## 3. Set a VNC password (server)

```sh
mkdir -p ~/.vnc
tigervncpasswd ~/.vnc/passwd     # answer "n" to the view-only question
chmod 600 ~/.vnc/passwd
```

VNC passwords are limited to 8 characters. That is acceptable here because
the VNC port is reachable only from the server itself. The password guards
against other users on the same server.

## 4. Start script (server)

Create `~/bin/mwm-vnc`:

```sh
#!/bin/sh
# Virtual X display with a VNC server, and mwm on it.
set -eu

N=${MWM_DISPLAY:-1}                 # display :1 -> VNC port 5901
GEOMETRY=${MWM_GEOMETRY:-1600x900}

Xtigervnc ":$N" \
    -geometry "$GEOMETRY" -depth 24 \
    -localhost -rfbport "$((5900 + N))" \
    -rfbauth "$HOME/.vnc/passwd" -SecurityTypes VncAuth \
    -desktop mwm -AlwaysShared &
xvnc=$!
trap 'kill "$xvnc" 2>/dev/null || :' EXIT

# Wait until the display accepts connections.
i=0
until xdpyinfo -display ":$N" >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo "Xtigervnc did not start" >&2; exit 1; }
    sleep 0.1
done

export DISPLAY=":$N"
/usr/local/bin/mwm &
wm=$!
# Stopping the script stops both; mwm quitting stops the display.
trap 'kill "$wm" "$xvnc" 2>/dev/null || :' EXIT INT TERM
status=0
wait "$wm" || status=$?
exit "$status"                      # mwm's exit code, for systemd
```

```sh
mkdir -p ~/bin
chmod +x ~/bin/mwm-vnc
```

Optionally, list the programs that should start with the session in
`~/.config/mwm/autostart`:

```sh
mkdir -p ~/.config/mwm
cat > ~/.config/mwm/autostart <<'EOF'
#!/bin/sh
xterm -geometry 100x30 &
EOF
chmod +x ~/.config/mwm/autostart
```

### Quick manual test

```sh
~/bin/mwm-vnc &
DISPLAY=:1 xprop -root _NET_SUPPORTING_WM_CHECK   # prints a window id -> mwm runs
ss -ltn | grep 5901                               # only 127.0.0.1 / [::1]
kill %1                                           # stops mwm and Xtigervnc
```

## 5. Run it as a service (server)

The service keeps the session running when you log out of SSH and starts
it again after a reboot.

`~/.config/systemd/user/mwm-vnc.service`:

```ini
[Unit]
Description=mwm on a TigerVNC display

[Service]
ExecStart=%h/bin/mwm-vnc
Restart=on-failure

[Install]
WantedBy=default.target
```

```sh
systemctl --user daemon-reload
systemctl --user enable --now mwm-vnc
sudo loginctl enable-linger "$USER"   # run user services without a login
```

| Task | Command |
|---|---|
| Status | `systemctl --user status mwm-vnc` |
| Logs (mwm and Xtigervnc) | `journalctl --user -u mwm-vnc -f` |
| Stop / start | `systemctl --user stop mwm-vnc` / `start` |

"Quit..." in the root menu or Ctrl+Alt+Backspace ends mwm with exit code 0,
so the service stays stopped until you start it again. "Restart..." in the
root menu (or `systemctl --user kill -s HUP mwm-vnc`) re-executes mwm and
keeps all windows open. Use it after `make && sudo make install`.

## 6. Connect (your machine)

Open the tunnel and leave it running:

```sh
ssh -N -L 5901:localhost:5901 user@server
```

Then point a VNC viewer at **`localhost:5901`**:

- **Linux:** `sudo apt install tigervnc-viewer`, then `vncviewer localhost:5901`.
  TigerVNC can also open the tunnel itself:
  `vncviewer -via user@server localhost:1`.
- **macOS:** Finder → Go → Connect to Server → `vnc://localhost:5901`
  (built-in Screen Sharing).
- **Windows:** run the `ssh -N -L …` line in PowerShell (OpenSSH is built
  in), then use the TigerVNC viewer.

If you connect often, put the tunnel in `~/.ssh/config`:

```
Host mwm-server
    HostName server.example.org
    User user
    LocalForward 5901 localhost:5901
```

Then a plain `ssh -N mwm-server` opens it.

## 7. Practical notes

- **Keyboard shortcuts:** your local desktop may catch Alt+Tab, Alt+F4 and
  similar keys before the viewer sees them. In the TigerVNC viewer, use full
  screen (F8 opens its menu), which grabs the keyboard. mwm's window menu is
  also on Shift+Escape and on the title bar's menu button.
- **Screen size:** start with a different size through `MWM_GEOMETRY=1920x1080`
  (add `Environment=MWM_GEOMETRY=1920x1080` to the service). The TigerVNC
  viewer can also resize the remote session to fit its window
  ("Resize remote session to the local window"); mwm follows RandR changes.
  `DISPLAY=:1 xrandr` lists the available sizes.
- **Keyboard layout:** VNC sends key symbols, so your local layout usually
  works as is. If keys come out wrong, run `setxkbmap de` (or `us`, …) in an
  xterm inside the session.
- **Colours and font:** set `MWM_ACTIVE`, `MWM_INACTIVE`, `MWM_ROOT` and
  `MWM_FONT` in the start script before the `mwm` line. The README lists
  them.
- **Several users:** give each one their own display number
  (`MWM_DISPLAY=2` → port 5902).

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `mwm: cannot open display` | Xtigervnc did not start; see `journalctl --user -u mwm-vnc`. A stale lock from a crash: `rm -f /tmp/.X1-lock /tmp/.X11-unix/X1`. |
| `another window manager is running` | Something else manages `:1`. `mwm -r` replaces it. |
| xterm: `cannot load font` | `sudo apt install xfonts-base`, then restart the service. |
| Titles and menus show boxes or no text | `sudo apt install fonts-dejavu-core` (any TrueType font that fontconfig resolves for `Sans` works). |
| Viewer: connection refused | The tunnel is not running, or it points to the wrong port (5900 + display number). |
| Viewer: authentication failed | Set the password again with `tigervncpasswd ~/.vnc/passwd`. |
| Service stops after logout | `sudo loginctl enable-linger "$USER"` is missing. |

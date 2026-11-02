# Headless setup: mwm on an Ubuntu 24.04 server over SSH

This is for a server that has **no X11 at all**: no Xorg, no display manager
and no GPU. You only reach it with `ssh`. The server gets a virtual X
display, with mwm and your X applications on it, and you look at it through
an SSH tunnel. There are two ways to do that:

| | **A: RDP (xrdp)** | **B: VNC (TigerVNC)** |
|---|---|---|
| Client | Windows' built-in Remote Desktop (`mstsc`), opened through **Bitvise** or any SSH tunnel | a VNC viewer |
| Install on your machine | nothing | a VNC viewer (not built into Windows) |
| X starts | when you log in through RDP | when you start it from the SSH shell or a service |
| Best for | a Windows corporate notebook | Linux/macOS clients, or no xrdp on the server |

Both keep their port on the server's localhost, so no firewall changes are
needed and nothing is exposed to the network. The SSH tunnel is the only
way in. Both can be installed side by side.

```
corporate notebook (Windows)              server (Ubuntu 24.04)
┌──────────────────────────┐  SSH tunnel  ┌───────────────────────────────┐
│ Bitvise SSH Client       │ ═══════════> │ xrdp  127.0.0.1:3389          │
│  └─ Remote Desktop       │ (encrypted)  │  └─ Xorg (xorgxrdp) :10       │
│     (mstsc)              │              │      ├─ mwm                   │
└──────────────────────────┘              │      └─ xterm, other X apps   │
                                          └───────────────────────────────┘
```

> Why not X forwarding (`ssh -X`, or Bitvise's X11 forwarding)? It shows
> remote windows on a *local* X server: you would have to install one
> (VcXsrv, Xming) on the notebook, and that X server would already have its
> own window manager. A window manager needs an X display of its own on the
> server, and xrdp or Xtigervnc provides one.

## 1. Common setup (server)

### Packages

```sh
sudo apt update
sudo apt install --no-install-recommends \
    build-essential pkg-config git \
    libx11-dev libxext-dev libxft-dev libxrandr-dev \
    xterm xfonts-base fonts-dejavu-core \
    x11-utils x11-xserver-utils x11-xkb-utils dbus-x11
```

| Packages | Purpose |
|---|---|
| `build-essential pkg-config git lib*-dev` | build mwm |
| `xterm xfonts-base` | a terminal (xterm needs the core `fixed` font from `xfonts-base`) |
| `fonts-dejavu-core` | the TrueType font that mwm's titles and menus use through Xft |
| `x11-utils x11-xserver-utils x11-xkb-utils` | `xdpyinfo`, `xprop`, `xrandr`, `xsetroot`, `setxkbmap` |
| `dbus-x11` | `dbus-launch`, which many X applications expect |

### Build and install mwm

```sh
git clone https://github.com/reygnn/mwm-x11.git
cd mwm-x11
make
sudo make install          # /usr/local/bin/mwm
```

### Programs to start with the session (optional)

```sh
mkdir -p ~/.config/mwm
cat > ~/.config/mwm/autostart <<'EOF'
#!/bin/sh
xterm -geometry 100x30 &
EOF
chmod +x ~/.config/mwm/autostart
```

Colours and fonts are covered under "Configuration" in the
[README](../README.md).

---

## Variant A: RDP through Bitvise (recommended for Windows)

### A1. Install xrdp (server)

```sh
sudo apt install xrdp xorgxrdp
sudo adduser xrdp ssl-cert    # lets xrdp read the TLS key it ships with
```

`xorgxrdp` brings in the Xorg server. It runs with its own virtual video
driver, so no GPU or monitor is needed.

### A2. Listen on localhost only (server)

In `/etc/xrdp/xrdp.ini`, section `[Globals]`, replace `port=3389` with
`port=tcp://.:3389` (the file's own comments list this form as
`127.0.0.1:3389`):

```sh
sudo sed -i 's|^port=3389$|port=tcp://.:3389|' /etc/xrdp/xrdp.ini
sudo systemctl restart xrdp
ss -ltn | grep 3389          # must show 127.0.0.1:3389, not 0.0.0.0 or *
```

### A3. Start mwm in the RDP session (server)

xrdp runs `/etc/xrdp/startwm.sh`, which hands over to `/etc/X11/Xsession`,
which runs `~/.xsession`:

```sh
cat > ~/.xsession <<'EOF'
#!/bin/sh
exec /usr/local/bin/mwm
EOF
chmod +x ~/.xsession
```

Environment variables for mwm (for example `export MWM_FONT=…`) go before
the `exec` line.

The RDP login asks for your **Linux password** on the server. If you only
ever logged in with an SSH key, set one first: `sudo passwd "$USER"`.

### A4. Connect with Bitvise (notebook)

Log in to the server with Bitvise SSH Client as usual. Then pick one of
these two ways:

- **RDP tab (simplest):** set *Computer* to `localhost`. That is resolved
  on the server, and port 3389 is the default. Then click *New Remote
  Desktop* in the panel on the left. Bitvise opens the tunnel and starts
  Windows' Remote Desktop through it. Labels can differ slightly between
  Bitvise versions.
- **Port forwarding with your own `mstsc`:** on the *C2S* tab, add a rule:
  *Listen Interface* `127.0.0.1`, *List. Port* `13389`, *Destination Host*
  `localhost`, *Dest. Port* `3389`. Then run `mstsc /v:127.0.0.1:13389`.
  Save the profile so the rule comes back next time.

In xrdp's login window, keep *Session* = **Xorg** and enter your Linux user
name and password. mwm's desktop appears.

Plain OpenSSH works the same way:
`ssh -N -L 13389:localhost:3389 user@server`, then `mstsc /v:127.0.0.1:13389`.

### A5. Living with the session

- **Disconnect and come back:** closing the Remote Desktop window only
  disconnects. The session with all its windows keeps running, and the
  next login with the same user (and the same colour depth) reconnects to
  it.
- **Ending the session:** "Quit..." in the root menu or
  Ctrl+Alt+Backspace ends mwm, and with it the X session.
- **Restart mwm** (after `make && sudo make install`): "Restart..." in the
  root menu. Windows stay open.
- **Screen size:** the session takes the size of the Remote Desktop window
  at login. Pick it under *Show Options → Display* in `mstsc`, or use full
  screen.
- **Keyboard shortcuts:** in `mstsc` under *Local Resources → Keyboard*,
  set "Apply Windows key combinations" to *On the remote computer* (or
  *Only when using the full screen*), so Alt+Tab reaches mwm. mwm's window
  menu is also on Shift+Escape and on the title bar's menu button.
- **Keyboard layout:** xrdp uses the notebook's layout. If keys come out
  wrong, run `setxkbmap de` (or `ch`, `us`, …) in an xterm in the session,
  or add it to `~/.config/mwm/autostart`.

### A6. Troubleshooting (RDP)

| Symptom | Cause / fix |
|---|---|
| Bitvise/mstsc: cannot connect | `systemctl status xrdp xrdp-sesman`; `ss -ltn \| grep 3389` must show `127.0.0.1:3389`. |
| xrdp: "login failed" | Wrong or missing Linux password (`sudo passwd "$USER"`). |
| Login works, then the window closes or stays black | The session died at start: see `~/.xsession-errors` and `/var/log/xrdp-sesman.log`. Is `~/.xsession` executable and does `/usr/local/bin/mwm` exist? |
| xrdp does not start, log mentions the key | `sudo adduser xrdp ssl-cert`, then `sudo systemctl restart xrdp`. |
| `mwm: another window manager is running` in `~/.xsession-errors` | `~/.xsession` starts something else too. Only `exec /usr/local/bin/mwm` belongs at the end. |

---

## Variant B: VNC (TigerVNC)

### B1. Install TigerVNC and set a password (server)

```sh
sudo apt install --no-install-recommends tigervnc-standalone-server tigervnc-tools xauth
mkdir -p ~/.vnc
tigervncpasswd ~/.vnc/passwd     # answer "n" to the view-only question
chmod 600 ~/.vnc/passwd
```

`Xtigervnc` is an X server and VNC server in one and needs no GPU. VNC
passwords are limited to 8 characters. That is acceptable here because the
VNC port is reachable only from the server itself. The password guards
the VNC port against other users on the same server. An X authority cookie,
which the start script below creates, guards the X display itself.

### B2. Start script (server)

Create `~/bin/mwm-vnc`:

```sh
#!/bin/sh
# Virtual X display with a VNC server, and mwm on it.
set -eu

N=${MWM_DISPLAY:-1}                 # display :1 -> VNC port 5901
GEOMETRY=${MWM_GEOMETRY:-1600x900}

# Only clients holding this cookie may use the display: without it,
# any local user could connect to /tmp/.X11-unix/X$N directly.
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"
xauth -q add ":$N" . "$(mcookie)"

Xtigervnc ":$N" -auth "$XAUTHORITY" \
    -geometry "$GEOMETRY" -depth 24 \
    -localhost -rfbport "$((5900 + N))" \
    -rfbauth "$HOME/.vnc/passwd" -SecurityTypes VncAuth \
    -desktop mwm -AlwaysShared &
xvnc=$!
trap 'kill "$xvnc" 2>/dev/null || :' EXIT
trap 'exit 0' HUP INT TERM          # stopping is a clean exit, for systemd

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
trap 'kill "$wm" "$xvnc" 2>/dev/null || :' EXIT
status=0
wait "$wm" || status=$?
exit "$status"                      # mwm's exit code, for systemd
```

```sh
mkdir -p ~/bin
chmod +x ~/bin/mwm-vnc
```

Quick test from the SSH shell, which also covers "start X when needed":

```sh
~/bin/mwm-vnc &
DISPLAY=:1 xprop -root _NET_SUPPORTING_WM_CHECK   # prints a window id -> mwm runs
ss -ltn | grep 5901                               # only 127.0.0.1 / [::1]
kill %1                                           # stops mwm and Xtigervnc
```

A session started like this ends when the SSH connection does. To keep it
running after logout, use the service below, or start it with
`setsid -f ~/bin/mwm-vnc`.

### B3. Run it as a service (server, optional)

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
so the service stays stopped until you start it again; `systemctl --user
stop` counts as a clean stop too. "Restart..." in the root menu, or
`pkill -HUP -u "$USER" -x mwm`, re-executes mwm and keeps all windows open.
Do not use `systemctl --user kill -s HUP` for that: it signals every
process of the service, and Xtigervnc resets the display on SIGHUP.

### B4. Connect (your machine)

Open the tunnel and leave it running. In Bitvise, that is a *C2S* rule
`127.0.0.1:5901 → localhost:5901`. With OpenSSH:

```sh
ssh -N -L 5901:localhost:5901 user@server
```

Then point a VNC viewer at **`localhost:5901`**:

- **Linux:** `sudo apt install tigervnc-viewer`, then `vncviewer localhost:5901`.
  TigerVNC can also open the tunnel itself:
  `vncviewer -via user@server localhost:1`.
- **macOS:** Finder → Go → Connect to Server → `vnc://localhost:5901`
  (built-in Screen Sharing).
- **Windows:** the TigerVNC viewer (if you are allowed to install it).

If you connect often, put the tunnel in `~/.ssh/config`:

```
Host mwm-server
    HostName server.example.org
    User user
    LocalForward 5901 localhost:5901
```

Then a plain `ssh -N mwm-server` opens it.

### B5. Practical notes (VNC)

- **Keyboard shortcuts:** your local desktop may catch Alt+Tab, Alt+F4 and
  similar keys before the viewer sees them. In the TigerVNC viewer, use full
  screen (F8 opens its menu), which grabs the keyboard.
- **Screen size:** start with a different size through `MWM_GEOMETRY=1920x1080`
  (add `Environment=MWM_GEOMETRY=1920x1080` to the service). The TigerVNC
  viewer can also resize the remote session to fit its window
  ("Resize remote session to the local window"); mwm follows RandR changes.
  `DISPLAY=:1 xrandr` lists the available sizes.
- **Keyboard layout:** VNC sends key symbols, so your local layout usually
  works as is. If keys come out wrong, run `setxkbmap de` in an xterm
  inside the session.
- **Several users:** give each one their own display number
  (`MWM_DISPLAY=2` → port 5902).

### B6. Troubleshooting (VNC)

| Symptom | Cause / fix |
|---|---|
| `mwm: cannot open display` | Xtigervnc did not start; see `journalctl --user -u mwm-vnc`. A stale lock from a crash: `rm -f /tmp/.X1-lock /tmp/.X11-unix/X1`. |
| `another window manager is running` | Something else manages `:1`. `mwm -r` replaces it. |
| Viewer: connection refused | The tunnel is not running, or it points to the wrong port (5900 + display number). |
| Viewer: authentication failed | Set the password again with `tigervncpasswd ~/.vnc/passwd`. |
| Service stops after logout | `sudo loginctl enable-linger "$USER"` is missing. |

---

## Both variants

| Symptom | Cause / fix |
|---|---|
| xterm: `cannot load font` | `sudo apt install xfonts-base`, then start the session again. |
| Titles and menus show boxes or no text | `sudo apt install fonts-dejavu-core` (any TrueType font that fontconfig resolves for `Sans` works). |

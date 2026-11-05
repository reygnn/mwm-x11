#!/bin/bash
# Fixes taken over from mwc's reviews. Unlike the walk-throughs, this one checks
# its results: each line says ok or FAIL, and the exit code counts the failures.
# Runs inside the container (see run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
gcc -o /usr/local/bin/xc /repo/tests/docker/xc.c -lX11 || exit 1
gcc -shared -fPIC -o /tmp/monitors.so /repo/tests/docker/monitors.c || exit 1
: > /out/geo.txt
export DISPLAY=:99
failures=0

# Xvfb, then mwm with the given environment.
start() {
	pkill -x xc; pkill -x mwm; pkill -x Xvfb; sleep 0.3
	rm -f /tmp/.X99-lock
	Xvfb :99 -screen 0 1280x800x24 +extension RANDR >/dev/null 2>&1 &
	sleep 0.7
	env "$@" /b/mwm >>/out/mwm.log 2>&1 &
	sleep 0.5
}
win() { timeout 5 xdotool search --name "^$1\$" 2>/dev/null | head -1; }
open() { local t=$1; shift; xc win "$t" "$@" & sleep 0.4; }
close() { pkill -f "^xc win $1( |\$)"; sleep 0.4; }
name_of() {
	[ "$(($1))" = 0 ] && { echo none; return; }
	xdotool getwindowname "$(($1))" 2>/dev/null || echo "?"
}
active() { name_of "$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')"; }
stacking() { # bottom to top
	xprop -root _NET_CLIENT_LIST_STACKING | sed 's/.*# //; s/,//g' | tr ' ' '\n' |
		while read -r i; do [ -n "$i" ] && printf '%s ' "$(name_of "$i")"; done
}
mapstate() { timeout 5 xwininfo -id "$1" | awk '/Map State/{print $3}'; }
geo() {
	timeout 5 xwininfo -id "$1" | awk '/Absolute upper-left X/{x=$4}
		/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}
		END{printf "%dx%d+%d+%d",w,h,x,y}'
}
abs_x() { geo "$1" | sed 's/.*x[0-9]*+\(-\?[0-9]*\)+.*/\1/'; }
frame_h() {
	local p
	p=$(timeout 5 xwininfo -tree -id "$1" | awk '/Parent window id/{print $4}')
	timeout 5 xwininfo -id "$p" | awk '/Height:/{print $2}'
}
title_xy() {
	timeout 5 xwininfo -id "$1" | awk '/Absolute upper-left X/{x=$4}
		/Absolute upper-left Y/{y=$4}/Width:/{w=$2}END{printf "%d %d",x+w/2,y-10}'
}
alive() { pgrep -x mwm >/dev/null && echo yes || echo no; }
drag_start() {
	read -r DX DY <<<"$(title_xy "$1")"
	xdotool mousemove "$DX" "$DY" mousedown 1; sleep 0.15
	xdotool mousemove $((DX + 40)) $((DY + 40)); sleep 0.15
}
# name, want, got
check() {
	local r=ok
	[ "$2" = "$3" ] || { r=FAIL; failures=$((failures + 1)); }
	printf '%-4s %-46s want: %-16s got: %s\n' "$r" "$1" "$2" "$3" | tee -a /out/geo.txt
}
check_not() {
	local r=ok
	[ "$2" != "$3" ] || { r=FAIL; failures=$((failures + 1)); }
	printf '%-4s %-46s not:  %-16s got: %s\n' "$r" "$1" "$2" "$3" | tee -a /out/geo.txt
}

# ---- focus and stacking ------------------------------------------------------

start
open A; xdotool key alt+F3; sleep 0.3
check "Alt+F3 on a lone window keeps the focus" A "$(active)"

start
open O; open P; open D --transient "$(win P)"
xdotool key alt+shift+Tab; sleep 0.4
check "Alt+Shift+Tab sends a dialog back with its window" "P D O " "$(stacking)"
check "... and the focus moves on" O "$(active)"

start
open P --geo 300x200+100+100; open D --transient "$(win P)"
open O --usertime0 --geo 300x200+500+100
close D
check "A closed dialog focuses its window, not the top" P "$(active)"

# ---- dialogs and their minimized windows -------------------------------------

start
open P; P=$(win P); xdotool windowminimize "$P"; sleep 0.3
open D --transient "$P"; D=$(win D)
check "A dialog of an icon waits for it" IsUnMapped "$(mapstate "$D")"
xdotool windowactivate "$P"; sleep 0.4
check "... and comes back with it" IsViewable "$(mapstate "$D")"
check "... mwm survives" yes "$(alive)"

start
open P; P=$(win P); open D --transient "$P"; D=$(win D)
xdotool windowminimize "$P"; sleep 0.3
xprop -id "$D" -remove WM_TRANSIENT_FOR; sleep 0.3
check "A hidden dialog that loses its window comes back" IsViewable "$(mapstate "$D")"

start
open P; P=$(win P); open D --transient "$P"; D=$(win D)
xdotool windowminimize "$P"; sleep 0.3
pkill -HUP -x mwm; sleep 0.8
check "A dialog stays hidden with its icon over a restart" IsUnMapped "$(mapstate "$D")"
xdotool windowactivate "$P"; sleep 0.4
check "... and comes back with it" IsViewable "$(mapstate "$D")"

# ---- a state changing during a drag ------------------------------------------

start
open A --geo 300x200+200+200; A=$(win A); before=$(geo "$A"); drag_start "$A"
xc state "$A" add _NET_WM_STATE_FULLSCREEN; sleep 0.3
xdotool mousemove $((DX + 90)) $((DY + 90)); sleep 0.15; xdotool mouseup 1; sleep 0.3
check "Fullscreen during a drag stays in place" 1280x800+0+0 "$(geo "$A")"
xc state "$A" remove _NET_WM_STATE_FULLSCREEN; sleep 0.3
check "... and returns from before the drag" "$before" "$(geo "$A")"

start
open A --geo 300x200+200+200; A=$(win A); drag_start "$A"
xc state "$A" add _NET_WM_STATE_MAXIMIZED_VERT; sleep 0.3
xdotool mousemove $((DX + 90)) $((DY + 90)); sleep 0.15; xdotool mouseup 1; sleep 0.3
check "Maximize during a drag holds" yes \
	"$(xprop -id "$A" _NET_WM_STATE | grep -q MAXIMIZED_VERT && echo yes || echo no)"

# ---- tiling ------------------------------------------------------------------

start
for n in T1 T2 T3 T4 T5; do open $n --minh 300; done
xdotool key super+t; sleep 0.5
T2=$(win T2)
check "Without room the stack rolls up" 37 "$(frame_h "$T2")"
close T4; sleep 0.3
check_not "With room again it unrolls" 37 "$(frame_h "$T2")"

start
open TALL --minh 1000 --geo 300x200+20+20; open B --minh 100; open C --minh 100
xdotool key super+t; sleep 0.5
TALL=$(win TALL)
check_not "A window too tall for the column is not rolled up" 37 "$(frame_h "$TALL")"
check "... and floats, at its minimum height" 300x1000+38+63 "$(geo "$TALL")"

# ---- autostart ---------------------------------------------------------------

mkdir -p "/tmp/a b/mwm"
printf '#!/bin/sh\ntouch /tmp/autostart-ran\n' > "/tmp/a b/mwm/autostart"
chmod +x "/tmp/a b/mwm/autostart"; rm -f /tmp/autostart-ran
start XDG_CONFIG_HOME="/tmp/a b"; sleep 0.3
check "Autostart with a space in its path" yes \
	"$([ -e /tmp/autostart-ran ] && echo yes || echo no)"
printf 'touch /tmp/autostart-ran\n' > "/tmp/a b/mwm/autostart"; rm -f /tmp/autostart-ran
start XDG_CONFIG_HOME="/tmp/a b"; sleep 0.3
check "Autostart without a #! line" yes \
	"$([ -e /tmp/autostart-ran ] && echo yes || echo no)"

# ---- keys and grabs ----------------------------------------------------------

start; xdotool key ctrl+alt+BackSpace; sleep 0.4
check "Ctrl+Alt+Backspace quits" no "$(alive)"
start; xdotool mousemove 900 700 click 3; sleep 0.3
xdotool key ctrl+alt+BackSpace; sleep 0.4
check "... with a menu open" no "$(alive)"
start; open A; xdotool key alt+F7; sleep 0.3; xdotool key ctrl+alt+BackSpace; sleep 0.4
check "... during a keyboard move" no "$(alive)"

start; open A --geo 300x200+200+200; xdotool mousemove 900 700; sleep 0.2
xc holdpointer 2 & sleep 0.4
xdotool key alt+F7; sleep 0.3; wait $!
check "Alt+F7 without a grab leaves the pointer" "900 700" \
	"$(xdotool getmouselocation --shell | awk -F= '/^[XY]=/{printf "%s ",$2}' | sed 's/ $//')"

# ---- monitors, through the shim ----------------------------------------------

monitors=/tmp/monitors
two() { printf '0 0 640 800\n640 0 640 800\n' > $monitors; }
one() { printf '0 0 640 800\n' > $monitors; }
# Xvfb cannot resize, so mwm hears of a change by a ConfigureNotify on the root.
changed() { xc rootchanged; sleep 0.4; }
SHIM=(LD_PRELOAD=/tmp/monitors.so MWM_TEST_MONITORS=$monitors)

two; start "${SHIM[@]}"
# A new window opens on the monitor under the pointer: start on the first.
xdotool mousemove 100 400; sleep 0.1
open A --geo 300x200+100+100; A=$(win A); read -r x y <<<"$(title_xy "$A")"
xdotool mousemove "$x" "$y" mousedown 1; sleep 0.15; xdotool mousemove 900 300; sleep 0.15
xdotool mousemove 900 0; sleep 0.2; xdotool mouseup 1; sleep 0.4
want=$(geo "$A")
check_not "Dropped on the second monitor's top edge" "${want%%+*}+6+31" "$want"
xdotool key super+t; sleep 0.4
check "Tiling takes it to the first monitor" yes "$([ "$(abs_x "$A")" -lt 640 ] && echo yes || echo no)"
xdotool key super+t; sleep 0.4
check "Tiling off puts it back on the second" "$want" "$(geo "$A")"
xdotool key super+t; sleep 0.4
pkill -HUP -x mwm; sleep 0.8
xdotool key super+t; sleep 0.4
check "... also after a restart while tiled" "$want" "$(geo "$A")"

two; start "${SHIM[@]}"
open A --geo 300x200+800+100; A=$(win A); xc state "$A" add _NET_WM_STATE_MAXIMIZED_VERT; sleep 0.2
xc state "$A" add _NET_WM_STATE_MAXIMIZED_HORZ; sleep 0.3
# Maximized on the second monitor, which goes; it would return to that one.
one; changed
xc state "$A" remove _NET_WM_STATE_MAXIMIZED_VERT; sleep 0.2
xc state "$A" remove _NET_WM_STATE_MAXIMIZED_HORZ; sleep 0.3
x=$(abs_x "$A")
check "Its way back is on a monitor that is left" yes "$([ "$x" -lt 640 ] && echo yes || echo no)"

two; start "${SHIM[@]}"; xdotool mousemove 900 700 click 3; sleep 0.3
check "A menu holds the keyboard" yes "$(xc grabbed)"
one; changed
check "... until the monitors change" no "$(xc grabbed)"

two; start "${SHIM[@]}"; open A --geo 300x200+800+100; xdotool key alt+F7; sleep 0.3
check "A keyboard move holds the keyboard" yes "$(xc grabbed)"
one; changed
check "... until the monitors change" no "$(xc grabbed)"

pkill -x xc; pkill -x mwm; pkill -x Xvfb
echo "failures: $failures" | tee -a /out/geo.txt
exit "$failures"

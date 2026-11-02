#!/bin/bash
# Tiling edge cases: maximized before tiling, focus and shading, Alt+Tab,
# fullscreen, an undecorated window, 25 windows. Runs inside the
# container (see run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make -s clean && make -s || exit 1
export DISPLAY=:99
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 & sleep 1
./mwm > /out/mwm.log 2>&1 & sleep 1
xt() { xterm -xrm 'XTerm*allowTitleOps: false' -geometry 60x15+${2:-100}+${3:-100} -title "$1" & sleep 0.6; }
win() { xdotool search --name "^$1\$" | head -1; }
frame() { local p; p=$(xwininfo -tree -id "$(win $1)" | awk '/Parent window id/{print $4}'); xwininfo -id $p | awk -v t=$1 '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}END{printf "%s=%dx%d+%d+%d ",t,w,h,x,y}'; }
show() { echo -n "$1: "; shift; for t in "$@"; do frame $t; done; echo; }
active() { xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}'; }
name_of() { xprop -id "$1" WM_NAME 2>/dev/null | sed 's/.*= "\(.*\)"/\1/'; }
absxy() { xwininfo -id "$(win $1)" | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}END{print x, y, w}'; }
click_btn() { read X Y W < <(absxy $1); if [ "$2" = max ]; then bx=$((X+W-12)); else bx=$((X+W-37)); fi; xdotool mousemove $bx $((Y-12)) click 1; sleep 0.6; }

xt A 40 40; xt B 200 200; xt C 400 350
# maximize B before tiling (Alt+F10 on focused B)
xdotool windowactivate --sync $(win B) 2>/dev/null; xdotool key alt+F10; sleep 0.5
echo "B state before tiling: $(xprop -id $(win B) _NET_WM_STATE)"
xdotool key super+t; sleep 0.8
show "tiled" A B C
echo "B state while tiled: $(xprop -id $(win B) _NET_WM_STATE)"
# shade A (stack) after focusing it
click_btn A min
show "A shaded" A B C
echo "active after shading A: $(name_of $(active))"
import -window root /out/1-shaded.png
# Alt+Tab until A focused -> unshades
for i in 1 2 3; do xdotool key alt+Tab; sleep 0.5; [ "$(name_of $(active))" = A ] && break; done
show "after Alt+Tab to A" A B C
echo "active: $(name_of $(active))"
# fullscreen C (main?) via Alt+F11, then back
xdotool windowactivate --sync $(win C) 2>/dev/null; xdotool key alt+F11; sleep 0.6
show "C fullscreen" A B C
xdotool key alt+F11; sleep 0.6
show "C back" A B C
# undecorated window: remove decorations via motif hints on A, then Alt+F9
xprop -id $(win A) -f _MOTIF_WM_HINTS 32c -set _MOTIF_WM_HINTS "2, 0, 0, 0, 0"; sleep 0.5
xdotool windowactivate --sync $(win A) 2>/dev/null; xdotool key alt+F9; sleep 0.5
show "undecorated A after Alt+F9 (must stay open)" A B C
xdotool key super+t; sleep 0.8
show "tiling off" A B C
echo "B state after tiling off: $(xprop -id $(win B) _NET_WM_STATE)"
# many windows
xdotool key super+t; sleep 0.5
for i in $(seq 1 22); do xterm -xrm 'XTerm*allowTitleOps: false' -geometry 40x5 -title "W$i" & sleep 0.25; done
sleep 2
import -window root /out/2-many.png
over=0
for i in $(seq 1 22) A B C; do id=$(win $([[ $i =~ ^[0-9]+$ ]] && echo W$i || echo $i)); p=$(xwininfo -tree -id $id | awk '/Parent window id/{print $4}'); read y h < <(xwininfo -id $p | awk '/Absolute upper-left Y/{y=$4}/Height:/{h=$2}END{print y, h}'); [ $((y+h)) -gt 800 ] && over=$((over+1)); done
echo "windows extending below the screen: $over"
echo "X errors in mwm log: $(grep -ciE 'x error|badvalue' /out/mwm.log)"

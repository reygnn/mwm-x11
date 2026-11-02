#!/bin/bash
# Super+Shift+arrows: a floating window to the edges and corners of the
# work area; a vertically maximized one only sideways. Runs inside the
# container (see run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make -s clean && make -s || exit 1
: > /out/geo.txt # appended to below
export DISPLAY=:99
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 & sleep 1
./mwm > /out/mwm.log 2>&1 & sleep 1
xterm -xrm 'XTerm*allowTitleOps: false' -geometry 60x15+300+200 -title A & sleep 1
win() { xdotool search --name "^$1\$" | head -1; }
frame() { local p; p=$(xwininfo -tree -id "$(win $1)" | awk '/Parent window id/{print $4}'); xwininfo -id $p | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}END{printf "%dx%d+%d+%d (right %d, bottom %d)",w,h,x,y,x+w,y+h}'; }
step() { xdotool key "$1"; sleep 0.5; echo "$2: $(frame A)" >> /out/geo.txt; }
xdotool windowactivate --sync "$(win A)" 2>/dev/null
echo "start: $(frame A)" >> /out/geo.txt
step super+shift+Left  "left"
step super+shift+Up    "left+up -> top-left corner"
step super+shift+Right "right -> top-right corner"
step super+shift+Down  "down -> bottom-right corner"
step super+shift+Left  "left -> bottom-left corner"
xdotool key super+Left; sleep 0.5
step super+shift+Right "snapped left half: ignored"
xdotool key super+Right; sleep 0.5
echo "unsnapped: $(frame A)" >> /out/geo.txt
import -window root /out/move.png

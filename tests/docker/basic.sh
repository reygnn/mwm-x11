#!/bin/bash
# Tiling walk-through: three xterms, tiling on, shade, promote, demote,
# a new window, tiling off, restart. Runs inside the container (see
# run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
: > /out/geo.txt # appended to below
export DISPLAY=:99
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
sleep 1
./mwm -d > /out/mwm.log 2>&1 &
sleep 1
xt() { xterm -xrm 'XTerm*allowTitleOps: false' -geometry "$1" -title "$2" -bg "$3" & sleep 0.7; }
xt 60x15+40+40 A '#ffdddd'
xt 60x15+200+200 B '#ddffdd'
xt 60x15+400+350 C '#ddddff'
sleep 1
geo() {
  echo "== $1" >> /out/geo.txt
  for t in A B C; do
    id=$(xdotool search --name "^$t\$" | head -1)
    [ -z "$id" ] && { echo "$t: gone" >> /out/geo.txt; continue; }
    p=$(xwininfo -tree -id "$id" | awk '/Parent window id/{print $4}')
    fh=$(xwininfo -id "$p" | awk '/Height:/{print $2}')
    echo -n "frame_h=$fh " >> /out/geo.txt
    xwininfo -id "$id" | awk -v t=$t '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}/Map State/{m=$3}END{printf "%s: %dx%d+%d+%d %s\n",t,w,h,x,y,m}' >> /out/geo.txt
  done
  import -window root "/out/$2.png" 2>/dev/null
}
win() { xdotool search --name "^$1\$" | head -1; }
# frame-relative click helpers: title bar buttons of client $1
# frame = parent of client; buttons sit right in the title bar
absgeo() { xwininfo -id "$1" | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}END{print x, y, w}'; }
click_btn() { # $1 title, $2 = min|max
  id=$(win "$1"); read X Y WIDTH < <(absgeo "$id")
  # buttons are T x T squares at the right end of the title bar (T ~ 25)
  local tb=$((Y - 12)) bx
  if [ "$2" = max ]; then bx=$((X + WIDTH - 12)); else bx=$((X + WIDTH - 37)); fi
  xdotool mousemove $bx $tb click 1; sleep 0.6
}
geo "floating start" 0-float
xdotool key super+t; sleep 1
geo "tiling on (C newest -> main)" 1-tiled
click_btn A min
geo "minimize on stack A -> shaded" 2-shaded
click_btn A max
geo "maximize A -> A main, unshaded" 3-promoted
click_btn A min
geo "minimize main A -> into stack" 4-demoted
xt 60x15+600+100 D '#ffffdd'
geo "new window D -> main" 5-newD
xdotool key super+t; sleep 1
geo "tiling off -> back to floating" 6-off
xdotool key super+t; sleep 1
geo "tiling on again" 7-on
pkill -HUP -x mwm; sleep 1.5
geo "after restart" 8-restart
grep -iE "error|warn|crash" /out/mwm.log | head

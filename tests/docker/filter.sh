#!/bin/bash
# DoNotTile / TileOnly and "Raise/Lower Tiled Windows": two xterms (class
# XTerm) and one of class Other. Runs inside the container (see run.sh):
# /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
: > /out/geo.txt # appended to below
export DISPLAY=:99 XDG_CONFIG_HOME=/tmp/cfg
mkdir -p $XDG_CONFIG_HOME/mwm
# Both set: the later line (TileOnly) wins.
cat > $XDG_CONFIG_HOME/mwm/mwmrc <<'RC'
Mwm*DoNotTile: XTerm
Mwm*TileOnly:  nothing , xterm
RC
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
sleep 1
./mwm -d > /out/mwm.log 2>&1 &
sleep 1
xt() { xterm -xrm '*allowTitleOps: false' -geometry "$1" -title "$2" -bg "$3" ${4:+-name $4 -class $4} & sleep 0.7; }
xt 60x15+40+40 A '#ffdddd'
xt 60x15+200+200 B '#ddffdd'
xt 60x15+400+350 C '#ddddff' Other
sleep 1
geo() {
  echo "== $1" >> /out/geo.txt
  for t in A B C; do
    id=$(xdotool search --name "^$t\$" | head -1)
    xwininfo -id "$id" | awk -v t=$t '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}END{printf "%s: %dx%d+%d+%d\n",t,w,h,x,y}' >> /out/geo.txt
  done
  # top first: _NET_CLIENT_LIST_STACKING is bottom to top
  local s=""
  for w in $(xprop -root _NET_CLIENT_LIST_STACKING | cut -d'#' -f2 | tr -d ','); do
    s="$(xdotool getwindowname $w) $s"; done
  echo "stacking (top first): $s focus=$(xdotool getwindowname $(xdotool getactivewindow))" >> /out/geo.txt
  import -window root "/out/$2.png" 2>/dev/null
}
rootmenu() { # $1 = mnemonic; the desktop shows in the 8 px gap at the top left
  xdotool mousemove 3 3 click 3; sleep 0.5
  import -window root "/out/menu-$1.png" 2>/dev/null
  xdotool key "$1"; sleep 0.7
}
xdotool key super+t; sleep 1
geo "TileOnly xterm: A, B tiled, C floats" 1-tileonly
xdotool windowactivate --sync "$(xdotool search --name '^C$' | head -1)"; sleep 0.5
geo "C clicked to front" 2-cfront
rootmenu a
geo "Raise Tiled Windows: A, B on top" 3-raised
rootmenu l
geo "Lower Tiled Windows: C on top" 4-lowered
# DoNotTile alone, picked up on restart
echo 'Mwm*DoNotTile: xterm' > $XDG_CONFIG_HOME/mwm/mwmrc
pkill -HUP -x mwm; sleep 1.5
geo "DoNotTile xterm after restart: C tiled alone, A, B float" 5-donottile
grep -iE "error|warn|crash|tile|TileOnly|DoNotTile" /out/mwm.log | head

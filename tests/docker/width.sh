#!/bin/bash
# Mwm*tileMainWidth and MWM_TILE_MAIN_WIDTH: width of the main tiled
# window. Runs inside the container (see run.sh).
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
: > /out/geo.txt
export DISPLAY=:99 XDG_CONFIG_HOME=/tmp/cfg
mkdir -p $XDG_CONFIG_HOME/mwm
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
sleep 1
run() { # $1 = label, rest = env
  pkill -x mwm; sleep 0.5
  env "${@:2}" ./mwm -d >> /out/mwm.log 2>&1 &
  sleep 1; xdotool key super+t; sleep 1
  echo -n "$1: " >> /out/geo.txt
  for t in A B; do
    id=$(xdotool search --name "^$t\$" | head -1)
    xwininfo -id "$id" | awk -v t=$t '/Width:/{w=$2}END{printf "%s w=%d  ",t,w}' >> /out/geo.txt
  done
  echo >> /out/geo.txt
  xdotool key super+t; sleep 0.5
}
./mwm >/dev/null 2>&1 & sleep 1
xterm -xrm '*allowTitleOps: false' -title A -geometry 60x15+40+40 & sleep 0.7
xterm -xrm '*allowTitleOps: false' -title B -geometry 60x15+200+200 & sleep 0.7
: > $XDG_CONFIG_HOME/mwm/mwmrc
run "default (55)"
echo 'Mwm*tileMainWidth: 70%' > $XDG_CONFIG_HOME/mwm/mwmrc
run "mwmrc 70%"
run "env 40 over mwmrc" MWM_TILE_MAIN_WIDTH=40
run "env 95 (ignored) -> mwmrc 70" MWM_TILE_MAIN_WIDTH=95
echo 'Mwm*tileMainWidth: abc' > $XDG_CONFIG_HOME/mwm/mwmrc
run "mwmrc abc (ignored) -> 55"
grep -i "ignoring" /out/mwm.log

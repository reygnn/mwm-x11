#!/bin/bash
# Desktop colour: rootBackground / MWM_ROOT including "none", which must
# leave a wallpaper alone. Each case starts a fresh Xvfb, sets a magenta
# root pixmap (wallpaper stand-in), starts mwm and samples the root.
# Runs inside the container (see run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
: > /out/geo.txt # appended to below
export DISPLAY=:99
px() { import -window root -crop 1x1+640+400 -depth 8 txt:- 2>/dev/null |
	grep -o '#[0-9A-F]\{6\}' | head -1; }
run() { # $1 label, $2 mwmrc line (''=no file), $3 env assignment (''=none), $4 expected
	Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 & local X=$!; sleep 1
	display -window root -size 64x64 xc:'#FF00FF' 2>/dev/null; sleep 0.3
	rm -rf /cfg; mkdir -p /cfg/mwm; [ -n "$2" ] && echo "$2" > /cfg/mwm/mwmrc
	env XDG_CONFIG_HOME=/cfg $3 ./mwm > /out/mwm.log 2>&1 & local M=$!; sleep 1
	local got log alive r
	got=$(px); log=$(grep -i ignoring /out/mwm.log | tr '\n' '|')
	# iconify an xterm so the default icon image (uses the root colour) is drawn
	xterm -geometry 40x5+50+50 -title T 2>/dev/null & sleep 0.8
	xdotool search --name '^T$' | head -1 | xargs -r xdotool windowminimize; sleep 0.5
	kill -0 $M 2>/dev/null && alive=alive || alive=DEAD
	[ "$got" = "$4" ] && [ $alive = alive ] && r=OK || r=FAIL
	printf '%-4s %-30s root=%s (want %s) mwm=%s log=[%s]\n' \
		"$r" "$1" "$got" "$4" "$alive" "$log" >> /out/geo.txt
	kill $M $X 2>/dev/null; pkill xterm; wait 2>/dev/null; sleep 0.3
}
run "nothing set -> default"       ""                             ""                 "#5B7792"
run "mwmrc colour"                 "Mwm*rootBackground: #336699"  ""                 "#336699"
run "mwmrc none"                   "Mwm*rootBackground: none"     ""                 "#FF00FF"
run "mwmrc NONE (case)"            "Mwm*rootBackground: NONE"     ""                 "#FF00FF"
run "MWM_ROOT=none, mwmrc colour"  "Mwm*rootBackground: #336699"  "MWM_ROOT=none"    "#FF00FF"
run "MWM_ROOT=#112233, mwmrc none" "Mwm*rootBackground: none"     "MWM_ROOT=#112233" "#112233"
run "MWM_ROOT=foo, mwmrc none"     "Mwm*rootBackground: none"     "MWM_ROOT=foo"     "#FF00FF"
run "MWM_ROOT=foo, nothing else"   ""                             "MWM_ROOT=foo"     "#5B7792"
run "mwmrc garbage"                "Mwm*rootBackground: blah"     ""                 "#5B7792"

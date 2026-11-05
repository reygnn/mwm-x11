#!/bin/bash
# Tab groups. Checks its results like review.sh: each line says ok or FAIL, and
# the exit code counts the failures. Runs inside the container (see run.sh):
# /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
gcc -o /usr/local/bin/xc /repo/tests/docker/xc.c -lX11 || exit 1
: > /out/geo.txt
export DISPLAY=:99
failures=0

start() {
	pkill -x xc; pkill -x mwm; pkill -x Xvfb; sleep 0.3
	rm -f /tmp/.X99-lock
	Xvfb :99 -screen 0 1280x800x24 +extension RANDR >/dev/null 2>&1 &
	sleep 0.7
	/b/mwm >>/out/mwm.log 2>&1 &
	sleep 0.5
}
win() { timeout 5 xdotool search --name "^$1\$" 2>/dev/null | head -1; }
open() { local t=$1; shift; xc win "$t" "$@" & sleep 0.4; }
close() { pkill -f "^xc win $1( |\$)"; sleep 0.4; }
key() { xdotool key "$1"; sleep 0.3; }
name_of() {
	[ "$(($1))" = 0 ] && { echo none; return; }
	xdotool getwindowname "$(($1))" 2>/dev/null || echo "?"
}
active() { name_of "$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')"; }
mapstate() { timeout 5 xwininfo -id "$1" | awk '/Map State/{print $3}'; }
# Which of the given windows are on the screen.
shown() {
	local out="" t
	for t in "$@"; do
		[ "$(mapstate "$(win "$t")")" = IsViewable ] && out="$out$t "
	done
	echo "${out% }"
}
geo() {
	timeout 5 xwininfo -id "$1" | awk '/Absolute upper-left X/{x=$4}
		/Absolute upper-left Y/{y=$4}/Width:/{w=$2}/Height:/{h=$2}
		END{printf "%dx%d+%d+%d",w,h,x,y}'
}
wm_state() { xprop -id "$1" WM_STATE | awk '/window state:/{print $3}'; }
frame_of() { timeout 5 xwininfo -tree -id "$1" | awk '/Parent window id/{print $4}'; }
alive() { pgrep -x mwm >/dev/null && echo yes || echo no; }
# Middle of tab $2 of $3 in the frame of the client $1 (title bar 25 px, with
# the menu button on the left and two buttons on the right).
tab_xy() {
	timeout 5 xwininfo -id "$1" | awk -v i="$2" -v n="$3" -v T=25 '
		/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}
		END{printf "%d %d", x + T + (i + 0.5) * (w - 3 * T) / n, y - 12}'
}
# name, want, got
check() {
	local r=ok
	[ "$2" = "$3" ] || { r=FAIL; failures=$((failures + 1)); }
	printf '%-4s %-46s want: %-22s got: %s\n' "$r" "$1" "$2" "$3" | tee -a /out/geo.txt
}

# Group of A, B, C with C shown, from a window of 400x300 at a known place.
group3() {
	open A --geo 400x300+100+100; open B --geo 250x150+100+100; key super+g
	open C --geo 500x200+100+100; key super+g
}

# ---- forming ------------------------------------------------------------------

start
open A --geo 400x300+100+100; want=$(geo "$(win A)")
open B --geo 250x150+300+300
key super+g
check "Super+G: the new one takes the other's place" "$want" "$(geo "$(win B)")"
check "... and is the one shown" B "$(shown A B)"
check "... with the focus" B "$(active)"
check "A hidden tab says Iconic" Iconic "$(wm_state "$(win A)")"
check "A hidden tab says hidden" yes \
	"$(xprop -id "$(win A)" _NET_WM_STATE | grep -q HIDDEN && echo yes || echo no)"
check "Forming it keeps the frame height" "$(echo "$want" | cut -dx -f2 | cut -d+ -f1)" \
	"$(geo "$(win B)" | cut -dx -f2 | cut -d+ -f1)"

start; open A; open D --transient "$(win A)"; key super+g
check "A dialog can't be a tab" "A D" "$(shown A D)"

start; open A; open F --fixed; key super+g
check "A window of a fixed size can't be a tab" "A F" "$(shown A F)"

# ---- switching ----------------------------------------------------------------

start; group3
check "A group of three shows the newest" C "$(shown A B C)"
key super+Tab; check "Super+Tab wraps to the first" A "$(shown A B C)"
key super+Tab; check "Super+Tab goes on" B "$(shown A B C)"
key super+shift+Tab; check "Super+Shift+Tab goes back" A "$(shown A B C)"
key super+3; check "Super+3 is the third" C "$(shown A B C)"
check "... with the focus" C "$(active)"
key super+7; check "Super+7 is no tab" C "$(shown A B C)"
g=$(geo "$(win C)"); key super+1
check "A switch keeps the place" "$g" "$(geo "$(win A)")"

start; group3
read -r x y <<<"$(tab_xy "$(win C)" 0 3)"; xdotool mousemove "$x" "$y" click 1; sleep 0.4
check "A click shows that tab" A "$(shown A B C)"
read -r x y <<<"$(tab_xy "$(win A)" 1 3)"; xdotool mousemove "$x" "$y"; xdotool click 5; sleep 0.4
check "The wheel steps" B "$(shown A B C)"
xdotool click 4; sleep 0.4
check "... both ways" A "$(shown A B C)"

start; group3; C=$(win C); read -r x y <<<"$(tab_xy "$C" 1 3)"
before=$(geo "$C")
xdotool mousemove "$x" "$y" mousedown 1; sleep 0.15; xdotool mousemove $((x + 60)) $((y + 40)); sleep 0.15
xdotool mouseup 1; sleep 0.4
check "A drag on a tab switches nothing" C "$(shown A B C)"
check "... and moves the group" no "$([ "$before" = "$(geo "$C")" ] && echo yes || echo no)"
g=$(geo "$C"); key super+1
check "... which all of it follows" "$g" "$(geo "$(win A)")"

start; group3; C=$(win C); read -r x y <<<"$(tab_xy "$C" 2 3)"
xdotool mousemove "$x" "$y" click --repeat 2 --delay 60 1; sleep 0.4
g=$(geo "$C")
check "A double click maximizes" yes "$(xprop -id "$C" _NET_WM_STATE | grep -q MAXIMIZED_HORZ && echo yes || echo no)"
key super+1
check "... and that carries over to the next tab" "$g" "$(geo "$(win A)")"
check "... also in what it says" yes "$(xprop -id "$(win A)" _NET_WM_STATE | grep -q MAXIMIZED_HORZ && echo yes || echo no)"
check "... and the one before says no more" no "$(xprop -id "$C" _NET_WM_STATE | grep -q MAXIMIZED_HORZ && echo yes || echo no)"

# ---- leaving ------------------------------------------------------------------

start; open A --geo 400x300+100+100; own=$(geo "$(win A)" | cut -d+ -f1); open B --geo 250x150+100+100; key super+g
key super+shift+g
check "Taken out: both are on the screen" "A B" "$(shown A B)"
check "... the one left has its size back" "$own" "$(geo "$(win A)" | cut -d+ -f1)"
check "... and the one taken out" 250x150 "$(geo "$(win B)" | cut -d+ -f1)"
check "... with the focus" B "$(active)"

start; group3; key super+2
close B
check "Closed: the right neighbour takes over" C "$(shown A C)"
check "... with the focus" C "$(active)"
close C
check "The last one left is a window again" A "$(shown A)"
check "... with its own size" 400x300 "$(geo "$(win A)" | cut -d+ -f1)"
check "mwm survives all that" yes "$(alive)"

start; group3
close A
check "A hidden one closing changes nothing" C "$(shown B C)"
key super+Tab
check "... and the rest still switch" B "$(shown B C)"

start; group3; A=$(win A); xdotool windowunmap "$A"
xprop -id "$A" WM_STATE >/dev/null; sleep 0.3
check "A hidden one unmapping again changes nothing" yes "$(alive)"

# ---- one window to the rest ---------------------------------------------------

start; open O --geo 300x200+700+400; group3
key alt+Tab; key alt+Tab
active_after=$(active)
check "Alt+Tab counts a group once" C "$active_after"
check "... and shows nothing else of it" C "$(shown A B C)"

start; group3; key alt+F9
check "Minimized: all of it goes" "" "$(shown A B C)"
key_win=$(win C); xdotool windowactivate "$key_win"; sleep 0.4
check "... and comes back as it was" C "$(shown A B C)"

start; group3; xdotool windowactivate "$(win A)"; sleep 0.4
check "Activating a hidden tab shows it" A "$(shown A B C)"

start; group3; xdotool windowminimize "$(win A)"; sleep 0.3
check "A hidden tab can't minimize the group" C "$(shown A B C)"

start; group3; g=$(geo "$(win C)")
xc state "$(win A)" add _NET_WM_STATE_FULLSCREEN; sleep 0.3
check "A hidden tab has no fullscreen until shown" "$g" "$(geo "$(win C)")"
xdotool key alt+F11; sleep 0.3
key super+Tab
check "While fullscreen the tabs don't switch" C "$(shown A B C)"
xdotool key alt+F11; sleep 0.3

start; group3; open D --transient "$(win A)"; sleep 0.2
check "A dialog of a hidden tab brings it" "A D" "$(shown A B C D)"
check "... and takes the focus" D "$(active)"
# The keys belong to the group's window, not to the dialog.
xdotool windowactivate "$(win A)"; sleep 0.3; key super+2
check "Its dialog goes with the tab" B "$(shown A B C D)"
key super+1
check "... and comes back with it" "A D" "$(shown A B C D)"

start; group3; key alt+F9; open D --transient "$(win A)"; sleep 0.2
check "With the group an icon, the dialog waits" "" "$(shown A B C D)"
xdotool windowactivate "$(win C)"; sleep 0.5
check "... and shows up with its tab" "A D" "$(shown A B C D)"

# ---- tiling -------------------------------------------------------------------

start; open X; group3; key super+t
C=$(win C); g=$(geo "$C")
check "Tiled: the group is one main window" yes "$(echo "$g" | grep -q '^[0-9]*x[0-9]*+5[0-9][0-9]+' && echo yes || echo no)"
key super+1
check "... and a switch stays in the cell" "$(echo "$g" | cut -d+ -f2-)" "$(geo "$(win A)" | cut -d+ -f2-)"
key super+t
check "Off again: mwm survives" yes "$(alive)"

# ---- taking one window along --------------------------------------------------

take() { # x y to_x to_y button
	xdotool mousemove "$1" "$2" mousedown "$5"; sleep 0.15
	xdotool mousemove $((($1 + $3) / 2)) $((($2 + $4) / 2)); sleep 0.15
	xdotool mousemove "$3" "$4"; sleep 0.2; xdotool mouseup "$5"; sleep 0.5
}

start; group3; read -r x y <<<"$(tab_xy "$(win C)" 0 3)"
take "$x" "$y" 900 600 2
check "Middle drag to the desktop takes that tab out" "A C" "$(shown A B C)"
check "... at its own size" 400x300 "$(geo "$(win A)" | cut -d+ -f1)"
check "... where it was dropped" yes "$(geo "$(win A)" | awk -F+ '{print ($2 > 500 && $3 > 400) ? "yes" : "no"}')"

start; open O --geo 300x200+800+400; group3; O=$(win O); read -r ox oy _ <<<"$(geo "$O" | awk -F'[x+]' '{print $3+20, $4+20}')"
read -r x y <<<"$(tab_xy "$(win C)" 2 3)"
take "$x" "$y" "$ox" "$oy" 2
check "Dropped on a window it joins that" "B C" "$(shown A B C O)"

start; open O --geo 300x200+800+400; group3; O=$(win O); read -r ox oy _ <<<"$(geo "$O" | awk -F'[x+]' '{print $3+20, $4+20}')"
read -r x y <<<"$(tab_xy "$(win C)" 2 3)"
xdotool keydown ctrl; take "$x" "$y" "$ox" "$oy" 1; xdotool keyup ctrl; sleep 0.2
check "Ctrl+left drag does the same" "B C" "$(shown A B C O)"

start; group3; read -r x y <<<"$(tab_xy "$(win C)" 0 3)"
xdotool mousemove "$x" "$y" mousedown 2; sleep 0.15; xdotool mousemove 900 600; sleep 0.2
key Escape; xdotool mouseup 2; sleep 0.4
check "Escape cancels taking along" C "$(shown A B C)"

start; open O --geo 300x200+100+100; open A --geo 300x200+100+100; A=$(win A)
read -r x y <<<"$(timeout 5 xwininfo -id "$A" | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width:/{w=$2}END{printf "%d %d",x+w/2,y-12}')"
xdotool mousemove "$x" "$y" click 2; sleep 0.4
check "A middle click still lowers" O "$(active)"

# ---- the menu -----------------------------------------------------------------

start; group3; key alt+space; key e
check "The menu's Next Tab" A "$(shown A B C)"
key alt+space; key d
check "The menu's Detach Tab" "A B" "$(shown A B C)"

# ---- too many, restart, quit --------------------------------------------------

start
open T1 --geo 400x200+100+100
for n in T2 T3 T4 T5 T6 T7; do open $n --geo 400x200+100+100; key super+g; done
check "Seven tabs in a narrow frame" T7 "$(shown T1 T2 T3 T4 T5 T6 T7)"
key super+2; check "... still reach every one" T2 "$(shown T1 T2 T3 T4 T5 T6 T7)"
read -r x y <<<"$(tab_xy "$(win T2)" 0 3)"; xdotool mousemove "$x" "$y"; xdotool click 5; sleep 0.3
check "... also by the wheel" T3 "$(shown T1 T2 T3 T4 T5 T6 T7)"
import -window root /out/too-many.png 2>/dev/null

start; group3; key super+2; g=$(geo "$(win B)")
import -window root /out/three.png 2>/dev/null
pkill -HUP -x mwm; sleep 0.9
check "A restart keeps the group" B "$(shown A B C)"
check "... and the place" "$g" "$(geo "$(win B)")"
key super+Tab; check "... and the order" C "$(shown A B C)"
close C
check "... and it still works" B "$(shown A B)"
key super+shift+g
check "... with the sizes it came with" 250x150 "$(geo "$(win B)" | cut -d+ -f1)"

start; group3; key ctrl+alt+BackSpace; sleep 0.3
check "Quitting leaves every window mapped" "A B C" "$(shown A B C)"

pkill -x xc; pkill -x mwm; pkill -x Xvfb
echo "failures: $failures" | tee -a /out/geo.txt
exit "$failures"

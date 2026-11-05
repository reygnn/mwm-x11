#!/bin/bash
# Programs from mwmrc: root menu entries, run dialog and screen saver. Each line
# says ok or FAIL, and the exit code counts the failures.
# Runs inside the container (see run.sh): /repo (ro) = source, /out = results.
cp -r /repo /b && cd /b && make clean >/dev/null && make -s CFLAGS="-O2 -g" || exit 1
: > /out/geo.txt
export DISPLAY=:99 XDG_CONFIG_HOME=/tmp/cfg
mkdir -p /tmp/cfg/mwm
failures=0

start() {
	pkill -x mwm; pkill -x Xvfb; sleep 0.3
	rm -f /tmp/.X99-lock /tmp/ran-*
	Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
	sleep 0.7
	/b/mwm >/tmp/mwm.log 2>&1 &
	sleep 0.5
}
ran() { sleep 0.4; [ -e "/tmp/ran-$1" ] && echo yes || echo no; }
alive() { pgrep -x mwm >/dev/null && echo yes || echo no; }
check() {
	local r=ok
	[ "$2" = "$3" ] || { r=FAIL; failures=$((failures + 1)); }
	printf '%-4s %-46s want: %-6s got: %s\n' "$r" "$1" "$2" "$3" | tee -a /out/geo.txt
}

: > /tmp/cfg/mwm/mwmrc
start
xdotool mousemove 900 700 click 2; sleep 0.3
check "No run dialog set: the middle button is harmless" yes "$(alive)"
xdotool key super+Escape; sleep 0.3
check "No screen saver set: Super+Esc is harmless" yes "$(alive)"

cat > /tmp/cfg/mwm/mwmrc <<'RC'
Mwm*rootMenuItem: Xylophone: touch /tmp/ran-x
Mwm*rootMenuItem: Noises and Yodels: touch "/tmp/ran-with: colon"
Mwm*rootMenuItem: broken line
Mwm*runDialog:    touch /tmp/ran-dialog
Mwm*screenSaver:  touch /tmp/ran-saver
RC
start
xdotool mousemove 900 700 click 2
check "The middle button runs runDialog" yes "$(ran dialog)"
xdotool key super+Escape
check "Super+Esc runs screenSaver" yes "$(ran saver)"
xdotool mousemove 900 300 click 3; sleep 0.3; xdotool key x
check "A root menu entry runs by its first letter" yes "$(ran x)"
# N is New Window's, so the second one goes by a later letter; the command may
# have colons, as the label ends at the first.
xdotool mousemove 900 300 click 3; sleep 0.3; xdotool key o
check "The next letter free, and a colon in the command" yes "$(ran 'with: colon')"
check "A line without a command is logged" yes \
	"$(grep -q 'expected Mwm\*rootMenuItem' /tmp/mwm.log && echo yes || echo no)"

for i in $(seq 1 25); do echo "Mwm*rootMenuItem: Item$i: touch /tmp/ran-$i"; done > /tmp/cfg/mwm/mwmrc
start
xdotool mousemove 900 10 click 3; sleep 0.3; xdotool key Escape; sleep 0.2
check "25 entries: mwm survives the menu" yes "$(alive)"
check "... and logs those it leaves out" yes \
	"$(grep -q 'only the first 20' /tmp/mwm.log && echo yes || echo no)"

pkill -x mwm; pkill -x Xvfb
echo "failures: $failures" | tee -a /out/geo.txt
exit "$failures"

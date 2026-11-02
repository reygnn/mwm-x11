#!/bin/sh
# Build and test mwm in Docker under Xvfb, with xterm and xdotool.
#   tests/docker/run.sh [basic|edge]   (default: both)
# Results (geometry log, screenshots, mwm log) land in tests/docker/out/.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
docker build -q -t mwm-test "$here" >/dev/null
for t in ${1:-basic edge}; do
	mkdir -p "$here/out/$t"
	echo "== $t"
	docker run --rm -v "$repo":/repo:ro -v "$here/$t.sh":/t.sh:ro \
		-v "$here/out/$t":/out mwm-test /t.sh || true
	[ -f "$here/out/$t/geo.txt" ] && cat "$here/out/$t/geo.txt"
done

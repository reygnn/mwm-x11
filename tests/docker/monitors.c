/*
 * LD_PRELOAD shim for review.sh: Xvfb offers one monitor and no way to add
 * another, so XRRGetMonitors answers from the file named by MWM_TEST_MONITORS,
 * one "x y w h" per line. Everything else in mwm stays the real thing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <X11/extensions/Xrandr.h>

XRRMonitorInfo *XRRGetMonitors(Display *dpy, Window window, Bool get_active,
	int *nmonitors)
{
	(void)dpy;
	(void)window;
	(void)get_active;
	XRRMonitorInfo *m = calloc(16, sizeof(*m));
	FILE *f = fopen(getenv("MWM_TEST_MONITORS"), "r");
	int n = 0;
	while (m && f && n < 16 && fscanf(f, "%d %d %d %d", &m[n].x, &m[n].y,
			&m[n].width, &m[n].height) == 4)
		n++;
	if (f)
		fclose(f);
	*nmonitors = n;
	return m;
}

void XRRFreeMonitors(XRRMonitorInfo *monitors)
{
	free(monitors);
}

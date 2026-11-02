/*
 * Wireframe move and resize, as Motif mwm does with moveOpaque: False
 * and twm does by default. While a window is dragged only a thin outline
 * follows the pointer; the window itself moves and resizes once, when the
 * drag ends. Over a remote display (xrdp, VNC) that is a few lines per
 * motion event instead of the whole window being repainted and sent.
 *
 * The outline is made of thin override-redirect strips (four sides and
 * the title bar line), each with a background pixmap only as big as the
 * strip itself. A move just moves them; a resize repaints only the strips
 * whose size changed. No server grab, no XOR drawing, no SHAPE needed.
 * `Mwm*moveOpaque: true` in mwmrc, or MWM_MOVE_OPAQUE=1, brings back
 * opaque dragging.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "mwm.h"

#define OUTLINE_W 3 /* line thickness */

enum { STRIP_TOP, STRIP_BOTTOM, STRIP_LEFT, STRIP_RIGHT, STRIP_SEP, N_STRIPS };

static Window strip[N_STRIPS];
/* Root geometry each strip has now; w == 0 while it is unmapped. */
static struct rect shown[N_STRIPS];

/* 1 or 0, or -1 when s is unset or no boolean. */
static int parse_bool(const char *name, const char *s)
{
	static const char *warned; /* log each bad value once, not per drag */
	if (!s || !*s)
		return -1;
	if (!strcasecmp(s, "true") || !strcasecmp(s, "on") ||
			!strcasecmp(s, "yes") || !strcmp(s, "1"))
		return 1;
	if (!strcasecmp(s, "false") || !strcasecmp(s, "off") ||
			!strcasecmp(s, "no") || !strcmp(s, "0"))
		return 0;
	if (s != warned) {
		wm_log("ignoring %s value %s, not a boolean", name, s);
		warned = s;
	}
	return -1;
}

/* Whether a move or resize starting now should use the outline. The
 * environment wins over mwmrc, unless it holds no boolean. */
bool outline_wanted(void)
{
	int opaque = parse_bool("MWM_MOVE_OPAQUE", getenv("MWM_MOVE_OPAQUE"));
	if (opaque < 0)
		opaque = parse_bool("moveOpaque", config_get("moveOpaque"));
	return opaque != 1;
}

/* Paint strip s (relative to a w x h box) as its part of the whole
 * outline: a small Motif bevel in the window's colour, so the lines read
 * on light and dark backgrounds alike. The whole ring is drawn shifted
 * into the strip's pixmap; the server clips it to the strip. */
static void render_strip(Window win, struct rect s, int w, int h, int sep,
	const struct shade *sh)
{
	const int t = OUTLINE_W;
	struct canvas cv;
	canvas_begin(&cv, s.w, s.h);
	fill_rect(&cv, sh->bg, 0, 0, s.w, s.h);
	draw_bevel(&cv, -s.x, -s.y, w, h, 1, sh, false);
	draw_bevel(&cv, t - 1 - s.x, t - 1 - s.y, w - 2 * (t - 1),
		h - 2 * (t - 1), 1, sh, true);
	if (sep)
		draw_bevel(&cv, t - s.x, sep - s.y, w - 2 * t, t, 1, sh, false);
	canvas_commit(&cv, win);
}

/* Show the outline at the frame box c has now. */
void outline_show(struct client *c)
{
	struct rect b = client_box(c);
	const int t = OUTLINE_W;
	b.w = MAX(b.w, 2 * t + 1);
	b.h = MAX(b.h, 2 * t + 1);
	/* The line between title bar and client, as twm and mwm draw it. */
	int sep = 0;
	if (frame_title(c)) {
		sep = frame_border(c) + frame_title(c) - (t + 1) / 2;
		if (sep < t || sep + t > b.h - t)
			sep = 0;
	}
	const struct rect rel[N_STRIPS] = {
		[STRIP_TOP] = { 0, 0, b.w, t },
		[STRIP_BOTTOM] = { 0, b.h - t, b.w, t },
		[STRIP_LEFT] = { 0, t, t, b.h - 2 * t },
		[STRIP_RIGHT] = { b.w - t, t, t, b.h - 2 * t },
		[STRIP_SEP] = { t, sep, sep ? b.w - 2 * t : 0, t },
	};
	const struct shade *sh = c->active_shade ? c->active_shade :
		&theme.active;

	for (int i = 0; i < N_STRIPS; i++) {
		struct rect s = rel[i];
		if (!s.w) {
			if (shown[i].w)
				XUnmapWindow(wm.dpy, strip[i]);
			shown[i].w = 0;
			continue;
		}
		int x = b.x + s.x, y = b.y + s.y;
		if (!strip[i]) {
			XSetWindowAttributes a = {
				.override_redirect = True,
				.save_under = True,
				.background_pixel = sh->bg,
			};
			strip[i] = XCreateWindow(wm.dpy, wm.root, x, y, s.w,
				s.h, 0, wm.depth, InputOutput, wm.visual,
				CWOverrideRedirect | CWSaveUnder | CWBackPixel,
				&a);
		}
		/* A strip's picture depends only on its size, so a pure
		 * move just moves it. */
		bool was_mapped = shown[i].w != 0;
		if (s.w != shown[i].w || s.h != shown[i].h) {
			XMoveResizeWindow(wm.dpy, strip[i], x, y, s.w, s.h);
			render_strip(strip[i], s, b.w, b.h, sep, sh);
		} else if (x != shown[i].x || y != shown[i].y) {
			XMoveWindow(wm.dpy, strip[i], x, y);
		}
		shown[i] = (struct rect){ x, y, s.w, s.h };
		if (!was_mapped)
			XMapRaised(wm.dpy, strip[i]);
	}
}

/* Back on top, e.g. above a snap preview that was just mapped. */
void outline_raise(void)
{
	for (int i = 0; i < N_STRIPS; i++)
		if (shown[i].w)
			XRaiseWindow(wm.dpy, strip[i]);
}

void outline_hide(void)
{
	for (int i = 0; i < N_STRIPS; i++)
		if (shown[i].w)
			XUnmapWindow(wm.dpy, strip[i]);
	/* Forgetting the sizes repaints every strip on the next show, in
	 * the colour of whichever window is dragged then. */
	memset(shown, 0, sizeof(shown));
}

/* Called by client_configure and client_move: during an outline grab the
 * grabbed window stays where it is and only the outline follows its new
 * geometry. end_grab applies it. */
bool outline_defer(struct client *c)
{
	if (!wm.grab_outline || c != wm.grabbed)
		return false;
	wm.grab_deferred = true;
	outline_show(c);
	return true;
}

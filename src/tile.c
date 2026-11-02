/*
 * Tiling mode: the main window takes the right part of the work area,
 * the other windows share the left part, one above the other. Gaps
 * around and between the windows keep the desktop (and its root menu)
 * within reach.
 *
 * There is no dragging: tiled windows keep their full decoration, but
 * moving and resizing do nothing. Minimize on the main window moves it
 * down into the stack, on a stack window it shades it (rolls it up to
 * its title bar); maximize on a stack window makes it the main window.
 * A new window, or one back from an icon, becomes the main window.
 *
 * Only normal top-level windows take part. Dialogs, windows of a fixed
 * size, fullscreen and minimized windows float as before. The layout
 * covers the first monitor.
 *
 * Super+T or "Tile Windows" in the root menu switches it; Mwm*tiling:
 * true in mwmrc (or MWM_TILING=1) turns it on from the start, and a
 * restart keeps the current choice.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <X11/Xatom.h>
#include "mwm.h"

#define TILE_GAP 8         /* px around and between windows */
#define TILE_MAIN_PCT 55   /* share of the width for the main window */

static bool tileable(struct client *c)
{
	return c->type == WT_NORMAL && !c->minimized && !c->fullscreen &&
		!c->fixed && !transient_parent(c);
}

static int by_seq(const void *a, const void *b)
{
	const struct client *x = *(struct client *const *)a;
	const struct client *y = *(struct client *const *)b;
	return (x->tile_seq > y->tile_seq) - (x->tile_seq < y->tile_seq);
}

/* The tiled windows in layout order, main window first; *n of them.
 * The caller frees the array. */
static struct client **tiled_windows(int *n)
{
	struct client **v = xmalloc(sizeof(*v) * (wm.n_clients + 1));
	*n = 0;
	for (int i = 0; i < wm.n_clients; i++)
		if (wm.clients[i]->tiled)
			v[(*n)++] = wm.clients[i];
	qsort(v, *n, sizeof(*v), by_seq);
	return v;
}

static void publish(void)
{
	long on = wm.tiling;
	XChangeProperty(wm.dpy, wm.root, atoms[MWM_TILING], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&on, 1);
}

/* Before the windows are adopted: a restart (or a new mwm taking over)
 * keeps what the last one had, otherwise the environment and mwmrc. */
void tile_init(void)
{
	long on;
	int v;
	if (get_cardinal(wm.root, atoms[MWM_TILING], &on))
		wm.tiling = on != 0;
	else if ((v = config_bool("MWM_TILING", getenv("MWM_TILING"))) >= 0)
		wm.tiling = v;
	else
		wm.tiling = config_bool("tiling", config_get("tiling")) == 1;
	publish();
}

void tile_toggle(void)
{
	wm.tiling = !wm.tiling;
	publish();
	tile_arrange();
}

/* Lay out the stack windows v[0..k) in the column at x, width w: shaded
 * ones take their title bar, the others share what is left. */
static void layout_stack(struct client **v, int k, struct rect a, int x,
	int w)
{
	int shaded = 0, n_open = 0;
	for (int i = 0; i < k; i++) {
		if (v[i]->shaded)
			shaded += frame_extra_h(v[i]);
		else
			n_open++;
	}
	int room = a.h - 2 * TILE_GAP - (k - 1) * TILE_GAP - shaded;
	int each = n_open ? room / n_open : 0;
	int y = a.y + TILE_GAP, open_left = n_open;
	for (int i = 0; i < k; i++) {
		struct client *c = v[i];
		int h;
		if (c->shaded) {
			h = frame_extra_h(c);
		} else {
			/* The last open window takes the remainder. */
			h = --open_left ? each : room - each * (n_open - 1);
			h = MAX(h, frame_extra_h(c) + 1);
		}
		c->tile = (struct rect){ x, y, w, h };
		c->tile_main = false;
		y += h + TILE_GAP;
	}
}

void tile_arrange(void)
{
	/* Leaving or entering the layout reconfigures windows, which must
	 * not start another round from inside this one. */
	static bool busy;
	if (busy)
		return;
	busy = true;

	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		bool want = wm.tiling && tileable(c);
		if (want != c->tiled)
			client_set_tiled(c, want, true);
	}

	int n;
	struct client **v = tiled_windows(&n);
	if (n) {
		struct rect a = monitor_usable_at(wm.mon[0].x, wm.mon[0].y);
		int top = a.y + TILE_GAP, h = a.h - 2 * TILE_GAP;
		if (n == 1) {
			v[0]->tile = (struct rect){ a.x + TILE_GAP, top,
				a.w - 2 * TILE_GAP, h };
		} else {
			int inner = a.w - 3 * TILE_GAP;
			int mw = inner * TILE_MAIN_PCT / 100, sw = inner - mw;
			v[0]->tile = (struct rect){ a.x + 2 * TILE_GAP + sw,
				top, mw, h };
			layout_stack(v + 1, n - 1, a, a.x + TILE_GAP, sw);
		}
		/* The main window is never shaded. */
		v[0]->tile_main = true;
		v[0]->shaded = false;
		for (int i = 0; i < n; i++)
			client_apply_state(v[i]);
	}
	free(v);
	busy = false;
}

bool tile_is_main(struct client *c)
{
	return c->tiled && c->tile_main;
}

bool tile_has_stack(void)
{
	int n = 0;
	for (int i = 0; i < wm.n_clients && n < 2; i++)
		n += wm.clients[i]->tiled;
	return n > 1;
}

static void swap_seq(struct client *a, struct client *b)
{
	long t = a->tile_seq;
	a->tile_seq = b->tile_seq;
	b->tile_seq = t;
}

/* Minimize in the layout: the main window goes into the stack, its
 * place taken by the first stack window; a stack window shades or
 * unshades. */
void tile_iconify(struct client *c)
{
	if (!c->tiled)
		return;
	if (!c->tile_main) {
		c->shaded = !c->shaded;
		tile_arrange();
		return;
	}
	int n;
	struct client **v = tiled_windows(&n);
	if (n > 1) {
		swap_seq(v[0], v[1]);
		tile_arrange();
	}
	free(v);
}

/* Maximize in the layout: a stack window trades places with the main
 * window. The main window has nowhere further to go. */
void tile_promote(struct client *c)
{
	if (!c->tiled || c->tile_main)
		return;
	int n;
	struct client **v = tiled_windows(&n);
	if (n > 1) {
		c->shaded = false;
		swap_seq(v[0], c);
		tile_arrange();
		client_focus(c);
	}
	free(v);
}

void tile_unshade(struct client *c)
{
	if (!c->shaded)
		return;
	c->shaded = false;
	tile_arrange();
}

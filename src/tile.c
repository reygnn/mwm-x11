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
 * size, fullscreen and minimized windows float as before, and so do
 * programs left out by Mwm*DoNotTile or Mwm*TileOnly. The layout
 * covers the first monitor. "Raise/Lower Tiled Windows" in the root
 * menu puts the layout above or below the floating windows.
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

/* Normal top-level windows that may be resized; nofit ones (their
 * minimum size does not fit their cell) float for this round. */
static bool tileable(struct client *c)
{
	return c->type == WT_NORMAL && !c->minimized && !c->fullscreen &&
		!c->fixed && (c->base_funcs & FUNC_RESIZE) &&
		!transient_parent(c) && !c->tile_nofit && !c->no_tile;
}

/* Layout order; ties (possible right after a restart) by management
 * order, so the layout does not change with the stacking order. */
static int by_seq(const void *a, const void *b)
{
	const struct client *x = *(struct client *const *)a;
	const struct client *y = *(struct client *const *)b;
	if (x->tile_seq != y->tile_seq)
		return x->tile_seq < y->tile_seq ? -1 : 1;
	return (x->serial > y->serial) - (x->serial < y->serial);
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

/* Before the windows are adopted: a restart keeps what the last mwm had
 * (tile_handover left it on the root), otherwise the environment and
 * mwmrc decide. The property is gone afterwards either way, so nothing
 * stale is left for a later mwm. */
void tile_init(void)
{
	long on;
	int v;
	bool restarted = getenv("MWM_RESTARTED") &&
		get_cardinal(wm.root, atoms[MWM_TILING], &on);
	XDeleteProperty(wm.dpy, wm.root, atoms[MWM_TILING]);
	if (restarted)
		wm.tiling = on != 0;
	else if ((v = config_bool("MWM_TILING", getenv("MWM_TILING"))) >= 0)
		wm.tiling = v;
	else
		wm.tiling = config_bool("tiling", config_get("tiling")) == 1;
}

/* On restart: the mode for the next mwm. */
void tile_handover(void)
{
	long on = wm.tiling;
	XChangeProperty(wm.dpy, wm.root, atoms[MWM_TILING], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&on, 1);
}

void tile_toggle(void)
{
	wm.tiling = !wm.tiling;
	tile_arrange();
}

bool tile_can_shade(struct client *c)
{
	return frame_title(c) > 0;
}

/* Smallest frame c can have, from its size hints. */
static void min_frame(struct client *c, int *w, int *h)
{
	int cw = 1, ch = 1;
	client_apply_size_hints(c, &cw, &ch, true);
	*w = cw + frame_extra_w(c);
	*h = ch + frame_extra_h(c);
}

/* Lay out the stack windows v[0..k) in the column at x, width w: shaded
 * ones take their title bar, the open ones share what is left. When
 * that is too little for them, open ones from the bottom up are shaded
 * (the focused one last). Returns false if the column still cannot
 * hold them. */
static bool layout_stack(struct client **v, int k, struct rect a, int x,
	int w)
{
	for (;;) {
		int shaded = 0, n_open = 0, need = 0;
		for (int i = 0; i < k; i++) {
			int mw, mh;
			min_frame(v[i], &mw, &mh);
			if (v[i]->shaded) {
				shaded += frame_extra_h(v[i]);
			} else {
				n_open++;
				need = MAX(need, mh);
			}
		}
		int room = a.h - 2 * TILE_GAP - (k - 1) * TILE_GAP - shaded;
		int each = n_open ? room / n_open : 0;
		if (n_open && each < need) {
			/* Shade one more, if any can be. */
			struct client *pick = NULL;
			for (int i = k - 1; i >= 0 && !pick; i--)
				if (!v[i]->shaded && tile_can_shade(v[i]) &&
						v[i] != wm.focus)
					pick = v[i];
			for (int i = k - 1; i >= 0 && !pick; i--)
				if (!v[i]->shaded && tile_can_shade(v[i]))
					pick = v[i];
			if (pick) {
				pick->shaded = true;
				continue;
			}
		}
		if (room < 0 || (n_open && each < need))
			return false;
		int y = a.y + TILE_GAP, open_left = n_open;
		for (int i = 0; i < k; i++) {
			struct client *c = v[i];
			int h = frame_extra_h(c);
			if (!c->shaded) /* the last open one gets the rest */
				h = --open_left ? each :
					room - each * (n_open - 1);
			c->tile = (struct rect){ x, y, w, h };
			c->tile_main = false;
			y += h + TILE_GAP;
		}
		return true;
	}
}

/* Cells for v[0..n), main window first. Returns a window that cannot
 * fit its cell, or NULL; *overflow says the stack column is full even
 * with every window in it shaded. */
static struct client *plan(struct client **v, int n, bool *overflow)
{
	*overflow = false;
	struct rect a = monitor_usable_at(wm.mon[0].x, wm.mon[0].y);
	/* Icons along the bottom stay in view: the layout ends above the
	 * highest row of them (they keep their own 8 px margin). */
	int bottom = a.y + a.h;
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (c->minimized && c->icon_win && !c->hidden_with_parent &&
				c->icon_placed && c->icon_x >= a.x &&
				c->icon_x < a.x + a.w && c->icon_y >= a.y &&
				c->icon_y < bottom)
			bottom = c->icon_y;
	}
	a.h = MAX(bottom - a.y, 2 * TILE_GAP + 1);
	int top = a.y + TILE_GAP, h = a.h - 2 * TILE_GAP;
	/* The main window is never shaded. */
	v[0]->shaded = false;
	if (n == 1) {
		v[0]->tile = (struct rect){ a.x + TILE_GAP, top,
			a.w - 2 * TILE_GAP, h };
	} else {
		int inner = a.w - 3 * TILE_GAP;
		int mw = inner * TILE_MAIN_PCT / 100, sw = inner - mw;
		v[0]->tile = (struct rect){ a.x + 2 * TILE_GAP + sw, top,
			mw, h };
		if (!layout_stack(v + 1, n - 1, a, a.x + TILE_GAP, sw)) {
			*overflow = true;
			return v[n - 1];
		}
	}
	v[0]->tile_main = true;
	for (int i = 0; i < n; i++) {
		int mw, mh;
		min_frame(v[i], &mw, &mh);
		if (mw > v[i]->tile.w ||
				(!v[i]->shaded && mh > v[i]->tile.h))
			return v[i];
	}
	return NULL;
}

void tile_arrange(void)
{
	/* Leaving or entering the layout reconfigures windows, which must
	 * not start another round from inside this one. */
	static bool busy;
	if (busy)
		return;
	busy = true;

	for (int i = 0; i < wm.n_clients; i++)
		wm.clients[i]->tile_nofit = false;
	for (;;) {
		for (int i = 0; i < wm.n_clients; i++) {
			struct client *c = wm.clients[i];
			bool want = wm.tiling && tileable(c);
			if (want != c->tiled)
				client_set_tiled(c, want, true);
		}
		int n;
		struct client **v = tiled_windows(&n);
		bool overflow;
		struct client *misfit = n ? plan(v, n, &overflow) : NULL;
		if (misfit && overflow) {
			/* No room even rolled up: the oldest one becomes an
			 * icon, still in reach, and the newest when restored. */
			free(v);
			client_minimize(misfit);
			continue;
		}
		if (misfit) {
			/* Too large for its cell: it floats, and the others
			 * share its room. */
			misfit->tile_nofit = true;
			free(v);
			continue;
		}
		for (int i = 0; i < n; i++)
			client_apply_state(v[i]);
		free(v);
		break;
	}
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
		if (!tile_can_shade(c))
			return; /* no title bar to roll up to */
		c->shaded = !c->shaded;
		tile_arrange();
		/* Nothing to type into any more. */
		if (c->shaded && wm.focus == c)
			client_focus_next(c);
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

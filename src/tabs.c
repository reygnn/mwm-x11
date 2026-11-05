/*
 * Tab groups, as in mwc (see its docs/TABBED.md). Several windows share one
 * place on the screen, with their titles as tabs in the title bar, and one
 * of them is shown.
 *
 * The shown client is the window, the others don't exist for the rest of
 * mwm: moving, maximizing, tiling and Alt+Tab only ever see the shown one.
 * What makes up a window's place moves over to the next one when it is
 * shown, see move_place(). A hidden client is unmapped, with its frame, and
 * WM_STATE says Iconic, which is true enough and what programs handle.
 *
 * There is no group of one. A restart keeps the groups: _MWM_HANDOVER carries
 * them over, and tabs_restore_groups() puts them together again.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "mwm.h"

static bool tab_material(struct client *c)
{
	return c->type == WT_NORMAL && !c->fixed && !transient_parent(c) &&
		(c->decor & DECOR_TITLE);
}

bool tabs_can_join(struct client *c)
{
	return !c->minimized && !c->tab_hidden && !c->fullscreen &&
		tab_material(c);
}

/*
 * Everything that makes up the window's place on the screen. A new field of
 * struct client that belongs to the place rather than the client has to be
 * added here.
 */
static void move_place(struct client *to, struct client *from)
{
	to->x = from->x;
	to->y = from->y;
	to->saved = from->saved;
	to->fit_x = from->fit_x;
	to->fit_y = from->fit_y;
	to->maxed_v = from->maxed_v;
	to->maxed_h = from->maxed_h;
	to->snap = from->snap;
	to->tiled = from->tiled;
	to->tile = from->tile;
	to->tile_main = from->tile_main;
	to->tile_seq = from->tile_seq;
	to->tile_back = from->tile_back;
	to->shaded = from->shaded;
	to->auto_shaded = from->auto_shaded;

	from->maxed_v = from->maxed_h = false;
	from->snap = SNAP_NONE;
	from->tiled = false;
	from->tile_main = false;
	from->tile_back = false;
	from->shaded = false;
	from->auto_shaded = false;
}

/* A view on its way out, gone, can't be told anything any more, but its
 * place still counts. */
static void switch_to(struct tab_group *g, struct client *to,
	struct client *from, bool gone)
{
	/* The focus stays with the group, also if a dialog of the old one
	 * had it. */
	bool focus = wm.focus == from ||
		(wm.focus && client_is_transient_of(wm.focus, from));
	if (gone) {
		if (wm.focus == from)
			wm.focus = NULL;
	} else {
		grab_give_way(from);
		if (wm.menu.open && wm.menu.target == from)
			menu_close();
	}

	/* The same frame box, whatever frame the new one has. Fullscreen is the
	 * one state that stays behind: it has no tabs to come back to. */
	struct rect box = client_box(from);
	bool from_fullscreen = from->fullscreen;
	move_place(to, from);
	if (from_fullscreen) {
		to->x = from->saved.x;
		to->y = from->saved.y;
		box = (struct rect){ to->x, to->y,
			from->saved.w + frame_extra_w(to),
			from->saved.h + frame_extra_h(to) };
	}
	from->tab_hidden = true;
	to->tab_hidden = false;
	g->shown = to;

	client_update_decorations(to);
	client_take_stack_place(to, from);
	if (to->tiled) {
		client_apply_state(to);
	} else if (to->maxed_v || to->maxed_h || to->snap != SNAP_NONE) {
		client_refit(to);
	} else {
		int w = box.w - frame_extra_w(to), h = box.h - frame_extra_h(to);
		client_apply_size_hints(to, &w, &h, false);
		to->w = w;
		to->h = h;
		client_configure(to);
	}
	restack();

	/* The new one first, so nothing shows through in between. */
	client_tab_show(to);
	client_dialogs_show(to, true);
	if (!gone) {
		client_dialogs_show(from, false);
		client_tab_hide(from);
		client_update_decorations(from);
		ewmh_set_allowed_actions(from);
	}
	ewmh_set_allowed_actions(to);
	ewmh_set_state(to);
	frame_render(to);

	if (focus)
		client_focus(to);
	tile_arrange();
}

/* The neighbour taking over: the one to the right, or else to the left. */
static struct client *neighbour(struct client *c)
{
	struct tab_group *g = c->group;
	int i = tabs_index(c);
	return g->members[i + 1 < g->n ? i + 1 : i - 1];
}

/* A group takes the size of the window it began with, and only a window
 * that floats has its real size at hand: otherwise it is what it goes back
 * to. */
static void remember_own_size(struct client *c)
{
	bool state = c->maxed_v || c->maxed_h || c->snap != SNAP_NONE ||
		c->tiled || c->fullscreen;
	c->tab_own_w = state ? c->saved.w : c->w;
	c->tab_own_h = state ? c->saved.h : c->h;
}

void tabs_own_size(struct client *c, int *w, int *h)
{
	*w = c->group ? c->tab_own_w : c->w;
	*h = c->group ? c->tab_own_h : c->h;
}

/* Out of a group, with no one to share with. In a state, it is what it goes
 * back to. */
static void back_to_own_size(struct client *c)
{
	int w = c->tab_own_w, h = c->tab_own_h;
	if (c->maxed_v || c->maxed_h || c->snap != SNAP_NONE || c->tiled ||
			c->fullscreen) {
		c->saved.w = w;
		c->saved.h = h;
		return;
	}
	client_apply_size_hints(c, &w, &h, false);
	c->w = w;
	c->h = h;
	c->saved = (struct rect){ c->x, c->y, w, h };
	client_configure(c);
}

/* Out of the list, which ends the group when one is left. It has been
 * hidden by now. */
static void remove_member(struct client *c)
{
	struct tab_group *g = c->group;
	int i = tabs_index(c);
	memmove(&g->members[i], &g->members[i + 1],
		sizeof(*g->members) * (g->n - i - 1));
	g->n--;
	c->group = NULL;
	c->tab_hidden = false;
	if (g->n > 1) {
		frame_render(g->shown);
		return;
	}
	struct client *last = g->members[0];
	last->group = NULL;
	last->tab_hidden = false;
	free(g->members);
	free(g);
	back_to_own_size(last);
	frame_render(last);
}

void tabs_show(struct client *c)
{
	struct tab_group *g = c->group;
	if (g && g->shown != c)
		switch_to(g, c, g->shown, false);
}

void tabs_bring(struct client *c)
{
	struct tab_group *g = c->group;
	if (!g || g->shown == c)
		return;
	if (g->shown->fullscreen)
		client_set_fullscreen(g->shown, false);
	if (g->shown->minimized)
		client_restore(g->shown);
	/* Restoring may have brought it already, for a dialog of its own. */
	if (c->group == g)
		tabs_show(c);
}

bool tabs_fit_together(struct client *c, struct client *target)
{
	if (target->tab_hidden && target->group)
		target = target->group->shown;
	/* A hidden tab may be taken along, and needs only to qualify as a
	 * window. */
	return c != target && (!c->group || c->group != target->group) &&
		(c->tab_hidden ? tab_material(c) : tabs_can_join(c)) &&
		tabs_can_join(target) && c->no_tile == target->no_tile;
}

bool tabs_join(struct client *c, struct client *target)
{
	if (!tabs_fit_together(c, target))
		return false;
	if (target->tab_hidden)
		target = target->group->shown;
	if (c->group)
		tabs_detach(c);
	grab_give_way(c);
	remember_own_size(c);

	struct tab_group *g = target->group;
	if (!g) {
		remember_own_size(target);
		g = xcalloc(1, sizeof(*g));
		g->key = target->win;
		g->members = xmalloc(sizeof(*g->members));
		g->members[g->n++] = target;
		g->shown = target;
		target->group = g;
	}
	g->members = xrealloc(g->members, sizeof(*g->members) * (g->n + 1));
	g->members[g->n++] = c;
	c->group = g;
	/* It takes the group's place, and what it was doing until now is over. */
	c->tiled = false;
	c->tile_main = false;
	c->shaded = false;
	c->auto_shaded = false;
	switch_to(g, c, g->shown, false);
	return true;
}

struct client *tabs_previous(struct client *c)
{
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *o = wm.clients[i];
		if (!o->tab_hidden && tabs_fit_together(c, o))
			return o;
	}
	return NULL;
}

struct client *tabs_nth(struct client *c, int n)
{
	return c->group && n >= 0 && n < c->group->n ? c->group->members[n] : NULL;
}

void tabs_detach(struct client *c)
{
	struct tab_group *g = c->group;
	if (!g || g->shown->minimized)
		return;
	if (c->fullscreen)
		client_set_fullscreen(c, false);
	if (g->shown == c)
		switch_to(g, neighbour(c), c, false);
	remove_member(c);
	c->tile_seq = --wm.tile_front;
	/* It is placed by the size it is about to have. */
	int w = c->tab_own_w, h = c->tab_own_h;
	client_apply_size_hints(c, &w, &h, false);
	c->w = w;
	c->h = h;
	client_place(c);
	c->saved = (struct rect){ c->x, c->y, c->w, c->h };
	client_update_decorations(c);
	client_configure(c);
	client_tab_show(c);
	client_dialogs_show(c, true);
	ewmh_set_allowed_actions(c);
	ewmh_set_state(c);
	frame_render(c);
	client_raise(c);
	client_focus(c);
	tile_arrange();
}

void tabs_client_gone(struct client *c)
{
	struct tab_group *g = c->group;
	if (!g)
		return;
	if (g->shown == c)
		switch_to(g, neighbour(c), c, true);
	remove_member(c);
}

void tabs_client_changed(struct client *c)
{
	if (c->group && !tab_material(c))
		tabs_detach(c);
}

void tabs_step(struct client *c, bool forward)
{
	struct tab_group *g = c->group;
	if (!g || g->shown->fullscreen || g->shown->minimized)
		return;
	int i = tabs_index(g->shown) + (forward ? 1 : -1);
	tabs_show(g->members[(i + g->n) % g->n]);
}

void tabs_show_nth(struct client *c, int n)
{
	struct tab_group *g = c->group;
	if (!g || g->shown->fullscreen || g->shown->minimized)
		return;
	struct client *m = tabs_nth(c, n);
	if (m)
		tabs_show(m);
}

/* A dialog that appeared in a hidden tab meanwhile has waited long enough. */
void tabs_after_restore(struct client *c)
{
	struct tab_group *g = c->group;
	if (!g)
		return;
	for (int i = 0; i < g->n; i++) {
		struct client *m = g->members[i];
		if (m == g->shown)
			continue;
		for (int k = 0; k < wm.n_clients; k++) {
			struct client *d = wm.clients[k];
			if (d->hidden_with_parent &&
					client_is_transient_of(d, m)) {
				tabs_show(m);
				client_activate(d);
				return;
			}
		}
	}
}

int tabs_index(struct client *c)
{
	if (!c->group)
		return -1;
	for (int i = 0; i < c->group->n; i++)
		if (c->group->members[i] == c)
			return i;
	return -1;
}

static int by_handover_index(const void *a, const void *b)
{
	const struct client *x = *(struct client *const *)a;
	const struct client *y = *(struct client *const *)b;
	return (x->handover_index > y->handover_index) -
		(x->handover_index < y->handover_index);
}

/* After a restart, once all windows are adopted: the groups they came from.
 * A group that can't be whole again shows what is left of it. */
void tabs_restore_groups(void)
{
	for (int i = 0; i < wm.n_clients; i++) {
		long key = wm.clients[i]->handover_group;
		if (!key)
			continue;
		struct client **v = xmalloc(sizeof(*v) * wm.n_clients);
		int n = 0;
		for (int k = i; k < wm.n_clients; k++) {
			struct client *c = wm.clients[k];
			if (c->handover_group == key) {
				c->handover_group = 0;
				v[n++] = c;
			}
		}
		qsort(v, n, sizeof(*v), by_handover_index);
		struct client *shown = NULL;
		for (int k = 0; k < n; k++)
			if (!v[k]->tab_hidden && !shown)
				shown = v[k];
		if (n < 2 || !shown) {
			for (int k = 0; k < n; k++)
				if (v[k]->tab_hidden)
					client_tab_show(v[k]);
			free(v);
			continue;
		}
		struct tab_group *g = xcalloc(1, sizeof(*g));
		g->key = (Window)key;
		g->members = v;
		g->n = n;
		g->shown = shown;
		for (int k = 0; k < n; k++) {
			v[k]->group = g;
			if (v[k] != shown && !v[k]->tab_hidden)
				client_tab_hide(v[k]);
		}
		frame_render(shown);
	}
}

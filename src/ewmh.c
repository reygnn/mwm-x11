/*
 * Extended Window Manager Hints (EWMH 1.5): root window properties,
 * per-client state, and the client messages pagers, panels and toolkits
 * send to ask for focus, state changes or interactive moves.
 *
 * mwm has no workspaces, so it exposes exactly one desktop.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include "mwm.h"

/* _NET_WM_MOVERESIZE directions */
enum {
	MR_SIZE_TOPLEFT,
	MR_SIZE_TOP,
	MR_SIZE_TOPRIGHT,
	MR_SIZE_RIGHT,
	MR_SIZE_BOTTOMRIGHT,
	MR_SIZE_BOTTOM,
	MR_SIZE_BOTTOMLEFT,
	MR_SIZE_LEFT,
	MR_MOVE,
	MR_SIZE_KEYBOARD,
	MR_MOVE_KEYBOARD,
	MR_CANCEL,
};

/* _NET_WM_STATE actions */
enum { STATE_REMOVE, STATE_ADD, STATE_TOGGLE };

/* ---- property helpers --------------------------------------------------- */

/* A format-32 property (XFree the result) and its item count in *n; the
 * items come back as longs, as usual with Xlib. Anything stored with
 * another format is rejected, since callers index the data as longs. */
void *get_property(Window w, Atom prop, Atom type, unsigned long *n)
{
	Atom real;
	int format;
	unsigned long after;
	unsigned char *data = NULL;
	*n = 0;
	if (XGetWindowProperty(wm.dpy, w, prop, 0, 1L << 20, False, type,
			&real, &format, n, &after, &data) != Success)
		return NULL;
	if (!data || (type != AnyPropertyType && real != type) ||
			format != 32 || !*n) {
		if (data)
			XFree(data);
		*n = 0;
		return NULL;
	}
	return data;
}

bool get_cardinal(Window w, Atom prop, long *out)
{
	unsigned long n;
	long *v = get_property(w, prop, XA_CARDINAL, &n);
	if (!v)
		return false;
	*out = v[0];
	XFree(v);
	return true;
}

Window get_window_prop(Window w, Atom prop)
{
	unsigned long n;
	Window *v = get_property(w, prop, XA_WINDOW, &n);
	if (!v)
		return None;
	Window r = v[0];
	XFree(v);
	return r;
}

/* Text property converted to UTF-8 whatever its encoding (STRING,
 * COMPOUND_TEXT or UTF8_STRING). Returns a malloc'd string or NULL. */
char *get_text_property(Window w, Atom prop)
{
	XTextProperty tp;
	if (!XGetTextProperty(wm.dpy, w, &tp, prop) || !tp.value)
		return NULL;
	char *out = NULL;
	if (tp.nitems) {
		char **list = NULL;
		int count = 0;
		if (tp.encoding == atoms[UTF8_STRING]) {
			out = strndup((char *)tp.value, tp.nitems);
		} else if (Xutf8TextPropertyToTextList(wm.dpy, &tp, &list,
				&count) >= Success && count > 0 && list) {
			out = xstrdup(list[0]);
		}
		if (list)
			XFreeStringList(list);
	}
	XFree(tp.value);
	return out;
}

static void set_cardinals(Window w, Atom prop, const long *v, int n)
{
	XChangeProperty(wm.dpy, w, prop, XA_CARDINAL, 32, PropModeReplace,
		(const unsigned char *)v, n);
}

static void set_windows(Window w, Atom prop, const Window *v, int n)
{
	XChangeProperty(wm.dpy, w, prop, XA_WINDOW, 32, PropModeReplace,
		(const unsigned char *)v, n);
}

static void set_atoms(Window w, Atom prop, const Atom *v, int n)
{
	XChangeProperty(wm.dpy, w, prop, XA_ATOM, 32, PropModeReplace,
		(const unsigned char *)v, n);
}

static void set_utf8(Window w, Atom prop, const char *s, int len)
{
	XChangeProperty(wm.dpy, w, prop, atoms[UTF8_STRING], 8,
		PropModeReplace, (const unsigned char *)s, len);
}

/* ---- root window -------------------------------------------------------- */

void ewmh_init(void)
{
	static const enum atom_id supported[] = {
		NET_SUPPORTED, NET_SUPPORTING_WM_CHECK, NET_CLIENT_LIST,
		NET_CLIENT_LIST_STACKING, NET_NUMBER_OF_DESKTOPS,
		NET_DESKTOP_GEOMETRY, NET_DESKTOP_VIEWPORT,
		NET_CURRENT_DESKTOP, NET_DESKTOP_NAMES, NET_ACTIVE_WINDOW,
		NET_WORKAREA, NET_CLOSE_WINDOW, NET_MOVERESIZE_WINDOW,
		NET_WM_MOVERESIZE, NET_RESTACK_WINDOW,
		NET_REQUEST_FRAME_EXTENTS, NET_WM_NAME, NET_WM_ICON_NAME,
		NET_WM_DESKTOP, NET_WM_WINDOW_TYPE,
		NET_WM_WINDOW_TYPE_DESKTOP, NET_WM_WINDOW_TYPE_DOCK,
		NET_WM_WINDOW_TYPE_TOOLBAR, NET_WM_WINDOW_TYPE_MENU,
		NET_WM_WINDOW_TYPE_UTILITY, NET_WM_WINDOW_TYPE_SPLASH,
		NET_WM_WINDOW_TYPE_DIALOG, NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
		NET_WM_WINDOW_TYPE_POPUP_MENU, NET_WM_WINDOW_TYPE_TOOLTIP,
		NET_WM_WINDOW_TYPE_NOTIFICATION, NET_WM_WINDOW_TYPE_COMBO,
		NET_WM_WINDOW_TYPE_DND, NET_WM_WINDOW_TYPE_NORMAL,
		NET_WM_STATE, NET_WM_STATE_MODAL, NET_WM_STATE_STICKY,
		NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ,
		NET_WM_STATE_SKIP_TASKBAR, NET_WM_STATE_SKIP_PAGER,
		NET_WM_STATE_HIDDEN, NET_WM_STATE_FULLSCREEN,
		NET_WM_STATE_ABOVE, NET_WM_STATE_BELOW,
		NET_WM_STATE_DEMANDS_ATTENTION, NET_WM_STATE_FOCUSED,
		NET_WM_ALLOWED_ACTIONS, NET_WM_ACTION_MOVE,
		NET_WM_ACTION_RESIZE, NET_WM_ACTION_MINIMIZE,
		NET_WM_ACTION_MAXIMIZE_HORZ, NET_WM_ACTION_MAXIMIZE_VERT,
		NET_WM_ACTION_FULLSCREEN, NET_WM_ACTION_CLOSE,
		NET_WM_ACTION_ABOVE, NET_WM_ACTION_BELOW, NET_WM_STRUT,
		NET_WM_STRUT_PARTIAL, NET_WM_ICON, NET_WM_USER_TIME,
		NET_WM_USER_TIME_WINDOW, NET_FRAME_EXTENTS, NET_WM_PING,
		NET_WM_FULL_PLACEMENT,
	};
	Atom list[LENGTH(supported)];
	for (size_t i = 0; i < LENGTH(supported); i++)
		list[i] = atoms[supported[i]];
	set_atoms(wm.root, atoms[NET_SUPPORTED], list, LENGTH(list));

	set_windows(wm.root, atoms[NET_SUPPORTING_WM_CHECK], &wm.check, 1);
	set_windows(wm.check, atoms[NET_SUPPORTING_WM_CHECK], &wm.check, 1);
	set_utf8(wm.check, atoms[NET_WM_NAME], "mwm", 3);

	long one = 1, zero = 0, viewport[2] = { 0, 0 };
	set_cardinals(wm.root, atoms[NET_NUMBER_OF_DESKTOPS], &one, 1);
	set_cardinals(wm.root, atoms[NET_CURRENT_DESKTOP], &zero, 1);
	set_cardinals(wm.root, atoms[NET_DESKTOP_VIEWPORT], viewport, 2);
	set_utf8(wm.root, atoms[NET_DESKTOP_NAMES], "Desktop", 8);
	ewmh_update_geometry();
	ewmh_set_active(NULL);
	ewmh_update_client_list();
	ewmh_update_stacking(NULL, 0);
	ewmh_update_workarea();

	/* Motif applications look for this to know mwm is running. */
	long info[2] = { 2 /* MWM_INFO_STARTUP_CUSTOM */, (long)wm.check };
	XChangeProperty(wm.dpy, wm.root, atoms[MOTIF_WM_INFO],
		atoms[MOTIF_WM_INFO], 32, PropModeReplace,
		(unsigned char *)info, 2);
}

void ewmh_update_geometry(void)
{
	long geom[2] = { wm.sw, wm.sh };
	set_cardinals(wm.root, atoms[NET_DESKTOP_GEOMETRY], geom, 2);
}

static int by_serial(const void *a, const void *b)
{
	const struct client *x = *(struct client *const *)a;
	const struct client *y = *(struct client *const *)b;
	return (x->serial > y->serial) - (x->serial < y->serial);
}

void ewmh_update_client_list(void)
{
	struct client **sorted = xmalloc(sizeof(*sorted) *
		(wm.n_clients + 1));
	Window *wins = xmalloc(sizeof(*wins) * (wm.n_clients + 1));
	if (wm.n_clients) /* no list at all before the first window */
		memcpy(sorted, wm.clients, sizeof(*sorted) * wm.n_clients);
	qsort(sorted, wm.n_clients, sizeof(*sorted), by_serial);
	for (int i = 0; i < wm.n_clients; i++)
		wins[i] = sorted[i]->win;
	set_windows(wm.root, atoms[NET_CLIENT_LIST], wins, wm.n_clients);
	free(wins);
	free(sorted);
}

/* top_first: client windows in the order restack() gave the server. */
void ewmh_update_stacking(const Window *top_first, int n)
{
	Window *wins = xmalloc(sizeof(*wins) * (n + 1));
	for (int i = 0; i < n; i++)
		wins[i] = top_first[n - 1 - i];
	set_windows(wm.root, atoms[NET_CLIENT_LIST_STACKING], wins, n);
	free(wins);
}

/* The work area is the root window minus every strut reserved along its
 * edges; per-monitor usable areas come from monitor_usable_at(). */
void ewmh_update_workarea(void)
{
	long l = 0, r = 0, t = 0, b = 0;
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (!c->has_strut || c->minimized)
			continue;
		l = MAX(l, c->strut[0]);
		r = MAX(r, c->strut[1]);
		t = MAX(t, c->strut[2]);
		b = MAX(b, c->strut[3]);
	}
	long area[4] = { l, t, MAX(1, wm.sw - l - r), MAX(1, wm.sh - t - b) };
	set_cardinals(wm.root, atoms[NET_WORKAREA], area, 4);
}

void ewmh_set_active(struct client *c)
{
	Window w = c ? c->win : None;
	set_windows(wm.root, atoms[NET_ACTIVE_WINDOW], &w, 1);
}

/* ---- client properties -------------------------------------------------- */

void ewmh_set_state(struct client *c)
{
	Atom s[16];
	int n = 0;
	if (c->modal)
		s[n++] = atoms[NET_WM_STATE_MODAL];
	if (c->sticky)
		s[n++] = atoms[NET_WM_STATE_STICKY];
	/* A tiled window keeps a maximized state for later but is not
	 * maximized now. */
	if (c->maxed_v && !c->tiled)
		s[n++] = atoms[NET_WM_STATE_MAXIMIZED_VERT];
	if (c->maxed_h && !c->tiled)
		s[n++] = atoms[NET_WM_STATE_MAXIMIZED_HORZ];
	if (c->skip_taskbar)
		s[n++] = atoms[NET_WM_STATE_SKIP_TASKBAR];
	if (c->skip_pager)
		s[n++] = atoms[NET_WM_STATE_SKIP_PAGER];
	if (c->minimized || c->tab_hidden)
		s[n++] = atoms[NET_WM_STATE_HIDDEN];
	if (c->fullscreen)
		s[n++] = atoms[NET_WM_STATE_FULLSCREEN];
	if (c->above)
		s[n++] = atoms[NET_WM_STATE_ABOVE];
	if (c->below)
		s[n++] = atoms[NET_WM_STATE_BELOW];
	if (c->urgent || c->attention)
		s[n++] = atoms[NET_WM_STATE_DEMANDS_ATTENTION];
	if (c == wm.focus)
		s[n++] = atoms[NET_WM_STATE_FOCUSED];
	set_atoms(c->win, atoms[NET_WM_STATE], s, n);
}

void ewmh_set_allowed_actions(struct client *c)
{
	Atom a[10];
	int n = 0;
	if (c->funcs & FUNC_MOVE)
		a[n++] = atoms[NET_WM_ACTION_MOVE];
	if (c->funcs & FUNC_RESIZE)
		a[n++] = atoms[NET_WM_ACTION_RESIZE];
	if (c->funcs & FUNC_MINIMIZE)
		a[n++] = atoms[NET_WM_ACTION_MINIMIZE];
	if (c->funcs & FUNC_MAXIMIZE) {
		a[n++] = atoms[NET_WM_ACTION_MAXIMIZE_HORZ];
		a[n++] = atoms[NET_WM_ACTION_MAXIMIZE_VERT];
	}
	if (c->type != WT_DOCK && c->type != WT_DESKTOP)
		a[n++] = atoms[NET_WM_ACTION_FULLSCREEN];
	if (c->funcs & FUNC_CLOSE)
		a[n++] = atoms[NET_WM_ACTION_CLOSE];
	a[n++] = atoms[NET_WM_ACTION_ABOVE];
	a[n++] = atoms[NET_WM_ACTION_BELOW];
	set_atoms(c->win, atoms[NET_WM_ALLOWED_ACTIONS], a, n);
}

void ewmh_set_frame_extents(struct client *c)
{
	long b = frame_border(c), t = frame_title(c);
	long ext[4] = { b, b, b + t, b };
	set_cardinals(c->win, atoms[NET_FRAME_EXTENTS], ext, 4);
}

/* _NET_WM_STATE a client set before mapping. */
void ewmh_read_initial_state(struct client *c)
{
	unsigned long n;
	Atom *s = get_property(c->win, atoms[NET_WM_STATE], XA_ATOM, &n);
	if (!s)
		return;
	for (unsigned long i = 0; i < n; i++) {
		if (s[i] == atoms[NET_WM_STATE_MODAL])
			c->modal = true;
		else if (s[i] == atoms[NET_WM_STATE_STICKY])
			c->sticky = true;
		else if (s[i] == atoms[NET_WM_STATE_MAXIMIZED_VERT])
			c->maxed_v = true;
		else if (s[i] == atoms[NET_WM_STATE_MAXIMIZED_HORZ])
			c->maxed_h = true;
		else if (s[i] == atoms[NET_WM_STATE_SKIP_TASKBAR])
			c->skip_taskbar = true;
		else if (s[i] == atoms[NET_WM_STATE_SKIP_PAGER])
			c->skip_pager = true;
		else if (s[i] == atoms[NET_WM_STATE_FULLSCREEN])
			c->fullscreen = true;
		else if (s[i] == atoms[NET_WM_STATE_ABOVE])
			c->above = true;
		else if (s[i] == atoms[NET_WM_STATE_BELOW])
			c->below = true;
		else if (s[i] == atoms[NET_WM_STATE_DEMANDS_ATTENTION])
			c->attention = true;
	}
	XFree(s);
}

/* ---- client messages ---------------------------------------------------- */

static bool apply(long action, bool current)
{
	switch (action) {
	case STATE_REMOVE:
		return false;
	case STATE_ADD:
		return true;
	default:
		return !current;
	}
}

static void change_state(struct client *c, long action, Atom p1, Atom p2)
{
	if (action < STATE_REMOVE || action > STATE_TOGGLE)
		return; /* malformed */
	if (c->tab_hidden)
		return; /* a hidden tab has no state until it is shown */
	Atom props[2] = { p1, p2 };
	bool maxed_v = c->maxed_v, maxed_h = c->maxed_h;
	bool restack_needed = false;

	for (int i = 0; i < 2; i++) {
		Atom p = props[i];
		if (p == None)
			continue;
		if (p == atoms[NET_WM_STATE_MAXIMIZED_VERT]) {
			maxed_v = apply(action, c->maxed_v);
		} else if (p == atoms[NET_WM_STATE_MAXIMIZED_HORZ]) {
			maxed_h = apply(action, c->maxed_h);
		} else if (p == atoms[NET_WM_STATE_FULLSCREEN]) {
			client_set_fullscreen(c, apply(action, c->fullscreen));
		} else if (p == atoms[NET_WM_STATE_ABOVE]) {
			c->above = apply(action, c->above);
			if (c->above)
				c->below = false;
			restack_needed = true;
		} else if (p == atoms[NET_WM_STATE_BELOW]) {
			c->below = apply(action, c->below);
			if (c->below)
				c->above = false;
			restack_needed = true;
		} else if (p == atoms[NET_WM_STATE_STICKY]) {
			c->sticky = apply(action, c->sticky);
		} else if (p == atoms[NET_WM_STATE_SKIP_TASKBAR]) {
			c->skip_taskbar = apply(action, c->skip_taskbar);
		} else if (p == atoms[NET_WM_STATE_SKIP_PAGER]) {
			c->skip_pager = apply(action, c->skip_pager);
		} else if (p == atoms[NET_WM_STATE_MODAL]) {
			c->modal = apply(action, c->modal);
		} else if (p == atoms[NET_WM_STATE_DEMANDS_ATTENTION]) {
			/* Removing it also drops the ICCCM urgency it shows;
			 * the client sets that again if it still means it. */
			bool on = apply(action, c->urgent || c->attention);
			c->attention = on && c != wm.focus;
			if (!on)
				c->urgent = false;
		}
		/* HIDDEN and FOCUSED belong to the window manager. */
	}
	if (maxed_v != c->maxed_v || maxed_h != c->maxed_h)
		client_set_maximized(c, maxed_v, maxed_h);
	if (restack_needed)
		restack();
	ewmh_set_state(c);
}

static void moveresize_window(struct client *c, XClientMessageEvent *e)
{
	long flags = e->data.l[0];
	int gravity = flags & 0xff;
	if (gravity == 0)
		gravity = c->gravity;
	if (c->fullscreen)
		return;

	/* Same semantics as a ConfigureRequest with the given gravity. */
	XConfigureRequestEvent req = {
		.type = ConfigureRequest,
		.window = c->win,
		.x = e->data.l[1],
		.y = e->data.l[2],
		.width = e->data.l[3],
		.height = e->data.l[4],
	};
	if (flags & (1 << 8))
		req.value_mask |= CWX;
	if (flags & (1 << 9))
		req.value_mask |= CWY;
	if (flags & (1 << 10))
		req.value_mask |= CWWidth;
	if (flags & (1 << 11))
		req.value_mask |= CWHeight;
	int saved = c->gravity;
	c->gravity = gravity;
	client_configure_request(&req);
	c->gravity = saved;
}

static void wm_moveresize(struct client *c, XClientMessageEvent *e)
{
	long direction = e->data.l[2];
	int px = e->data.l[0], py = e->data.l[1];
	static const unsigned edges[] = {
		[MR_SIZE_TOPLEFT] = EDGE_TOP | EDGE_LEFT,
		[MR_SIZE_TOP] = EDGE_TOP,
		[MR_SIZE_TOPRIGHT] = EDGE_TOP | EDGE_RIGHT,
		[MR_SIZE_RIGHT] = EDGE_RIGHT,
		[MR_SIZE_BOTTOMRIGHT] = EDGE_BOTTOM | EDGE_RIGHT,
		[MR_SIZE_BOTTOM] = EDGE_BOTTOM,
		[MR_SIZE_BOTTOMLEFT] = EDGE_BOTTOM | EDGE_LEFT,
		[MR_SIZE_LEFT] = EDGE_LEFT,
	};
	if (direction == MR_CANCEL) {
		if (wm.grabbed == c &&
				(wm.grab == GRAB_MOVE || wm.grab == GRAB_RESIZE)) {
			wm.grab_zone = SNAP_NONE;
			end_grab();
		}
		return;
	}
	if (direction == MR_MOVE_KEYBOARD) {
		run_action(ACT_MOVE, c);
		return;
	}
	if (direction == MR_SIZE_KEYBOARD) {
		run_action(ACT_SIZE, c);
		return;
	}
	if (direction < 0 || direction > MR_MOVE || c->fullscreen || c->tab_hidden)
		return;

	/* The toolkit released its pointer grab after the button press; only
	 * take over while the button is actually still held. */
	Window rw, cw;
	int rx, ry, wx, wy;
	unsigned mask;
	if (!XQueryPointer(wm.dpy, wm.root, &rw, &cw, &rx, &ry, &wx, &wy,
			&mask))
		return;
	if (!(mask & (Button1Mask | Button2Mask | Button3Mask)))
		return;

	client_activate(c);
	if (direction == MR_MOVE) {
		if (c->funcs & FUNC_MOVE)
			begin_grab(c, GRAB_MOVE, 0, false, px, py);
	} else if (c->funcs & FUNC_RESIZE) {
		begin_grab(c, GRAB_RESIZE, edges[direction], false, px,
			py);
	}
}

/* Extents a not yet managed window will get, so toolkits can size it. */
static void request_frame_extents(Window w)
{
	struct client tmp = { .win = w };
	client_update_size_hints(&tmp);
	client_update_transient(&tmp);
	client_update_type(&tmp);
	client_update_motif_hints(&tmp);
	client_update_decorations(&tmp);
	ewmh_set_frame_extents(&tmp);
}

void ewmh_client_message(XClientMessageEvent *e)
{
	Atom type = e->message_type;

	if (type == atoms[WM_PROTOCOLS] &&
			(Atom)e->data.l[0] == atoms[NET_WM_PING]) {
		/* Pong: the client answered, it is not hung. */
		struct client *c = client_find_win((Window)e->data.l[2]);
		if (c)
			c->ping_sent = 0;
		return;
	}
	if (type == atoms[NET_REQUEST_FRAME_EXTENTS]) {
		if (!client_find_win(e->window))
			request_frame_extents(e->window);
		return;
	}
	if (type == atoms[NET_CURRENT_DESKTOP] ||
			type == atoms[NET_NUMBER_OF_DESKTOPS])
		return; /* there is only desktop 0 */

	struct client *c = client_find_win(e->window);
	if (!c)
		return;

	if (type == atoms[NET_ACTIVE_WINDOW]) {
		client_activate(c);
	} else if (type == atoms[NET_CLOSE_WINDOW]) {
		client_close(c);
	} else if (type == atoms[NET_WM_STATE]) {
		change_state(c, e->data.l[0], e->data.l[1], e->data.l[2]);
	} else if (type == atoms[NET_MOVERESIZE_WINDOW]) {
		moveresize_window(c, e);
	} else if (type == atoms[NET_WM_MOVERESIZE]) {
		wm_moveresize(c, e);
	} else if (type == atoms[NET_RESTACK_WINDOW]) {
		XConfigureRequestEvent req = {
			.type = ConfigureRequest,
			.window = c->win,
			.above = (Window)e->data.l[1],
			.detail = e->data.l[2],
			.value_mask = CWStackMode |
				(e->data.l[1] ? CWSibling : 0),
		};
		client_configure_request(&req);
	} else if (type == atoms[NET_WM_DESKTOP]) {
		/* Only one desktop: keep the property truthful. */
		long d = (c->sticky || e->data.l[0] == -1 ||
			(unsigned long)e->data.l[0] == 0xffffffffUL) ?
			(long)0xffffffffUL : 0;
		if (d)
			c->sticky = true;
		set_cardinals(c->win, atoms[NET_WM_DESKTOP], &d, 1);
		ewmh_set_state(c);
	} else if (type == atoms[WM_CHANGE_STATE]) {
		/* ICCCM 4.1.4: iconify request */
		if (e->data.l[0] == IconicState)
			client_minimize(c);
	} else if (type == atoms[GTK_SHOW_WINDOW_MENU] && !c->tab_hidden) {
		menu_open_window(c, e->data.l[1], e->data.l[2]);
	}
}

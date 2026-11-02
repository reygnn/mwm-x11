/*
 * Managed clients: reparenting into frames, ICCCM properties and state,
 * geometry with window gravity, focus, stacking, minimize/maximize/
 * fullscreen, and monitor geometry.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include "mwm.h"

#define PING_TIMEOUT_MS 3000

/* _MOTIF_WM_HINTS flags */
#define MWM_HINTS_FUNCTIONS (1L << 0)
#define MWM_HINTS_DECORATIONS (1L << 1)

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- client list (stacking order, index 0 on top) ---------------------- */

static void list_remove(struct client *c)
{
	for (int i = 0; i < wm.n_clients; i++) {
		if (wm.clients[i] == c) {
			memmove(&wm.clients[i], &wm.clients[i + 1],
				sizeof(*wm.clients) * (wm.n_clients - i - 1));
			wm.n_clients--;
			return;
		}
	}
}

static void list_insert(struct client *c, int at)
{
	if (wm.n_clients == wm.cap_clients) {
		wm.cap_clients = wm.cap_clients ? wm.cap_clients * 2 : 32;
		wm.clients = realloc(wm.clients,
			sizeof(*wm.clients) * wm.cap_clients);
	}
	at = MAX(0, MIN(at, wm.n_clients));
	memmove(&wm.clients[at + 1], &wm.clients[at],
		sizeof(*wm.clients) * (wm.n_clients - at));
	wm.clients[at] = c;
	wm.n_clients++;
}

struct client *client_find(Window w)
{
	if (!w)
		return NULL;
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (c->win == w || c->frame == w || c->icon_win == w)
			return c;
	}
	return NULL;
}

struct client *client_find_win(Window w)
{
	if (!w)
		return NULL;
	for (int i = 0; i < wm.n_clients; i++) {
		if (wm.clients[i]->win == w)
			return wm.clients[i];
	}
	return NULL;
}

static bool is_transient_of(struct client *c, struct client *parent)
{
	return parent && c != parent && c->transient_for == parent->win;
}

static struct client *transient_parent(struct client *c)
{
	return c->transient_for ? client_find_win(c->transient_for) : NULL;
}

/* ---- monitors ----------------------------------------------------------- */

void monitors_update(void)
{
	Window r;
	int x, y;
	unsigned w, h, bw, depth;
	XGetGeometry(wm.dpy, wm.root, &r, &x, &y, &w, &h, &bw, &depth);
	wm.sw = w;
	wm.sh = h;

	wm.n_mon = 0;
	if (wm.have_xrandr) {
		int n = 0;
		XRRMonitorInfo *m = XRRGetMonitors(wm.dpy, wm.root, True, &n);
		for (int i = 0; m && i < n && wm.n_mon < MAX_MONITORS; i++) {
			if (m[i].width > 0 && m[i].height > 0)
				wm.mon[wm.n_mon++] = (struct rect){ m[i].x,
					m[i].y, m[i].width, m[i].height };
		}
		if (m)
			XRRFreeMonitors(m);
	}
	if (!wm.n_mon)
		wm.mon[wm.n_mon++] = (struct rect){ 0, 0, wm.sw, wm.sh };
}

static int monitor_index_at(int x, int y)
{
	int best = 0;
	long best_d = -1;
	for (int i = 0; i < wm.n_mon; i++) {
		struct rect *m = &wm.mon[i];
		if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
			return i;
		/* Off every monitor: pick the closest one. */
		long dx = x < m->x ? m->x - x : x >= m->x + m->w ?
			x - (m->x + m->w - 1) : 0;
		long dy = y < m->y ? m->y - y : y >= m->y + m->h ?
			y - (m->y + m->h - 1) : 0;
		long d = dx * dx + dy * dy;
		if (best_d < 0 || d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

struct rect monitor_rect_at(int x, int y)
{
	return wm.mon[monitor_index_at(x, y)];
}

static bool ranges_overlap(long a0, long a1, long b0, long b1)
{
	return a0 <= b1 && b0 <= a1;
}

/* The monitor minus every strut that touches it. */
struct rect monitor_usable_at(int x, int y)
{
	struct rect m = monitor_rect_at(x, y);
	int left = m.x, right = m.x + m.w, top = m.y, bottom = m.y + m.h;
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (!c->has_strut || c->minimized)
			continue;
		long *s = c->strut;
		if (s[0] > m.x && ranges_overlap(s[4], s[5], m.y, m.y + m.h - 1))
			left = MAX(left, (int)s[0]);
		if (s[1] && wm.sw - s[1] < m.x + m.w &&
				ranges_overlap(s[6], s[7], m.y, m.y + m.h - 1))
			right = MIN(right, (int)(wm.sw - s[1]));
		if (s[2] > m.y && ranges_overlap(s[8], s[9], m.x, m.x + m.w - 1))
			top = MAX(top, (int)s[2]);
		if (s[3] && wm.sh - s[3] < m.y + m.h &&
				ranges_overlap(s[10], s[11], m.x, m.x + m.w - 1))
			bottom = MIN(bottom, (int)(wm.sh - s[3]));
	}
	if (right - left < 100 || bottom - top < 100)
		return m; /* nonsensical struts */
	return (struct rect){ left, top, right - left, bottom - top };
}

struct rect client_box(struct client *c)
{
	return (struct rect){ c->x, c->y, c->w + frame_extra_w(c),
		c->h + frame_extra_h(c) };
}

static void client_center(struct client *c, int *x, int *y)
{
	struct rect b = client_box(c);
	*x = b.x + b.w / 2;
	*y = b.y + b.h / 2;
}

/* ---- properties --------------------------------------------------------- */

void client_update_title(struct client *c)
{
	char *t = get_text_property(c->win, atoms[NET_WM_NAME]);
	if (!t)
		t = get_text_property(c->win, XA_WM_NAME);
	free(c->title);
	c->title = t;
}

void client_update_icon_name(struct client *c)
{
	char *t = get_text_property(c->win, atoms[NET_WM_ICON_NAME]);
	if (!t)
		t = get_text_property(c->win, XA_WM_ICON_NAME);
	free(c->icon_name);
	c->icon_name = t;
}

void client_update_size_hints(struct client *c)
{
	XSizeHints sh;
	long supplied;
	if (!XGetWMNormalHints(wm.dpy, c->win, &sh, &supplied))
		sh.flags = 0;
	c->size_flags = sh.flags;

	/* ICCCM 4.1.2.3: base and min size stand in for each other. */
	c->base_w = c->base_h = c->min_w = c->min_h = 0;
	if (sh.flags & PBaseSize) {
		c->base_w = sh.base_width;
		c->base_h = sh.base_height;
	} else if (sh.flags & PMinSize) {
		c->base_w = sh.min_width;
		c->base_h = sh.min_height;
	}
	if (sh.flags & PMinSize) {
		c->min_w = sh.min_width;
		c->min_h = sh.min_height;
	} else if (sh.flags & PBaseSize) {
		c->min_w = sh.base_width;
		c->min_h = sh.base_height;
	}
	c->max_w = c->max_h = 0;
	if (sh.flags & PMaxSize) {
		c->max_w = MAX(sh.max_width, 0);
		c->max_h = MAX(sh.max_height, 0);
	}
	c->inc_w = c->inc_h = 0;
	if (sh.flags & PResizeInc) {
		c->inc_w = MAX(sh.width_inc, 0);
		c->inc_h = MAX(sh.height_inc, 0);
	}
	c->min_aspect = c->max_aspect = 0;
	if ((sh.flags & PAspect) && sh.min_aspect.x > 0 &&
			sh.max_aspect.y > 0) {
		c->min_aspect = (float)sh.min_aspect.y / sh.min_aspect.x;
		c->max_aspect = (float)sh.max_aspect.x / sh.max_aspect.y;
	}
	c->gravity = (sh.flags & PWinGravity) ? sh.win_gravity :
		NorthWestGravity;
	c->fixed = c->max_w && c->max_h && c->max_w == c->min_w &&
		c->max_h == c->min_h;
}

void client_update_wm_hints(struct client *c)
{
	XWMHints *h = XGetWMHints(wm.dpy, c->win);
	c->input = true; /* ICCCM: assume Passive when the hint is missing */
	if (!h)
		return;
	if (h->flags & InputHint)
		c->input = h->input;
	bool urgent = (h->flags & XUrgencyHint) != 0;
	if (urgent && c == wm.focus)
		urgent = false;
	c->urgent = urgent;
	XFree(h);
}

void client_update_protocols(struct client *c)
{
	Atom *p;
	int n;
	c->delete_window = c->take_focus = c->ping = false;
	if (!XGetWMProtocols(wm.dpy, c->win, &p, &n))
		return;
	for (int i = 0; i < n; i++) {
		if (p[i] == atoms[WM_DELETE_WINDOW])
			c->delete_window = true;
		else if (p[i] == atoms[WM_TAKE_FOCUS])
			c->take_focus = true;
		else if (p[i] == atoms[NET_WM_PING])
			c->ping = true;
	}
	XFree(p);
}

void client_update_transient(struct client *c)
{
	Window t = None;
	c->transient_for = None;
	if (XGetTransientForHint(wm.dpy, c->win, &t) && t != c->win)
		c->transient_for = t; /* may be the root: a group dialog */
}

void client_update_type(struct client *c)
{
	unsigned long n;
	Atom *t = get_property(c->win, atoms[NET_WM_WINDOW_TYPE], XA_ATOM, &n);
	c->type = c->transient_for ? WT_DIALOG : WT_NORMAL;
	for (unsigned long i = 0; t && i < n; i++) {
		Atom a = t[i];
		if (a == atoms[NET_WM_WINDOW_TYPE_NORMAL])
			c->type = WT_NORMAL;
		else if (a == atoms[NET_WM_WINDOW_TYPE_DIALOG])
			c->type = WT_DIALOG;
		else if (a == atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
				a == atoms[NET_WM_WINDOW_TYPE_TOOLBAR] ||
				a == atoms[NET_WM_WINDOW_TYPE_MENU])
			c->type = WT_UTILITY;
		else if (a == atoms[NET_WM_WINDOW_TYPE_SPLASH])
			c->type = WT_SPLASH;
		else if (a == atoms[NET_WM_WINDOW_TYPE_DOCK])
			c->type = WT_DOCK;
		else if (a == atoms[NET_WM_WINDOW_TYPE_DESKTOP])
			c->type = WT_DESKTOP;
		else if (a == atoms[NET_WM_WINDOW_TYPE_NOTIFICATION] ||
				a == atoms[NET_WM_WINDOW_TYPE_TOOLTIP] ||
				a == atoms[NET_WM_WINDOW_TYPE_POPUP_MENU] ||
				a == atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU] ||
				a == atoms[NET_WM_WINDOW_TYPE_COMBO] ||
				a == atoms[NET_WM_WINDOW_TYPE_DND])
			c->type = WT_NOTIFICATION;
		else
			continue;
		break; /* first known type wins */
	}
	if (t)
		XFree(t);
}

void client_update_motif_hints(struct client *c)
{
	unsigned long n;
	/* Toolkits disagree on the type (_MOTIF_WM_HINTS or CARDINAL). */
	long *h = get_property(c->win, atoms[MOTIF_WM_HINTS],
		AnyPropertyType, &n);
	c->mwm_flags = 0;
	if (!h)
		return;
	if (n >= 3) {
		c->mwm_flags = h[0];
		c->mwm_funcs = h[1];
		c->mwm_decor = h[2];
	}
	XFree(h);
}

/* With the ALL bit set, the remaining bits name what to take away. */
static unsigned motif_mask(long hint, unsigned all)
{
	return (hint & 1) ? all & ~(unsigned)hint : (unsigned)hint & all;
}

void client_update_decorations(struct client *c)
{
	unsigned d = DECOR_ALL, f = FUNC_ALL;
	switch (c->type) {
	case WT_DESKTOP:
	case WT_DOCK:
	case WT_SPLASH:
		d = 0;
		f = 0;
		break;
	case WT_NOTIFICATION:
		d = 0;
		f = FUNC_CLOSE;
		break;
	case WT_DIALOG:
	case WT_UTILITY:
		/* mwm's transientDecoration/transientFunctions defaults */
		d &= ~(DECOR_MINIMIZE | DECOR_MAXIMIZE);
		f &= ~(FUNC_MINIMIZE | FUNC_MAXIMIZE);
		break;
	case WT_NORMAL:
		break;
	}
	if (c->mwm_flags & MWM_HINTS_FUNCTIONS)
		f &= motif_mask(c->mwm_funcs, FUNC_ALL);
	if (c->mwm_flags & MWM_HINTS_DECORATIONS)
		d &= motif_mask(c->mwm_decor, DECOR_ALL);
	if (c->fixed)
		f &= ~(FUNC_RESIZE | FUNC_MAXIMIZE);

	/* Decorations follow the functions they trigger. */
	if (!(f & FUNC_RESIZE) && (d & DECOR_RESIZEH))
		d = (d & ~DECOR_RESIZEH) | DECOR_BORDER;
	if (!(f & FUNC_MINIMIZE))
		d &= ~DECOR_MINIMIZE;
	if (!(f & FUNC_MAXIMIZE))
		d &= ~DECOR_MAXIMIZE;
	c->decor = d;
	c->funcs = f;
}

void client_update_strut(struct client *c)
{
	unsigned long n;
	long *s = get_property(c->win, atoms[NET_WM_STRUT_PARTIAL],
		XA_CARDINAL, &n);
	c->has_strut = false;
	memset(c->strut, 0, sizeof(c->strut));
	if (s && n >= 12) {
		memcpy(c->strut, s, sizeof(c->strut));
	} else {
		if (s)
			XFree(s);
		s = get_property(c->win, atoms[NET_WM_STRUT], XA_CARDINAL, &n);
		if (!s || n < 4) {
			if (s)
				XFree(s);
			return;
		}
		/* The old form reserves along the whole edge. */
		memcpy(c->strut, s, 4 * sizeof(long));
		c->strut[4] = c->strut[6] = 0;
		c->strut[5] = c->strut[7] = wm.sh - 1;
		c->strut[8] = c->strut[10] = 0;
		c->strut[9] = c->strut[11] = wm.sw - 1;
	}
	XFree(s);
	for (int i = 0; i < 4; i++) {
		if (c->strut[i] < 0)
			c->strut[i] = 0;
		if (c->strut[i])
			c->has_strut = true;
	}
}

void client_set_wm_state(struct client *c, long state)
{
	long data[2] = { state, None };
	XChangeProperty(wm.dpy, c->win, atoms[WM_STATE], atoms[WM_STATE], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

static long get_wm_state(Window w)
{
	unsigned long n;
	long *s = get_property(w, atoms[WM_STATE], atoms[WM_STATE], &n);
	long state = WithdrawnState;
	if (s) {
		state = s[0];
		XFree(s);
	}
	return state;
}

/* ---- geometry ----------------------------------------------------------- */

/*
 * ICCCM 4.1.2.3 window gravity: a client asks for a position of its own
 * window (x, y, with border width old_bw); the frame goes where that
 * reference point stays put.
 */
static void gravity_to_frame(struct client *c, int x, int y, int *fx, int *fy)
{
	int B = frame_border(c), T = frame_title(c);
	int fw = c->w + 2 * B, fh = c->h + 2 * B + T;
	int ow = c->w + 2 * c->old_bw, oh = c->h + 2 * c->old_bw;

	if (c->gravity == StaticGravity) {
		*fx = x + c->old_bw - B;
		*fy = y + c->old_bw - B - T;
		return;
	}
	switch (c->gravity) {
	case NorthGravity:
	case CenterGravity:
	case SouthGravity:
		*fx = x + ow / 2 - fw / 2;
		break;
	case NorthEastGravity:
	case EastGravity:
	case SouthEastGravity:
		*fx = x + ow - fw;
		break;
	default:
		*fx = x;
		break;
	}
	switch (c->gravity) {
	case WestGravity:
	case CenterGravity:
	case EastGravity:
		*fy = y + oh / 2 - fh / 2;
		break;
	case SouthWestGravity:
	case SouthGravity:
	case SouthEastGravity:
		*fy = y + oh - fh;
		break;
	default:
		*fy = y;
		break;
	}
}

/* Inverse of gravity_to_frame: where the client believes it is. */
static void frame_to_gravity(struct client *c, int *x, int *y)
{
	int fx0, fy0;
	gravity_to_frame(c, 0, 0, &fx0, &fy0);
	*x = c->x - fx0;
	*y = c->y - fy0;
}

void client_apply_size_hints(struct client *c, int *pw, int *ph, bool incr)
{
	int w = *pw, h = *ph;
	bool base_is_min = c->base_w == c->min_w && c->base_h == c->min_h;

	/* Aspect ratios apply to the size minus the base size, unless the
	 * base size merely stands in for the minimum (ICCCM 4.1.2.3). */
	if (!base_is_min) {
		w -= c->base_w;
		h -= c->base_h;
	}
	if (incr && c->min_aspect > 0 && c->max_aspect > 0 && w > 0 && h > 0) {
		if (c->max_aspect < (float)w / h)
			w = h * c->max_aspect + 0.5f;
		else if (c->min_aspect < (float)h / w)
			h = w * c->min_aspect + 0.5f;
	}
	if (base_is_min) {
		w -= c->base_w;
		h -= c->base_h;
	}
	if (incr && c->inc_w > 1)
		w -= w % c->inc_w;
	if (incr && c->inc_h > 1)
		h -= h % c->inc_h;
	w = MAX(w + c->base_w, c->min_w);
	h = MAX(h + c->base_h, c->min_h);
	if (c->max_w)
		w = MIN(w, c->max_w);
	if (c->max_h)
		h = MIN(h, c->max_h);
	*pw = MAX(w, 1);
	*ph = MAX(h, 1);
}

/* ICCCM 4.1.5: tell the client where it is in root coordinates. */
void client_send_configure(struct client *c)
{
	XConfigureEvent ce = {
		.type = ConfigureNotify,
		.display = wm.dpy,
		.event = c->win,
		.window = c->win,
		.x = c->x + frame_border(c),
		.y = c->y + frame_border(c) + frame_title(c),
		.width = c->w,
		.height = c->h,
		.border_width = 0,
		.above = None,
		.override_redirect = False,
	};
	XSendEvent(wm.dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void client_configure(struct client *c)
{
	int B = frame_border(c), T = frame_title(c);
	XMoveResizeWindow(wm.dpy, c->frame, c->x, c->y, c->w + 2 * B,
		c->h + 2 * B + T);
	XMoveResizeWindow(wm.dpy, c->win, B, B + T, c->w, c->h);
	frame_render(c);
	client_send_configure(c);
}

void client_move(struct client *c, int x, int y)
{
	c->x = x;
	c->y = y;
	XMoveWindow(wm.dpy, c->frame, x, y);
	client_send_configure(c);
}

/* Geometry derived from the fullscreen/maximized flags and c->saved. */
static void apply_state_geometry(struct client *c)
{
	int cx, cy;
	client_center(c, &cx, &cy);
	if (c->fullscreen) {
		struct rect m = monitor_rect_at(cx, cy);
		c->x = m.x;
		c->y = m.y;
		c->w = m.w;
		c->h = m.h;
	} else {
		struct rect a = monitor_usable_at(cx, cy);
		if (c->maxed_h) {
			c->x = a.x;
			c->w = a.w - frame_extra_w(c);
		} else {
			c->x = c->saved.x;
			c->w = c->saved.w;
		}
		if (c->maxed_v) {
			c->y = a.y;
			c->h = a.h - frame_extra_h(c);
		} else {
			c->y = c->saved.y;
			c->h = c->saved.h;
		}
		int w = c->w, h = c->h;
		client_apply_size_hints(c, &w, &h, false);
		c->w = w;
		c->h = h;
	}
	client_configure(c);
}

/* Remember the current geometry of the axes that are not maximized. */
static void save_free_axes(struct client *c)
{
	if (c->fullscreen)
		return;
	if (!c->maxed_h) {
		c->saved.x = c->x;
		c->saved.w = c->w;
	}
	if (!c->maxed_v) {
		c->saved.y = c->y;
		c->saved.h = c->h;
	}
}

void client_set_maximized(struct client *c, bool v, bool h)
{
	if (v == c->maxed_v && h == c->maxed_h)
		return;
	save_free_axes(c);
	c->maxed_v = v;
	c->maxed_h = h;
	if (!c->fullscreen)
		apply_state_geometry(c);
	ewmh_set_state(c);
}

void client_set_fullscreen(struct client *c, bool fs)
{
	if (c->fullscreen == fs)
		return;
	if (fs)
		save_free_axes(c);
	c->fullscreen = fs;
	apply_state_geometry(c);
	ewmh_set_frame_extents(c);
	ewmh_set_state(c);
	restack();
}

/* ---- stacking ----------------------------------------------------------- */

static enum layer client_layer(struct client *c)
{
	if (c->type == WT_DESKTOP)
		return LAYER_DESKTOP;
	if (c->fullscreen && c == wm.focus)
		return LAYER_FULLSCREEN;
	if (c->below)
		return LAYER_BELOW;
	if (c->above || c->type == WT_DOCK || c->type == WT_NOTIFICATION)
		return LAYER_ABOVE;
	/* Transients follow their parent into its layer. */
	struct client *p = transient_parent(c);
	if (p && p != c && !p->transient_for)
		return client_layer(p) == LAYER_FULLSCREEN ? LAYER_FULLSCREEN :
			MAX(client_layer(p), LAYER_NORMAL);
	return LAYER_NORMAL;
}

/* Push the stacking order of wm.clients, by layer, to the server. */
void restack(void)
{
	Window *wins = malloc(sizeof(*wins) * (2 * wm.n_clients + 2));
	int n = 0;
	if (wm.menu.open)
		wins[n++] = wm.menu.win;
	for (int layer = LAYER_COUNT - 1; layer >= 0; layer--) {
		for (int i = 0; i < wm.n_clients; i++) {
			struct client *c = wm.clients[i];
			if (layer == LAYER_ICONS) {
				if (c->minimized && c->icon_win &&
						!c->hidden_with_parent)
					wins[n++] = c->icon_win;
			} else if ((int)client_layer(c) == layer) {
				wins[n++] = c->frame;
			}
		}
	}
	XRestackWindows(wm.dpy, wins, n);
	free(wins);
	ewmh_update_stacking();
}

void client_raise(struct client *c)
{
	list_remove(c);
	list_insert(c, 0);
	/* Transients stay above the window they belong to. */
	struct client **kids = malloc(sizeof(*kids) * wm.n_clients);
	int n = 0;
	for (int i = wm.n_clients - 1; i >= 0; i--) {
		if (is_transient_of(wm.clients[i], c))
			kids[n++] = wm.clients[i];
	}
	for (int i = 0; i < n; i++) {
		list_remove(kids[i]);
		list_insert(kids[i], 0);
	}
	free(kids);
	restack();
}

void client_lower(struct client *c)
{
	list_remove(c);
	list_insert(c, wm.n_clients);
	restack();
	if (c == wm.focus)
		client_focus_next(c);
}

/* ---- focus -------------------------------------------------------------- */

static bool focusable(struct client *c)
{
	return !c->minimized && c->type != WT_DOCK &&
		c->type != WT_DESKTOP && c->type != WT_NOTIFICATION;
}

static void send_protocol(struct client *c, Atom proto, Time t)
{
	XClientMessageEvent ev = {
		.type = ClientMessage,
		.window = c->win,
		.message_type = atoms[WM_PROTOCOLS],
		.format = 32,
		.data.l = { (long)proto, (long)t, 0, 0, 0 },
	};
	XSendEvent(wm.dpy, c->win, False, NoEventMask, (XEvent *)&ev);
}

/* Bookkeeping for the active window, without moving the X input focus:
 * also used when a globally active client took the focus itself. */
void client_set_active(struct client *c)
{
	struct client *old = wm.focus;
	if (old == c)
		return;
	wm.focus = c;
	if (old) {
		client_grab_buttons(old, false);
		frame_render(old);
		ewmh_set_state(old);
	}
	if (c) {
		client_grab_buttons(c, true);
		c->urgent = false;
		frame_render(c);
		ewmh_set_state(c);
	}
	ewmh_set_active(c);
	/* Fullscreen windows only sit on top while focused. */
	if ((old && old->fullscreen) || (c && c->fullscreen))
		restack();
}

/*
 * ICCCM 4.1.7 input models: Passive and Locally Active clients get
 * SetInputFocus, Locally and Globally Active ones WM_TAKE_FOCUS, and No
 * Input clients neither (the focus parks on our check window).
 */
void client_focus(struct client *c)
{
	if (c && !focusable(c))
		return;
	client_set_active(c);
	if (!c) {
		XSetInputFocus(wm.dpy, wm.check, RevertToPointerRoot, wm.time);
		return;
	}
	if (c->input)
		XSetInputFocus(wm.dpy, c->win, RevertToPointerRoot, wm.time);
	else if (!c->take_focus)
		XSetInputFocus(wm.dpy, wm.check, RevertToPointerRoot, wm.time);
	if (c->take_focus)
		send_protocol(c, atoms[WM_TAKE_FOCUS], wm.time);
	if (c->cmap && c->cmap != wm.cmap)
		XInstallColormap(wm.dpy, c->cmap);
}

/* What a click on a window does: show it, raise it, focus it. */
void client_activate(struct client *c)
{
	if (c->minimized)
		client_restore(c);
	client_raise(c);
	client_focus(c);
}

void client_focus_next(struct client *except)
{
	/* Prefer the window a dialog belongs to. */
	struct client *p = except ? transient_parent(except) : NULL;
	if (p && p != except && focusable(p)) {
		client_focus(p);
		return;
	}
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (c != except && focusable(c)) {
			client_focus(c);
			return;
		}
	}
	client_focus(NULL);
}

void client_cycle(bool backwards)
{
	struct client *top = NULL, *bottom = NULL;
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (!focusable(c) || c->skip_taskbar)
			continue;
		if (!top)
			top = c;
		bottom = c;
	}
	if (!top)
		return;
	if (backwards) {
		/* Push the current window to the back, focus the next one. */
		client_lower(top);
		return;
	}
	client_activate(bottom);
}

/* Unfocused windows get a synchronous grab so the click that focuses
 * them can be replayed to the client; Alt+buttons are always ours. */
void client_grab_buttons(struct client *c, bool focused)
{
	static const unsigned buttons[] = { Button1, Button2, Button3 };
	unsigned locks[] = { 0, LockMask, wm.numlock, wm.numlock | LockMask };

	XUngrabButton(wm.dpy, AnyButton, AnyModifier, c->win);
	if (!focused)
		XGrabButton(wm.dpy, AnyButton, AnyModifier, c->win, False,
			ButtonPressMask | ButtonReleaseMask, GrabModeSync,
			GrabModeSync, None, None);
	for (size_t b = 0; b < LENGTH(buttons); b++) {
		for (size_t l = 0; l < LENGTH(locks); l++)
			XGrabButton(wm.dpy, buttons[b], Mod1Mask | locks[l],
				c->win, False, ButtonPressMask |
				ButtonReleaseMask | PointerMotionMask,
				GrabModeAsync, GrabModeAsync, None, None);
	}
}

/* ---- close, minimize, restore ------------------------------------------- */

void client_close(struct client *c)
{
	if (!c->delete_window) {
		XKillClient(wm.dpy, c->win);
		return;
	}
	/* A client that does not answer _NET_WM_PING within the timeout is
	 * considered hung, and the next close kills it. */
	if (c->ping && c->ping_sent &&
			now_ms() - c->ping_sent > PING_TIMEOUT_MS) {
		wm_log("killing unresponsive client 0x%lx", c->win);
		XKillClient(wm.dpy, c->win);
		return;
	}
	send_protocol(c, atoms[WM_DELETE_WINDOW], wm.time);
	if (c->ping && !c->ping_sent) {
		XClientMessageEvent ev = {
			.type = ClientMessage,
			.window = c->win,
			.message_type = atoms[WM_PROTOCOLS],
			.format = 32,
			.data.l = { (long)atoms[NET_WM_PING], (long)wm.time,
				(long)c->win, 0, 0 },
		};
		XSendEvent(wm.dpy, c->win, False, NoEventMask, (XEvent *)&ev);
		c->ping_sent = now_ms();
	}
}

static void hide(struct client *c)
{
	if (c->minimized)
		return;
	c->minimized = true;
	c->ignore_unmap++;
	XUnmapWindow(wm.dpy, c->win);
	XUnmapWindow(wm.dpy, c->frame);
	client_set_wm_state(c, IconicState);
	ewmh_set_state(c);
}

static void show(struct client *c)
{
	c->minimized = false;
	c->hidden_with_parent = false;
	XMapWindow(wm.dpy, c->win);
	XMapWindow(wm.dpy, c->frame);
	client_set_wm_state(c, NormalState);
	ewmh_set_state(c);
}

void client_minimize(struct client *c)
{
	if (c->minimized)
		return;
	bool had_focus = wm.focus == c;
	hide(c);
	/* Dialogs go away with the window they belong to, without icons. */
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *t = wm.clients[i];
		if (is_transient_of(t, c) && !t->minimized) {
			had_focus |= wm.focus == t;
			hide(t);
			t->hidden_with_parent = true;
		}
	}
	if (!c->icon_win) {
		XSetWindowAttributes a = {
			.override_redirect = True,
			.background_pixel = theme.inactive.bg,
			.event_mask = ButtonPressMask | ButtonReleaseMask,
		};
		c->icon_win = XCreateWindow(wm.dpy, wm.root, 0, 0, ICON_W,
			ICON_H, 0, wm.depth, InputOutput, wm.visual,
			CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	}
	icon_render(c);
	icons_arrange();
	XMapWindow(wm.dpy, c->icon_win);
	if (c->has_strut)
		ewmh_update_workarea();
	restack();
	if (had_focus)
		client_focus_next(c);
}

static void deiconify(struct client *c)
{
	if (c->icon_win)
		XUnmapWindow(wm.dpy, c->icon_win);
	show(c);
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *t = wm.clients[i];
		if (is_transient_of(t, c) && t->hidden_with_parent)
			show(t);
	}
	if (c->has_strut)
		ewmh_update_workarea();
}

/* mwm's f.normalize: back from an icon, fullscreen or maximized state. */
void client_restore(struct client *c)
{
	if (c->minimized) {
		struct client *p = transient_parent(c);
		if (c->hidden_with_parent && p) {
			client_restore(p);
			return;
		}
		deiconify(c);
		client_raise(c);
		client_focus(c);
	} else if (c->fullscreen) {
		client_set_fullscreen(c, false);
	} else if (c->maxed_v || c->maxed_h) {
		client_set_maximized(c, false, false);
	}
}

/* ---- manage / unmanage -------------------------------------------------- */

/* Cascade new windows from the top-left of the monitor under the pointer,
 * centring them once the cascade would run off the screen. */
static void place(struct client *c)
{
	Window rw, cw;
	int px = 0, py = 0, wx, wy;
	unsigned mask;
	XQueryPointer(wm.dpy, wm.root, &rw, &cw, &px, &py, &wx, &wy, &mask);
	struct rect a = monitor_usable_at(px, py);
	struct rect b = client_box(c);
	int step = theme.title_height + theme.border;
	int x = a.x + 32 + wm.cascade * step;
	int y = a.y + 32 + wm.cascade * step;
	if (x + b.w > a.x + a.w || y + b.h > a.y + a.h) {
		wm.cascade = 0;
		x = a.x + (a.w - b.w) / 2;
		y = a.y + (a.h - b.h) / 2;
	} else {
		wm.cascade = (wm.cascade + 1) % 10;
	}
	c->x = MAX(x, a.x);
	c->y = MAX(y, a.y);
}

static void center_in(struct client *c, struct rect r)
{
	struct rect b = client_box(c);
	c->x = r.x + (r.w - b.w) / 2;
	c->y = r.y + (r.h - b.h) / 2;
}

/* Keep at least the title bar reachable on some monitor. */
static void keep_visible(struct client *c)
{
	int cx, cy;
	client_center(c, &cx, &cy);
	struct rect a = monitor_usable_at(cx, cy);
	struct rect b = client_box(c);
	if (c->x + b.w > a.x + a.w)
		c->x = a.x + a.w - b.w;
	if (c->y + b.h > a.y + a.h)
		c->y = a.y + a.h - b.h;
	c->x = MAX(c->x, a.x);
	c->y = MAX(c->y, a.y);
}

static bool wants_focus_on_map(struct client *c)
{
	if (!focusable(c) || c->type == WT_SPLASH)
		return false;
	if (!c->input && !c->take_focus)
		return false;
	/* _NET_WM_USER_TIME 0: the window was not opened by the user. */
	Window tw = get_window_prop(c->win, atoms[NET_WM_USER_TIME_WINDOW]);
	long t;
	if (get_cardinal(tw ? tw : c->win, atoms[NET_WM_USER_TIME], &t) &&
			t == 0)
		return false;
	return true;
}

void client_manage(Window w, bool startup)
{
	XWindowAttributes wa;
	if (!XGetWindowAttributes(wm.dpy, w, &wa) || wa.override_redirect ||
			wa.class == InputOnly || client_find(w))
		return;

	struct client *c = calloc(1, sizeof(*c));
	c->win = w;
	c->serial = wm.next_serial++;
	c->w = MAX(wa.width, 1);
	c->h = MAX(wa.height, 1);
	c->old_bw = wa.border_width;
	c->cmap = wa.colormap;

	client_update_title(c);
	client_update_icon_name(c);
	client_update_size_hints(c);
	client_update_wm_hints(c);
	client_update_protocols(c);
	client_update_transient(c);
	client_update_type(c);
	client_update_motif_hints(c);
	client_update_decorations(c);
	client_update_strut(c);
	ewmh_read_initial_state(c);
	if (c->type == WT_DOCK || c->type == WT_DESKTOP)
		c->sticky = true;
	wm_debug("manage 0x%lx \"%s\" type %d", w, c->title ? c->title : "",
		c->type);

	/* ---- position ---- */
	struct client *parent = transient_parent(c);
	XSizeHints sh;
	long supplied;
	bool positioned = startup || c->type == WT_DOCK ||
		c->type == WT_DESKTOP;
	if (XGetWMNormalHints(wm.dpy, w, &sh, &supplied) &&
			((sh.flags & USPosition) ||
			((sh.flags & PPosition) && (wa.x || wa.y))))
		positioned = true;

	if (positioned) {
		gravity_to_frame(c, wa.x, wa.y, &c->x, &c->y);
	} else if (parent && !parent->minimized) {
		center_in(c, client_box(parent));
	} else if (c->type == WT_DIALOG || c->type == WT_SPLASH) {
		Window rw, cw;
		int px = 0, py = 0, wx, wy;
		unsigned mask;
		XQueryPointer(wm.dpy, wm.root, &rw, &cw, &px, &py, &wx, &wy,
			&mask);
		center_in(c, monitor_usable_at(px, py));
	} else {
		place(c);
	}
	if (!positioned || !startup)
		if (c->type != WT_DOCK && c->type != WT_DESKTOP)
			keep_visible(c);
	c->saved = (struct rect){ c->x, c->y, c->w, c->h };

	/* ---- frame ---- */
	int B = frame_border(c), T = frame_title(c);
	XSetWindowAttributes a = {
		.override_redirect = True,
		.background_pixel = theme.inactive.bg,
		.border_pixel = 0,
		.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
			ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
			EnterWindowMask | LeaveWindowMask,
	};
	c->frame = XCreateWindow(wm.dpy, wm.root, c->x, c->y, c->w + 2 * B,
		c->h + 2 * B + T, 0, wm.depth, InputOutput, wm.visual,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
		&a);
	c->cursor = wm.cursor[CUR_DEFAULT];
	XDefineCursor(wm.dpy, c->frame, c->cursor);

	XAddToSaveSet(wm.dpy, w);
	XSelectInput(wm.dpy, w, PropertyChangeMask | FocusChangeMask);
	XSetWindowBorderWidth(wm.dpy, w, 0);
	if (wa.map_state != IsUnmapped)
		c->ignore_unmap++; /* reparenting unmaps it */
	XReparentWindow(wm.dpy, w, c->frame, B, B + T);
	XResizeWindow(wm.dpy, w, c->w, c->h);
	client_grab_buttons(c, false);

	list_insert(c, 0);
	long desktop = c->sticky ? (long)0xffffffffUL : 0;
	XChangeProperty(wm.dpy, w, atoms[NET_WM_DESKTOP], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&desktop, 1);
	ewmh_set_allowed_actions(c);
	ewmh_set_frame_extents(c);
	ewmh_update_client_list();
	if (c->has_strut)
		ewmh_update_workarea();

	/* Requested states: geometry first, then decide on mapping. */
	if (c->fullscreen) {
		c->fullscreen = false;
		client_set_fullscreen(c, true);
	} else if (c->maxed_v || c->maxed_h) {
		bool v = c->maxed_v, h = c->maxed_h;
		c->maxed_v = c->maxed_h = false;
		client_set_maximized(c, v, h);
	} else {
		frame_render(c);
		client_send_configure(c);
	}

	XWMHints *hints = XGetWMHints(wm.dpy, w);
	bool iconic = hints && (hints->flags & StateHint) &&
		hints->initial_state == IconicState;
	if (hints)
		XFree(hints);
	if (startup)
		iconic = get_wm_state(w) == IconicState;

	if (iconic) {
		client_minimize(c);
		/* Unmapping a window that is not mapped sends no event. */
		if (wa.map_state == IsUnmapped)
			c->ignore_unmap--;
	} else {
		XMapWindow(wm.dpy, w);
		XMapWindow(wm.dpy, c->frame);
		client_set_wm_state(c, NormalState);
		ewmh_set_state(c);
		if (parent)
			client_raise(parent); /* brings c along on top */
		else
			client_raise(c);
		if (!startup && wants_focus_on_map(c))
			client_focus(c);
	}
}

static void release(struct client *c, bool destroyed, bool keep_iconic)
{
	if (wm.grabbed == c)
		end_grab();
	if (wm.menu.target == c)
		menu_close();
	if (wm.last_click_client == c)
		wm.last_click_client = NULL;

	if (!destroyed) {
		int x, y;
		frame_to_gravity(c, &x, &y);
		XUngrabButton(wm.dpy, AnyButton, AnyModifier, c->win);
		XSelectInput(wm.dpy, c->win, NoEventMask);
		XReparentWindow(wm.dpy, c->win, wm.root, x, y);
		XSetWindowBorderWidth(wm.dpy, c->win, c->old_bw);
		XRemoveFromSaveSet(wm.dpy, c->win);
		if (keep_iconic && c->minimized) {
			client_set_wm_state(c, IconicState);
		} else if (keep_iconic) {
			client_set_wm_state(c, NormalState);
		} else {
			/* Withdrawn (ICCCM 4.1.3.1, EWMH: drop desktop and
			 * state so a remap starts over). */
			client_set_wm_state(c, WithdrawnState);
			XDeleteProperty(wm.dpy, c->win, atoms[NET_WM_STATE]);
			XDeleteProperty(wm.dpy, c->win, atoms[NET_WM_DESKTOP]);
		}
	}
	XDestroyWindow(wm.dpy, c->frame);
	if (c->icon_win)
		XDestroyWindow(wm.dpy, c->icon_win);
	list_remove(c);
	for (int i = 0; i < wm.n_clients; i++) {
		if (wm.clients[i]->transient_for == c->win &&
				wm.clients[i]->hidden_with_parent)
			wm.clients[i]->hidden_with_parent = false;
	}
	free(c->title);
	free(c->icon_name);
}

void client_unmanage(struct client *c, bool destroyed)
{
	wm_debug("unmanage 0x%lx%s", c->win, destroyed ? " (destroyed)" : "");
	bool had_focus = wm.focus == c;
	bool had_strut = c->has_strut;
	if (had_focus)
		wm.focus = NULL;
	release(c, destroyed, false);
	free(c);
	if (had_strut)
		ewmh_update_workarea();
	ewmh_update_client_list();
	ewmh_update_stacking();
	icons_arrange();
	if (had_focus)
		client_focus_next(NULL);
}

/* On exit: hand every window back to the root, mapped, so the session
 * survives. On restart iconic windows stay iconic for the next mwm. */
void client_release_all(void)
{
	while (wm.n_clients) {
		struct client *c = wm.clients[wm.n_clients - 1];
		if (c->minimized && !wm.restart) {
			XMapWindow(wm.dpy, c->win);
			c->minimized = false;
		}
		if (!wm.restart) {
			XDeleteProperty(wm.dpy, c->win, atoms[NET_WM_STATE]);
			XDeleteProperty(wm.dpy, c->win, atoms[NET_WM_DESKTOP]);
		}
		release(c, false, true);
		free(c);
	}
	XSetInputFocus(wm.dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
}

/* ---- requests and notifications ----------------------------------------- */

void client_redecorate(struct client *c)
{
	client_update_decorations(c);
	client_configure(c);
	ewmh_set_frame_extents(c);
	ewmh_set_allowed_actions(c);
}

void client_configure_request(XConfigureRequestEvent *e)
{
	struct client *c = client_find_win(e->window);
	if (!c) {
		/* Not ours (yet): grant it as asked. */
		XWindowChanges wc = {
			.x = e->x, .y = e->y, .width = e->width,
			.height = e->height, .border_width = e->border_width,
			.sibling = e->above, .stack_mode = e->detail,
		};
		XConfigureWindow(wm.dpy, e->window, e->value_mask, &wc);
		return;
	}

	if (e->value_mask & CWBorderWidth)
		c->old_bw = e->border_width; /* restored on unmanage */

	if (!c->fullscreen && (e->value_mask & (CWX | CWY | CWWidth |
			CWHeight))) {
		int w = (e->value_mask & CWWidth) ? e->width : c->w;
		int h = (e->value_mask & CWHeight) ? e->height : c->h;
		if (c->maxed_h)
			w = c->w;
		if (c->maxed_v)
			h = c->h;
		client_apply_size_hints(c, &w, &h, false);

		/* Where the client thinks it is now, then what it asks for,
		 * translated through its gravity at the new size. */
		int vx, vy;
		frame_to_gravity(c, &vx, &vy);
		if ((e->value_mask & CWX) && !c->maxed_h)
			vx = e->x;
		if ((e->value_mask & CWY) && !c->maxed_v)
			vy = e->y;
		c->w = w;
		c->h = h;
		gravity_to_frame(c, vx, vy, &c->x, &c->y);
		if (c->maxed_h || c->maxed_v)
			save_free_axes(c);
		client_configure(c);
	} else {
		/* Denied or nothing to do: still confirm the geometry. */
		client_send_configure(c);
	}

	if (e->value_mask & CWStackMode) {
		if (e->detail == Above || e->detail == TopIf)
			client_raise(c);
		else if (e->detail == Below || e->detail == BottomIf)
			client_lower(c);
	}
}

void client_property_notify(XPropertyEvent *e)
{
	struct client *c = client_find_win(e->window);
	if (!c)
		return;
	Atom a = e->atom;
	if (a == XA_WM_NAME || a == atoms[NET_WM_NAME]) {
		client_update_title(c);
		frame_render(c);
		if (c->minimized && !c->icon_name && c->icon_win)
			icon_render(c);
	} else if (a == XA_WM_ICON_NAME || a == atoms[NET_WM_ICON_NAME]) {
		client_update_icon_name(c);
		if (c->minimized && c->icon_win)
			icon_render(c);
	} else if (a == atoms[NET_WM_ICON]) {
		if (c->minimized && c->icon_win)
			icon_render(c);
	} else if (a == XA_WM_NORMAL_HINTS) {
		bool fixed = c->fixed;
		client_update_size_hints(c);
		if (fixed != c->fixed)
			client_redecorate(c);
	} else if (a == XA_WM_HINTS) {
		client_update_wm_hints(c);
		ewmh_set_state(c);
	} else if (a == atoms[WM_PROTOCOLS]) {
		client_update_protocols(c);
	} else if (a == XA_WM_TRANSIENT_FOR) {
		client_update_transient(c);
		restack();
	} else if (a == atoms[MOTIF_WM_HINTS] ||
			a == atoms[NET_WM_WINDOW_TYPE]) {
		client_update_transient(c);
		client_update_type(c);
		client_update_motif_hints(c);
		client_redecorate(c);
		restack();
	} else if (a == atoms[NET_WM_STRUT] ||
			a == atoms[NET_WM_STRUT_PARTIAL]) {
		client_update_strut(c);
		ewmh_update_workarea();
	}
}

/*
 * Motif-style popup menus: the window menu (from the frame's menu button,
 * Alt+Space, Shift+Escape or a client request) and the root menu on the
 * desktop. A menu is an override-redirect window holding the pointer and
 * keyboard grab while it is posted.
 *
 * Armed items get a raised shadow rather than a colour change, mnemonics
 * are underlined and accelerators sit right-aligned, as in mwm.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>
#include "mwm.h"

#define PAD_X 10
#define ITEM_H 24
#define SEP_H 8
#define TITLE_H 24
#define ACCEL_GAP 28
#define DRAG_RELEASE_MS 400

static int str_width(const char *s)
{
	return s ? text_width(s, strlen(s)) : 0;
}

static void layout_menu(struct menu *m)
{
	int label_w = str_width(m->title), accel_w = 0;
	int item_h = MAX(ITEM_H, theme.font->ascent + theme.font->descent + 8);
	int y = theme.bevel + 2;
	if (m->title)
		y += item_h + 8;
	for (int i = 0; i < m->n_items; i++) {
		struct menu_item *it = &m->items[i];
		m->item_y[i] = y;
		if (!it->label) {
			m->item_h[i] = SEP_H;
		} else {
			m->item_h[i] = item_h;
			label_w = MAX(label_w, str_width(it->label));
			accel_w = MAX(accel_w, str_width(it->accel));
		}
		y += m->item_h[i];
	}
	m->height = y + theme.bevel + 2;
	m->width = 2 * theme.bevel + 2 * PAD_X + label_w +
		(accel_w ? ACCEL_GAP + accel_w : 0);
	m->width = MAX(m->width, 120);
}

/* Left-aligned label with its mnemonic letter underlined. */
static void draw_label(struct canvas *cv, const char *text, char mnemonic,
	XftColor *fg, int x, int y, int h)
{
	int len = strlen(text);
	int fh = theme.font->ascent + theme.font->descent;
	int base = y + (h - fh) / 2 + theme.font->ascent;
	XftDrawStringUtf8(cv->xft, fg, theme.font, x, base,
		(const FcChar8 *)text, len);
	const char *u = mnemonic ? strchr(text, mnemonic) : NULL;
	if (u) {
		int ux = x + text_width(text, u - text);
		int uw = text_width(u, 1);
		XftDrawRect(cv->xft, fg, ux, base + 2, uw, 1);
	}
}

static void render_menu(void)
{
	struct menu *m = &wm.menu;
	const struct shade *s = &theme.inactive;
	struct canvas cv;
	canvas_begin(&cv, m->width, m->height);
	fill_rect(&cv, s->bg, 0, 0, m->width, m->height);
	draw_bevel(&cv, 0, 0, m->width, m->height, theme.bevel, s, false);

	int inner = m->width - 2 * theme.bevel;
	if (m->title) {
		int ty = theme.bevel + 2;
		int th = MAX(TITLE_H, theme.font->ascent +
			theme.font->descent + 8);
		draw_text(&cv, m->title, (XftColor *)&s->fg, theme.bevel, ty,
			inner, th, true);
		/* mwm puts a double separator under the menu title */
		draw_groove_h(&cv, theme.bevel, ty + th, inner, s);
		draw_groove_h(&cv, theme.bevel, ty + th + 3, inner, s);
	}

	for (int i = 0; i < m->n_items; i++) {
		struct menu_item *it = &m->items[i];
		int y = m->item_y[i], h = m->item_h[i];
		if (!it->label) {
			draw_groove_h(&cv, theme.bevel, y + SEP_H / 2 - 1, inner,
				s);
			continue;
		}
		if (i == m->armed)
			draw_bevel(&cv, theme.bevel, y, inner, h, 2, s, false);
		XftColor *fg = it->sensitive ? (XftColor *)&s->fg : &theme.dim;
		draw_label(&cv, it->label, it->mnemonic, fg,
			theme.bevel + PAD_X, y, h);
		if (it->accel) {
			int aw = str_width(it->accel);
			draw_label(&cv, it->accel, 0, fg,
				m->width - theme.bevel - PAD_X - aw, y, h);
		}
	}
	canvas_commit(&cv, m->win);
}

static void add(struct menu *m, const char *label, char mnemonic,
	const char *accel, enum action action, bool sensitive)
{
	if (m->n_items >= MENU_MAX_ITEMS)
		return;
	m->items[m->n_items++] = (struct menu_item){
		label, mnemonic, accel, action, sensitive, NULL,
	};
}

static void open_at(int x, int y)
{
	struct menu *m = &wm.menu;
	layout_menu(m);
	struct rect a = monitor_rect_at(x, y);
	if (x + m->width > a.x + a.w)
		x = a.x + a.w - m->width;
	if (y + m->height > a.y + a.h)
		y = a.y + a.h - m->height;
	m->x = MAX(x, a.x);
	m->y = MAX(y, a.y);
	m->armed = -1;
	m->opened = wm.time;
	m->moved = m->pressed = false;
	Window rw, cw;
	int wx, wy;
	unsigned mask;
	XQueryPointer(wm.dpy, wm.root, &rw, &cw, &m->open_x, &m->open_y,
		&wx, &wy, &mask);

	if (!m->win) {
		XSetWindowAttributes wa = {
			.override_redirect = True,
			.save_under = True,
			.background_pixel = theme.inactive.bg,
		};
		m->win = XCreateWindow(wm.dpy, wm.root, 0, 0, 1, 1, 0, wm.depth,
			InputOutput, wm.visual,
			CWOverrideRedirect | CWSaveUnder | CWBackPixel, &wa);
	}
	XMoveResizeWindow(wm.dpy, m->win, m->x, m->y, m->width, m->height);
	render_menu();
	XMapRaised(wm.dpy, m->win);

	if (XGrabPointer(wm.dpy, wm.root, False, ButtonPressMask |
			ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
			GrabModeAsync, None, wm.cursor[CUR_DEFAULT],
			CurrentTime) != GrabSuccess) {
		XUnmapWindow(wm.dpy, m->win);
		return;
	}
	/* Without the keyboard Escape and the arrows would go to whoever
	 * holds it, and only a click could close the menu. */
	if (XGrabKeyboard(wm.dpy, wm.root, False, GrabModeAsync,
			GrabModeAsync, CurrentTime) != GrabSuccess) {
		XUngrabPointer(wm.dpy, CurrentTime);
		XUnmapWindow(wm.dpy, m->win);
		return;
	}
	m->open = true;
	wm.grab = GRAB_MENU;
}

static void add_tabs(struct menu *m, struct client *c)
{
	bool group = c->group && !c->minimized;
	add(m, "Tab With Previous Window", 'T', "Super+G", ACT_TAB_JOIN,
		!c->minimized && tabs_previous(c));
	add(m, "Next Tab", 'e', "Super+Tab", ACT_TAB_NEXT,
		group && !c->fullscreen);
	add(m, "Previous Tab", 'P', "Super+Shift+Tab", ACT_TAB_PREVIOUS,
		group && !c->fullscreen);
	add(m, "Detach Tab", 'D', "Super+Shift+G", ACT_TAB_DETACH, group);
	add(m, NULL, 0, NULL, ACT_NONE, false);
}

void menu_open_window(struct client *c, int x, int y)
{
	struct menu *m = &wm.menu;
	if (wm.grab != GRAB_NONE && wm.grab != GRAB_MENU)
		return;
	menu_close();
	m->title = NULL;
	m->n_items = 0;
	m->target = c;
	m->from_button = false;
	bool maxed = c->maxed_v || c->maxed_h;
	bool normal = !c->minimized && !maxed && !c->fullscreen &&
		c->snap == SNAP_NONE;
	unsigned f = c->funcs;
	if (c->tiled) {
		/* In the layout: Minimize moves the main window into the
		 * stack or shades a stack window, Maximize makes a stack
		 * window the main one, Restore unshades. */
		bool main = tile_is_main(c);
		add(m, "Restore", 'R', "Alt+F5", ACT_RESTORE, c->shaded);
		add(m, "Move", 'M', "Alt+F7", ACT_MOVE, false);
		add(m, "Size", 'S', "Alt+F8", ACT_SIZE, false);
		add(m, "Minimize", 'n', "Alt+F9", ACT_MINIMIZE,
			(f & FUNC_MINIMIZE) && (main ? tile_has_stack() :
			tile_can_shade(c)));
		add(m, "Maximize", 'x', "Alt+F10", ACT_MAXIMIZE,
			(f & FUNC_MAXIMIZE) && !main);
		add(m, "Lower", 'L', "Alt+F3", ACT_LOWER, true);
		add(m, NULL, 0, NULL, ACT_NONE, false);
		add_tabs(m, c);
		add(m, "Close", 'C', "Alt+F4", ACT_CLOSE, f & FUNC_CLOSE);
		open_at(x, y);
		return;
	}
	add(m, "Restore", 'R', "Alt+F5", ACT_RESTORE, !normal);
	add(m, "Move", 'M', "Alt+F7", ACT_MOVE,
		(f & FUNC_MOVE) && !c->fullscreen);
	add(m, "Size", 'S', "Alt+F8", ACT_SIZE, (f & FUNC_RESIZE) && normal);
	add(m, "Minimize", 'n', "Alt+F9", ACT_MINIMIZE,
		(f & FUNC_MINIMIZE) && !c->minimized);
	add(m, "Maximize", 'x', "Alt+F10", ACT_MAXIMIZE,
		(f & FUNC_MAXIMIZE) && !(c->maxed_v && c->maxed_h) &&
		!c->fullscreen);
	add(m, "Lower", 'L', "Alt+F3", ACT_LOWER, true);
	add(m, NULL, 0, NULL, ACT_NONE, false);
	add_tabs(m, c);
	add(m, "Close", 'C', "Alt+F4", ACT_CLOSE, f & FUNC_CLOSE);
	open_at(x, y);
}

/* The first letter of the label that no other entry uses, also of those
 * still to come below it. */
static char free_mnemonic(const struct menu *m, const char *label)
{
	static const char own[] = "nudftlarq";
	for (const char *c = label; *c; c++) {
		if (!isalpha((unsigned char)*c))
			continue;
		int lower = tolower((unsigned char)*c);
		bool taken = strchr(own, lower);
		for (int i = 0; i < m->n_items && !taken; i++)
			taken = tolower((unsigned char)m->items[i].mnemonic) == lower;
		if (!taken)
			return *c;
	}
	return 0;
}

/* The programs from mwmrc, on top and set off from the rest. */
static void add_root_items(struct menu *m)
{
	int n = config_root_items();
	for (int i = 0; i < n; i++) {
		const char *label, *command;
		config_root_item(i, &label, &command);
		add(m, label, free_mnemonic(m, label), NULL, ACT_RUN, true);
		m->items[m->n_items - 1].command = command;
	}
	if (n)
		add(m, NULL, 0, NULL, ACT_NONE, false);
}

void menu_open_root(int x, int y)
{
	struct menu *m = &wm.menu;
	menu_close();
	m->title = "Root Menu";
	m->n_items = 0;
	m->target = NULL;
	m->from_button = false;
	add_root_items(m);
	add(m, "New Window", 'N', NULL, ACT_NEW_WINDOW, true);
	add(m, "Shuffle Up", 'U', NULL, ACT_SHUFFLE_UP, true);
	add(m, "Shuffle Down", 'D', NULL, ACT_SHUFFLE_DOWN, true);
	add(m, "Refresh", 'f', NULL, ACT_REFRESH, true);
	add(m, wm.tiling ? "Untile Windows" : "Tile Windows",
		wm.tiling ? 't' : 'T', "Super+T",
		ACT_TOGGLE_TILING, true);
	if (wm.tiling) {
		int top = client_tiled_stacking();
		if (top == 1)
			add(m, "Lower Tiled Windows", 'L', NULL,
				ACT_LOWER_TILED, true);
		else
			add(m, "Raise Tiled Windows", 'a', NULL,
				ACT_RAISE_TILED, top == 0);
	}
	add(m, NULL, 0, NULL, ACT_NONE, false);
	add(m, "Restart...", 'R', NULL, ACT_RESTART_CONFIRM, true);
	add(m, "Quit...", 'Q', NULL, ACT_QUIT_CONFIRM, true);
	open_at(x, y);
}

/* mwm asks before f.restart and f.quit_mwm. */
static void open_confirm(const char *title, enum action action, int x, int y)
{
	struct menu *m = &wm.menu;
	m->title = title;
	m->n_items = 0;
	m->target = NULL;
	m->from_button = false;
	add(m, "OK", 'O', NULL, action, true);
	add(m, "Cancel", 'C', NULL, ACT_NONE, true);
	open_at(x, y);
	m->armed = 1;
	render_menu();
}

void menu_close(void)
{
	struct menu *m = &wm.menu;
	if (!m->open)
		return;
	m->open = false;
	m->target = NULL;
	XUnmapWindow(wm.dpy, m->win);
	XUngrabPointer(wm.dpy, CurrentTime);
	XUngrabKeyboard(wm.dpy, CurrentTime);
	if (wm.grab == GRAB_MENU)
		wm.grab = GRAB_NONE;
}

static int item_at(struct menu *m, int x, int y)
{
	x -= m->x;
	y -= m->y;
	if (x < 0 || x >= m->width || y < 0 || y >= m->height)
		return -2; /* outside the menu */
	for (int i = 0; i < m->n_items; i++) {
		if (y >= m->item_y[i] && y < m->item_y[i] + m->item_h[i])
			return (m->items[i].label && m->items[i].sensitive) ?
				i : -1;
	}
	return -1;
}

static void arm(int idx)
{
	if (wm.menu.armed == idx)
		return;
	wm.menu.armed = idx;
	render_menu();
}

static void activate(int idx)
{
	struct menu *m = &wm.menu;
	enum action action = m->items[idx].action;
	const char *command = m->items[idx].command;
	struct client *target = m->target;
	int x = m->x, y = m->y;
	menu_close();
	if (action == ACT_RUN)
		spawn(command);
	else if (action == ACT_QUIT_CONFIRM)
		open_confirm("QUIT Mwm?", ACT_QUIT, x, y);
	else if (action == ACT_RESTART_CONFIRM)
		open_confirm("Restart Mwm?", ACT_RESTART, x, y);
	else
		run_action(action, target);
}

void menu_motion(int x, int y)
{
	struct menu *m = &wm.menu;
	if (abs(x - m->open_x) > 4 || abs(y - m->open_y) > 4)
		m->moved = true;
	int idx = item_at(m, x, y);
	arm(idx < 0 ? -1 : idx);
}

void menu_button(int x, int y, unsigned button, bool pressed, Time time)
{
	struct menu *m = &wm.menu;
	/* A wheel tick is a press and a release; it must neither pick the
	 * item under the pointer nor close the menu. */
	if (button > Button3)
		return;
	int idx = item_at(m, x, y);
	if (pressed) {
		if (idx != -2) {
			m->pressed = true;
			arm(idx);
			return;
		}
		/* mwm: double-clicking the menu button closes the window. */
		struct client *t = m->target;
		unsigned edges;
		bool close = m->from_button && t &&
			time - m->opened < DOUBLE_CLICK_MS &&
			frame_hit(t, x - t->x, y - t->y, &edges) ==
			PART_MENU_BUTTON;
		menu_close();
		if (close)
			client_close(t);
		return;
	}
	/* The release of the click that posted the menu must not pick the
	 * item that happens to lie under the pointer, e.g. when the menu
	 * was pushed up from the bottom edge; only a drag or a click in
	 * the menu selects. */
	if (idx >= 0 && (m->moved || m->pressed))
		activate(idx);
	else if (idx == -2 && time - m->opened > DRAG_RELEASE_MS)
		menu_close(); /* press-drag-release that ended outside */
}

static int step(struct menu *m, int from, int dir)
{
	for (int n = 0, i = from; n < m->n_items; n++) {
		i = (i + dir + m->n_items) % m->n_items;
		if (m->items[i].label && m->items[i].sensitive)
			return i;
	}
	return from;
}

void menu_key(KeySym sym)
{
	struct menu *m = &wm.menu;
	switch (sym) {
	case XK_Escape:
		menu_close();
		break;
	case XK_Down:
		arm(step(m, m->armed < 0 ? -1 : m->armed, 1));
		break;
	case XK_Up:
		arm(step(m, m->armed < 0 ? 0 : m->armed, -1));
		break;
	case XK_Return:
	case XK_KP_Enter:
	case XK_space:
		if (m->armed >= 0)
			activate(m->armed);
		break;
	default:
		if (sym > 0xff)
			break;
		for (int i = 0; i < m->n_items; i++) {
			struct menu_item *it = &m->items[i];
			if (it->label && it->sensitive && it->mnemonic &&
					tolower(it->mnemonic) == tolower((int)sym)) {
				activate(i);
				break;
			}
		}
		break;
	}
}

/* ---- actions ------------------------------------------------------------ */

static void refresh_screen(void)
{
	/* mwm's f.refresh: map and drop a window over everything so all
	 * clients get exposed and repaint. */
	XSetWindowAttributes a = { .override_redirect = True };
	Window w = XCreateWindow(wm.dpy, wm.root, 0, 0, wm.sw, wm.sh, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect, &a);
	XMapRaised(wm.dpy, w);
	XDestroyWindow(wm.dpy, w);
	XFlush(wm.dpy);
}

void run_action(enum action action, struct client *c)
{
	int px, py;

	if (action >= ACT_TAB_1 && action <= ACT_TAB_9) {
		if (c)
			tabs_show_nth(c, action - ACT_TAB_1);
		return;
	}
	switch (action) {
	case ACT_NONE:
	case ACT_RESTART_CONFIRM:
	case ACT_QUIT_CONFIRM:
	case ACT_RUN: /* see activate() */
		break;
	case ACT_SCREEN_SAVER:
		spawn(config_get("screenSaver"));
		break;
	case ACT_RESTORE:
		if (c)
			client_restore(c);
		break;
	case ACT_MOVE:
		if (!c || !(c->funcs & FUNC_MOVE) || c->fullscreen)
			break;
		if (c->minimized)
			client_restore(c);
		/* Like f.move from a menu: the pointer jumps to the title and
		 * the window follows it until the next click. */
		px = c->x + (c->w + frame_extra_w(c)) / 2;
		py = c->y + frame_border(c) + frame_title(c) / 2;
		/* The pointer only jumps if a grab follows. */
		if (begin_grab(c, GRAB_MOVE, 0, true, px, py))
			XWarpPointer(wm.dpy, None, wm.root, 0, 0, 0, 0, px, py);
		break;
	case ACT_SIZE:
		if (!c || !(c->funcs & FUNC_RESIZE) || c->fullscreen)
			break;
		if (c->minimized)
			client_restore(c);
		px = c->x + c->w + frame_extra_w(c) - 1;
		py = c->y + c->h + frame_extra_h(c) - 1;
		if (begin_grab(c, GRAB_RESIZE, EDGE_BOTTOM | EDGE_RIGHT, true,
				px, py))
			XWarpPointer(wm.dpy, None, wm.root, 0, 0, 0, 0, px, py);
		break;
	case ACT_MINIMIZE:
		if (c && c->tiled && (c->funcs & FUNC_MINIMIZE))
			tile_iconify(c);
		else if (c && (c->funcs & FUNC_MINIMIZE))
			client_minimize(c);
		break;
	case ACT_MAXIMIZE:
		if (c && c->tiled && (c->funcs & FUNC_MAXIMIZE)) {
			tile_promote(c);
		} else if (c && (c->funcs & FUNC_MAXIMIZE)) {
			if (c->minimized)
				client_restore(c);
			client_set_maximized(c, true, true);
		}
		break;
	case ACT_TOGGLE_MAXIMIZE:
		if (c && c->tiled && (c->funcs & FUNC_MAXIMIZE)) {
			tile_promote(c);
		} else if (c && (c->funcs & FUNC_MAXIMIZE) && !c->minimized) {
			bool on = !(c->maxed_v && c->maxed_h);
			client_set_maximized(c, on, on);
		}
		break;
	case ACT_TOGGLE_FULLSCREEN:
		if (c && !c->minimized && c->type != WT_DOCK &&
				c->type != WT_DESKTOP)
			client_set_fullscreen(c, !c->fullscreen);
		break;
	case ACT_LOWER:
		if (c)
			client_lower(c);
		break;
	case ACT_CLOSE:
		if (c)
			client_close(c);
		break;
	case ACT_WINDOW_MENU:
		if (c)
			menu_open_window(c, c->x + frame_border(c),
				c->y + frame_border(c) + frame_title(c));
		break;
	case ACT_NEW_WINDOW:
		spawn(wm.terminal);
		break;
	case ACT_SHUFFLE_UP:
		client_cycle(false);
		break;
	case ACT_SHUFFLE_DOWN:
		client_cycle(true);
		break;
	case ACT_REFRESH:
		refresh_screen();
		break;
	case ACT_TOGGLE_TILING:
		tile_toggle();
		break;
	case ACT_RAISE_TILED:
	case ACT_LOWER_TILED:
		client_stack_tiled(action == ACT_RAISE_TILED);
		break;
	case ACT_SNAP_LEFT:
	case ACT_SNAP_RIGHT:
	case ACT_SNAP_UP:
	case ACT_SNAP_DOWN:
		snap_step(c, action);
		break;
	case ACT_EDGE_LEFT:
	case ACT_EDGE_RIGHT:
	case ACT_EDGE_UP:
	case ACT_EDGE_DOWN:
		if (c)
			client_move_to_edge(c, action);
		break;
	case ACT_TAB_JOIN:
		if (c && tabs_previous(c))
			tabs_join(c, tabs_previous(c));
		break;
	case ACT_TAB_DETACH:
		if (c)
			tabs_detach(c);
		break;
	case ACT_TAB_NEXT:
	case ACT_TAB_PREVIOUS:
		if (c)
			tabs_step(c, action == ACT_TAB_NEXT);
		break;
	case ACT_TAB_1: /* handled above */
	case ACT_TAB_2:
	case ACT_TAB_3:
	case ACT_TAB_4:
	case ACT_TAB_5:
	case ACT_TAB_6:
	case ACT_TAB_7:
	case ACT_TAB_8:
	case ACT_TAB_9:
		break;
	case ACT_RESTART:
		wm.restart = true;
		wm.running = false;
		break;
	case ACT_QUIT:
		wm.running = false;
		break;
	}
}

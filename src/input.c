/*
 * Pointer and keyboard handling: frame interaction, interactive move and
 * resize with mwm's feedback box (as an outline by default, see
 * outline.c), and mwm's default key bindings.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include "mwm.h"

#define DRAG_THRESHOLD 8

struct key {
	unsigned mod;
	KeySym sym;
	enum action action;
};

/* mwm's default key bindings plus a terminal launcher and quit. */
static const struct key keys[] = {
	{ Mod1Mask, XK_Tab, ACT_SHUFFLE_UP },
	{ Mod1Mask | ShiftMask, XK_Tab, ACT_SHUFFLE_DOWN },
	{ Mod1Mask, XK_Escape, ACT_SHUFFLE_DOWN },
	{ Mod1Mask, XK_space, ACT_WINDOW_MENU },
	{ ShiftMask, XK_Escape, ACT_WINDOW_MENU },
	{ Mod1Mask, XK_F3, ACT_LOWER },
	{ Mod1Mask, XK_F4, ACT_CLOSE },
	{ Mod1Mask, XK_F5, ACT_RESTORE },
	{ Mod1Mask, XK_F7, ACT_MOVE },
	{ Mod1Mask, XK_F8, ACT_SIZE },
	{ Mod1Mask, XK_F9, ACT_MINIMIZE },
	{ Mod1Mask, XK_F10, ACT_TOGGLE_MAXIMIZE },
	{ Mod1Mask, XK_F11, ACT_TOGGLE_FULLSCREEN },
	{ ControlMask | Mod1Mask, XK_Return, ACT_NEW_WINDOW },
	{ Mod4Mask, XK_Return, ACT_NEW_WINDOW },
	{ ControlMask | Mod1Mask, XK_BackSpace, ACT_QUIT },
	{ Mod4Mask, XK_Left, ACT_SNAP_LEFT },
	{ Mod4Mask, XK_Right, ACT_SNAP_RIGHT },
	{ Mod4Mask, XK_Up, ACT_SNAP_UP },
	{ Mod4Mask, XK_Down, ACT_SNAP_DOWN },
};

void spawn(const char *cmd)
{
	if (!cmd || !*cmd)
		return;
	pid_t pid = fork();
	if (pid == 0) {
		/* Double fork so we never collect zombies. */
		setsid();
		if (fork() == 0) {
			close(ConnectionNumber(wm.dpy));
			execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
	}
}

Cursor edge_cursor(unsigned edges)
{
	switch (edges) {
	case EDGE_TOP: return wm.cursor[CUR_TOP];
	case EDGE_BOTTOM: return wm.cursor[CUR_BOTTOM];
	case EDGE_LEFT: return wm.cursor[CUR_LEFT];
	case EDGE_RIGHT: return wm.cursor[CUR_RIGHT];
	case EDGE_TOP | EDGE_LEFT: return wm.cursor[CUR_TOP_LEFT];
	case EDGE_TOP | EDGE_RIGHT: return wm.cursor[CUR_TOP_RIGHT];
	case EDGE_BOTTOM | EDGE_LEFT: return wm.cursor[CUR_BOTTOM_LEFT];
	case EDGE_BOTTOM | EDGE_RIGHT: return wm.cursor[CUR_BOTTOM_RIGHT];
	default: return wm.cursor[CUR_DEFAULT];
	}
}

void input_init(void)
{
	static const unsigned shapes[CUR_COUNT] = {
		[CUR_DEFAULT] = XC_left_ptr,
		[CUR_MOVE] = XC_fleur,
		[CUR_TOP] = XC_top_side,
		[CUR_BOTTOM] = XC_bottom_side,
		[CUR_LEFT] = XC_left_side,
		[CUR_RIGHT] = XC_right_side,
		[CUR_TOP_LEFT] = XC_top_left_corner,
		[CUR_TOP_RIGHT] = XC_top_right_corner,
		[CUR_BOTTOM_LEFT] = XC_bottom_left_corner,
		[CUR_BOTTOM_RIGHT] = XC_bottom_right_corner,
	};
	/* libX11 maps font cursors onto the Xcursor theme when present. */
	for (int i = 0; i < CUR_COUNT; i++)
		wm.cursor[i] = XCreateFontCursor(wm.dpy, shapes[i]);
	update_numlock();
	grab_keys();
}

void update_numlock(void)
{
	wm.numlock = 0;
	XModifierKeymap *mm = XGetModifierMapping(wm.dpy);
	KeyCode nl = XKeysymToKeycode(wm.dpy, XK_Num_Lock);
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < mm->max_keypermod; j++) {
			if (nl && mm->modifiermap[i * mm->max_keypermod + j] == nl)
				wm.numlock = 1u << i;
		}
	}
	XFreeModifiermap(mm);
}

unsigned clean_mask(unsigned state)
{
	return state & ~(wm.numlock | LockMask) & (ShiftMask | ControlMask |
		Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask);
}

void grab_keys(void)
{
	unsigned locks[] = { 0, LockMask, wm.numlock, wm.numlock | LockMask };
	XUngrabKey(wm.dpy, AnyKey, AnyModifier, wm.root);
	for (size_t i = 0; i < LENGTH(keys); i++) {
		KeyCode code = XKeysymToKeycode(wm.dpy, keys[i].sym);
		if (!code)
			continue;
		for (size_t l = 0; l < LENGTH(locks); l++)
			XGrabKey(wm.dpy, code, keys[i].mod | locks[l], wm.root,
				True, GrabModeAsync, GrabModeAsync);
	}
}

/* ---- interactive move / resize ---------------------------------------- */

static void update_feedback(void)
{
	struct client *c = wm.grabbed;
	char buf[64];
	if (wm.grab == GRAB_MOVE && wm.grab_zone != SNAP_NONE) {
		snprintf(buf, sizeof(buf), "%s", snap_name(wm.grab_zone));
	} else if (wm.grab == GRAB_MOVE) {
		snprintf(buf, sizeof(buf), "%+d %+d", c->x, c->y);
	} else {
		/* Sizes in the client's own units, e.g. 80x24 for xterm. */
		int w = c->w, h = c->h;
		if (c->inc_w > 1)
			w = (w - c->base_w) / c->inc_w;
		if (c->inc_h > 1)
			h = (h - c->base_h) / c->inc_h;
		snprintf(buf, sizeof(buf), "%d x %d", w, h);
	}
	feedback_show(buf);
}

void begin_grab(struct client *c, enum grab_mode mode, unsigned edges,
	bool until_click, int px, int py)
{
	if (wm.grab != GRAB_NONE)
		return;
	Cursor cur = mode == GRAB_MOVE ? wm.cursor[CUR_MOVE] :
		edge_cursor(edges);
	if (XGrabPointer(wm.dpy, wm.root, False, ButtonPressMask |
			ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
			GrabModeAsync, None, cur, CurrentTime) != GrabSuccess)
		return;
	/* Escape cancels, Return confirms, arrows nudge. A drag with a
	 * button held works without, a move from the menu or keyboard
	 * does not. */
	if (XGrabKeyboard(wm.dpy, wm.root, False, GrabModeAsync,
			GrabModeAsync, CurrentTime) != GrabSuccess &&
			until_click) {
		XUngrabPointer(wm.dpy, CurrentTime);
		return;
	}

	wm.grab = mode;
	wm.grabbed = c;
	wm.grab_edges = edges;
	wm.grab_until_click = until_click;
	wm.grab_orig = (struct rect){ c->x, c->y, c->w, c->h };
	wm.grab_orig_snap = c->snap;
	wm.grab_orig_max_v = c->maxed_v;
	wm.grab_orig_max_h = c->maxed_h;
	wm.grab_px = wm.grab_last_x = px;
	wm.grab_py = wm.grab_last_y = py;
	wm.grab_detached = false;
	wm.grab_zone = SNAP_NONE;
	wm.grab_box = client_box(c);
	wm.grab_outline = outline_wanted();
	wm.grab_deferred = false;
	/* From the menu or keyboard there is no press to drag from yet;
	 * show where the window is. A drag shows it on the first motion. */
	if (wm.grab_outline && until_click)
		outline_show(c);
	if (mode == GRAB_MOVE) {
		wm.grab_x = px - c->x;
		wm.grab_y = py - c->y;
	} else {
		struct rect *b = &wm.grab_box;
		wm.grab_x = px - ((edges & EDGE_RIGHT) ? b->x + b->w : b->x);
		wm.grab_y = py - ((edges & EDGE_BOTTOM) ? b->y + b->h : b->y);
	}
	update_feedback();
}

void end_grab(void)
{
	if (wm.grab == GRAB_BUTTON) {
		/* Only reached when the client goes away mid-press; the
		 * implicit grab died with its frame. */
		if (wm.grabbed) {
			wm.grabbed->pressed = PART_NONE;
			wm.grabbed->pressed_inside = false;
		}
		wm.grab = GRAB_NONE;
		wm.grabbed = NULL;
		return;
	}
	if (wm.grab != GRAB_MOVE && wm.grab != GRAB_RESIZE)
		return;
	struct client *c = wm.grabbed;
	enum snap zone = wm.grab == GRAB_MOVE ? wm.grab_zone : SNAP_NONE;
	bool deferred = wm.grab_outline && wm.grab_deferred;
	wm.grab = GRAB_NONE;
	wm.grabbed = NULL;
	wm.grab_zone = SNAP_NONE;
	wm.grab_outline = wm.grab_deferred = false;
	XUngrabPointer(wm.dpy, CurrentTime);
	XUngrabKeyboard(wm.dpy, CurrentTime);
	feedback_hide();
	snap_preview(SNAP_NONE, 0, 0);
	outline_hide();
	/* Wireframe: now the window itself goes where the outline was. A
	 * snap below configures it anyway. */
	if (c && deferred && zone == SNAP_NONE)
		client_configure(c);
	if (c && zone != SNAP_NONE) {
		/* Restoring later brings it back to where it was before this
		 * drag, not to the screen edge it was dropped on. */
		bool was_free = wm.grab_orig_snap == SNAP_NONE &&
			!wm.grab_orig_max_v && !wm.grab_orig_max_h;
		c->x = was_free ? wm.grab_orig.x : c->saved.x;
		c->y = was_free ? wm.grab_orig.y : c->saved.y;
		client_snap(c, zone, wm.grab_last_x, wm.grab_last_y);
		return;
	}
	/* A moved or resized window is no longer snapped, nor maximized
	 * in the axis that changed. */
	if (c && (c->snap != SNAP_NONE || c->maxed_v || c->maxed_h)) {
		struct rect o = wm.grab_orig;
		bool changed = c->x != o.x || c->y != o.y || c->w != o.w ||
			c->h != o.h;
		bool v = c->maxed_v && c->y == o.y && c->h == o.h;
		bool h = c->maxed_h && c->x == o.x && c->w == o.w;
		if (v != c->maxed_v || h != c->maxed_h ||
				(c->snap != SNAP_NONE && changed)) {
			/* Only the axes that are free now take the new
			 * geometry; a still maximized one keeps what it
			 * restores to. */
			c->snap = SNAP_NONE;
			c->maxed_v = v;
			c->maxed_h = h;
			if (!h) {
				c->saved.x = c->x;
				c->saved.w = c->w;
			}
			if (!v) {
				c->saved.y = c->y;
				c->saved.h = c->h;
			}
			frame_render(c);
			ewmh_set_state(c);
		}
	}
}

void cancel_grab(void)
{
	struct client *c = wm.grabbed;
	struct rect o = wm.grab_orig;
	/* Back to where it was, including a snap it was dragged out of;
	 * c->saved still holds the geometry from before that snap. */
	c->x = o.x;
	c->y = o.y;
	c->w = o.w;
	c->h = o.h;
	c->snap = wm.grab_orig_snap;
	c->maxed_v = wm.grab_orig_max_v;
	c->maxed_h = wm.grab_orig_max_h;
	client_configure(c);
	ewmh_set_state(c);
	wm.grab_zone = SNAP_NONE;
	end_grab();
}

static void process_move(int px, int py)
{
	struct client *c = wm.grabbed;
	wm.grab_last_x = px;
	wm.grab_last_y = py;
	if (!wm.grab_detached) {
		/* From the menu or keyboard every step counts, down to the
		 * 1 px of Shift+arrow, though not the motion of the warp to
		 * the title; a drag needs to get going first. */
		int t = wm.grab_until_click ? 1 : DRAG_THRESHOLD;
		if (abs(px - wm.grab_px) < t && abs(py - wm.grab_py) < t)
			return;
		wm.grab_detached = true;
		if (c->snap != SNAP_NONE || c->maxed_v || c->maxed_h) {
			/* As in Windows: the window comes off at its old size,
			 * holding on at the same spot of its width. */
			struct rect b = client_box(c);
			double frac = b.w ? (double)(wm.grab_px - b.x) / b.w : 0.5;
			client_snap(c, SNAP_NONE, px, py);
			b = client_box(c);
			wm.grab_x = (int)(frac * b.w);
			wm.grab_y = MIN(wm.grab_y, b.h - 1);
		}
	}
	client_move(c, px - wm.grab_x, py - wm.grab_y);

	/* The same zone on another monitor is another target. */
	enum snap zone = snap_zone_at(c, px, py);
	struct rect mon = monitor_rect_at(px, py);
	if (zone != wm.grab_zone || (zone != SNAP_NONE &&
			memcmp(&mon, &wm.grab_zone_mon, sizeof(mon)))) {
		wm.grab_zone = zone;
		wm.grab_zone_mon = mon;
		snap_preview(zone, px, py);
		/* Where the window goes stays visible over the snap target. */
		outline_raise();
	}
	update_feedback();
}

static void process_resize(int px, int py)
{
	struct client *c = wm.grabbed;
	struct rect *g = &wm.grab_box;
	unsigned edges = wm.grab_edges;
	int bx = px - wm.grab_x, by = py - wm.grab_y;

	int left = g->x, right = g->x + g->w;
	int top = g->y, bottom = g->y + g->h;
	if (edges & EDGE_LEFT)
		left = bx;
	else if (edges & EDGE_RIGHT)
		right = bx;
	if (edges & EDGE_TOP)
		top = by;
	else if (edges & EDGE_BOTTOM)
		bottom = by;

	/* Room for the title bar buttons, then the client's own hints. */
	int cw = MAX(right - left - frame_extra_w(c),
		frame_title(c) ? 3 * theme.title_height + 24 : 16);
	int ch = MAX(bottom - top - frame_extra_h(c), 8);
	client_apply_size_hints(c, &cw, &ch, true);
	/* The axis not being dragged keeps its size: rounding it to the
	 * client's increments would undo a full-height snap. */
	if (!(edges & (EDGE_LEFT | EDGE_RIGHT)))
		cw = wm.grab_orig.w;
	if (!(edges & (EDGE_TOP | EDGE_BOTTOM)))
		ch = wm.grab_orig.h;
	int w = cw + frame_extra_w(c), h = ch + frame_extra_h(c);
	if (edges & EDGE_LEFT)
		left = right - w;
	if (edges & EDGE_TOP)
		top = bottom - h;

	if (cw == c->w && ch == c->h && left == c->x && top == c->y)
		return;
	c->x = left;
	c->y = top;
	c->w = cw;
	c->h = ch;
	client_configure(c);
	update_feedback();
}

/* ---- pointer ------------------------------------------------------------ */

static bool is_double_click(struct client *c, enum frame_part part, Time t)
{
	bool dbl = wm.last_click_client == c && wm.last_click_part == part &&
		t - wm.last_click < DOUBLE_CLICK_MS;
	/* A double click must not chain into a triple click. */
	wm.last_click_client = dbl ? NULL : c;
	wm.last_click_part = part;
	wm.last_click = t;
	return dbl;
}

static void set_frame_cursor(struct client *c, Cursor cur)
{
	if (c->cursor == cur)
		return;
	c->cursor = cur;
	XDefineCursor(wm.dpy, c->frame, cur);
}

/* Alt + button anywhere in a window: move, lower, resize. */
static bool alt_action(struct client *c, XButtonEvent *e)
{
	if (!(clean_mask(e->state) & Mod1Mask))
		return false;
	switch (e->button) {
	case Button1:
		client_activate(c);
		if ((c->funcs & FUNC_MOVE) && !c->fullscreen)
			begin_grab(c, GRAB_MOVE, 0, false, e->x_root,
				e->y_root);
		return true;
	case Button2:
		client_lower(c);
		return true;
	case Button3: {
		client_activate(c);
		if (!(c->funcs & FUNC_RESIZE) || c->fullscreen ||
				(c->maxed_v && c->maxed_h))
			return true;
		/* Resize from the nearest corner. */
		struct rect b = client_box(c);
		unsigned edges = (e->x_root < b.x + b.w / 2 ? EDGE_LEFT :
			EDGE_RIGHT) | (e->y_root < b.y + b.h / 2 ? EDGE_TOP :
			EDGE_BOTTOM);
		begin_grab(c, GRAB_RESIZE, edges, false, e->x_root,
			e->y_root);
		return true;
	}
	}
	return false;
}

static void press_icon(struct client *c, XButtonEvent *e)
{
	/* Double-click restores, button 3 posts the window menu. */
	if (e->button == Button1 && is_double_click(c, PART_NONE, e->time))
		client_restore(c);
	else if (e->button == Button3)
		menu_open_window(c, e->x_root, e->y_root);
}

static void press_frame(struct client *c, XButtonEvent *e)
{
	unsigned edges;
	enum frame_part part = e->subwindow == c->win ? PART_CLIENT :
		frame_hit(c, e->x, e->y, &edges);

	client_activate(c);
	switch (part) {
	case PART_CLIENT:
	case PART_NONE:
		return;
	case PART_TITLE:
		if (e->button == Button3) {
			menu_open_window(c, e->x_root, e->y_root);
		} else if (e->button == Button1) {
			if (is_double_click(c, part, e->time))
				run_action(ACT_TOGGLE_MAXIMIZE, c);
			else if ((c->funcs & FUNC_MOVE) && !c->fullscreen)
				begin_grab(c, GRAB_MOVE, 0, false,
					e->x_root, e->y_root);
		} else if (e->button == Button2) {
			client_lower(c);
		}
		return;
	case PART_MENU_BUTTON:
		if (e->button != Button1 && e->button != Button3)
			return;
		/* The menu takes the grab; its own press handling sees the
		 * second click of a double click and closes the window. */
		menu_open_window(c, c->x + frame_border(c),
			c->y + frame_border(c) + frame_title(c));
		wm.menu.from_button = true;
		return;
	case PART_MINIMIZE_BUTTON:
	case PART_MAXIMIZE_BUTTON:
		if (e->button != Button1)
			return;
		/* The implicit grab on the frame delivers motion and the
		 * release; the button looks pressed while inside. */
		c->pressed = part;
		c->pressed_inside = true;
		wm.grabbed = c;
		wm.grab = GRAB_BUTTON;
		wm.grab_button = e->button;
		frame_render(c);
		return;
	case PART_RESIZE:
		if (e->button == Button1)
			begin_grab(c, GRAB_RESIZE, edges, false,
				e->x_root, e->y_root);
		return;
	}
}

void handle_button_press(XButtonEvent *e)
{
	/* A press during one of our grabs never belongs to a client; thaw
	 * the pointer in case it came through a synchronous passive grab. */
	if (wm.grab != GRAB_NONE)
		XAllowEvents(wm.dpy, AsyncPointer, CurrentTime);
	switch (wm.grab) {
	case GRAB_MENU:
		menu_button(e->x_root, e->y_root, e->button, true, e->time);
		return;
	case GRAB_MOVE:
	case GRAB_RESIZE:
		if (wm.grab_until_click)
			end_grab();
		return;
	case GRAB_BUTTON:
		return;
	case GRAB_NONE:
		break;
	}

	if (e->window == wm.root) {
		/* Desktop: the root menu, as mwm binds it to buttons 1 and 3 */
		if (e->subwindow == None &&
				(e->button == Button1 || e->button == Button3))
			menu_open_root(e->x_root, e->y_root);
		return;
	}

	struct client *c = client_find(e->window);
	if (!c) {
		XAllowEvents(wm.dpy, ReplayPointer, CurrentTime);
		return;
	}
	if (e->window == c->icon_win) {
		press_icon(c, e);
		return;
	}
	if (e->window == c->win) {
		/* Passive grab on the client: Alt binding or the click that
		 * focuses an inactive window, which then goes on to it. */
		if (!alt_action(c, e))
			client_activate(c);
		XAllowEvents(wm.dpy, ReplayPointer, CurrentTime);
		return;
	}
	if (alt_action(c, e))
		return;
	press_frame(c, e);
}

void handle_button_release(XButtonEvent *e)
{
	switch (wm.grab) {
	case GRAB_MENU:
		menu_button(e->x_root, e->y_root, e->button, false, e->time);
		return;
	case GRAB_MOVE:
	case GRAB_RESIZE: {
		/* A second button pressed and released during the drag does
		 * not end it; the button that started it does. */
		unsigned held = e->state & (Button1Mask | Button2Mask |
			Button3Mask) & ~(Button1Mask << (e->button - 1));
		if (!wm.grab_until_click && !held)
			end_grab();
		return;
	}
	case GRAB_BUTTON: {
		struct client *c = wm.grabbed;
		if (e->button != wm.grab_button)
			return;
		enum frame_part part = c->pressed;
		bool inside = c->pressed_inside;
		c->pressed = PART_NONE;
		c->pressed_inside = false;
		wm.grab = GRAB_NONE;
		wm.grabbed = NULL;
		frame_render(c);
		if (!inside)
			return;
		if (part == PART_MINIMIZE_BUTTON)
			run_action(ACT_MINIMIZE, c);
		else if (part == PART_MAXIMIZE_BUTTON)
			run_action(ACT_TOGGLE_MAXIMIZE, c);
		return;
	}
	case GRAB_NONE:
		break;
	}
	struct client *c = client_find(e->window);
	if (c && e->window == c->win)
		XAllowEvents(wm.dpy, ReplayPointer, CurrentTime);
}

void handle_motion(XMotionEvent *e)
{
	/* Only the latest position matters, but only up to the next other
	 * event: a motion queued after a button release must not be
	 * handled before it (a drop would land where the pointer went
	 * afterwards). */
	XEvent next;
	while (XEventsQueued(wm.dpy, QueuedAfterReading)) {
		XEvent peek;
		XPeekEvent(wm.dpy, &peek);
		if (peek.type != MotionNotify ||
				peek.xmotion.window != e->window)
			break;
		XNextEvent(wm.dpy, &next);
		e = &next.xmotion;
	}

	switch (wm.grab) {
	case GRAB_MOVE:
		process_move(e->x_root, e->y_root);
		return;
	case GRAB_RESIZE:
		process_resize(e->x_root, e->y_root);
		return;
	case GRAB_MENU:
		menu_motion(e->x_root, e->y_root);
		return;
	case GRAB_BUTTON: {
		struct client *c = wm.grabbed;
		unsigned edges;
		bool inside = frame_hit(c, e->x_root - c->x, e->y_root - c->y,
			&edges) == c->pressed;
		if (inside != c->pressed_inside) {
			c->pressed_inside = inside;
			frame_render(c);
		}
		return;
	}
	case GRAB_NONE:
		break;
	}

	/* Resize cursors on the border. */
	struct client *c = client_find(e->window);
	if (!c || e->window != c->frame || e->subwindow != None)
		return;
	unsigned edges;
	enum frame_part part = frame_hit(c, e->x, e->y, &edges);
	set_frame_cursor(c, part == PART_RESIZE ? edge_cursor(edges) :
		wm.cursor[CUR_DEFAULT]);
}

void handle_crossing(XCrossingEvent *e)
{
	struct client *c = client_find(e->window);
	if (!c || e->window != c->frame || wm.grab != GRAB_NONE)
		return;
	/* Into the client (or out of the frame): the client's own cursor
	 * applies, which may inherit ours, so reset to the arrow. */
	if (e->type == LeaveNotify || e->detail == NotifyInferior) {
		set_frame_cursor(c, wm.cursor[CUR_DEFAULT]);
		return;
	}
	unsigned edges;
	enum frame_part part = frame_hit(c, e->x, e->y, &edges);
	set_frame_cursor(c, part == PART_RESIZE ? edge_cursor(edges) :
		wm.cursor[CUR_DEFAULT]);
}

/* ---- keyboard ----------------------------------------------------------- */

static void grab_key(KeySym sym, unsigned mods)
{
	int step = (mods & ShiftMask) ? 1 : 10;
	int dx = 0, dy = 0;
	switch (sym) {
	case XK_Escape:
		cancel_grab();
		return;
	case XK_Return:
	case XK_KP_Enter:
		end_grab();
		return;
	case XK_Left:
		dx = -step;
		break;
	case XK_Right:
		dx = step;
		break;
	case XK_Up:
		dy = -step;
		break;
	case XK_Down:
		dy = step;
		break;
	default:
		return;
	}
	XWarpPointer(wm.dpy, None, None, 0, 0, 0, 0, dx, dy);
}

void handle_key_press(XKeyEvent *e)
{
	KeySym sym = XkbKeycodeToKeysym(wm.dpy, e->keycode, 0, 0);
	unsigned mods = clean_mask(e->state);

	if (wm.grab == GRAB_MENU) {
		char buf[8];
		KeySym typed;
		/* Mnemonics come from the typed character, which follows the
		 * layout and Shift; everything else from the keysym. */
		if (XLookupString(e, buf, sizeof(buf), &typed, NULL) == 1 &&
				typed > XK_space && typed <= XK_asciitilde)
			sym = (unsigned char)buf[0];
		menu_key(sym);
		return;
	}
	if (wm.grab == GRAB_MOVE || wm.grab == GRAB_RESIZE) {
		grab_key(sym, mods);
		return;
	}
	if (wm.grab != GRAB_NONE)
		return;

	for (size_t i = 0; i < LENGTH(keys); i++) {
		if (keys[i].sym == sym && keys[i].mod == mods) {
			run_action(keys[i].action, wm.focus);
			return;
		}
	}
}

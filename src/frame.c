/*
 * Motif window frame, icons and the move/size feedback box.
 *
 *   +--+------------------------------------+--+
 *   |  |                                    |  |  <- resize border with
 *   |  +---+------------------------+---+---+  |     etched corner grooves
 *   |  | = |         Title          | . | O |  |  <- menu, title, min, max
 *   |  +---+------------------------+---+---+  |
 *   |  |                                    |  |
 *   |  |             client                 |  |
 *   ...
 *
 * Which pieces are present follows _MOTIF_WM_HINTS: a window without
 * resize handles gets a thin border, one without a title bar just the
 * border, and buttons come and go with their decoration bits.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include "mwm.h"

static unsigned decor(struct client *c)
{
	return c->fullscreen ? 0 : c->decor;
}

int frame_border(struct client *c)
{
	unsigned d = decor(c);
	if (d & DECOR_RESIZEH)
		return theme.border;
	if (d & DECOR_BORDER)
		return theme.bevel + 1;
	return 0;
}

int frame_title(struct client *c)
{
	return (decor(c) & DECOR_TITLE) ? theme.title_height : 0;
}

int frame_extra_w(struct client *c)
{
	return 2 * frame_border(c);
}

int frame_extra_h(struct client *c)
{
	return 2 * frame_border(c) + frame_title(c);
}

/* Horizontal title bar layout; button positions are -1 when absent. */
struct title_layout {
	int menu_x, title_x, title_w, min_x, max_x;
};

static void title_layout(struct client *c, struct title_layout *tl)
{
	unsigned d = decor(c);
	int B = frame_border(c), T = theme.title_height;
	int left = B, right = B + c->w;
	tl->menu_x = tl->min_x = tl->max_x = -1;

	/* Drop the buttons a narrow window has no room for, minimize
	 * first, so drawing and hit testing never see them overlap. */
	static const unsigned order[] = {
		DECOR_MINIMIZE, DECOR_MAXIMIZE, DECOR_MENU,
	};
	int n = !!(d & DECOR_MENU) + !!(d & DECOR_MINIMIZE) +
		!!(d & DECOR_MAXIMIZE);
	for (size_t i = 0; i < LENGTH(order) && n * T > c->w; i++) {
		if (d & order[i]) {
			d &= ~order[i];
			n--;
		}
	}
	if (d & DECOR_MENU) {
		tl->menu_x = left;
		left += T;
	}
	if (d & DECOR_MAXIMIZE) {
		right -= T;
		tl->max_x = right;
	}
	if (d & DECOR_MINIMIZE) {
		right -= T;
		tl->min_x = right;
	}
	tl->title_x = left;
	tl->title_w = right - left;
}

static void draw_button(struct canvas *cv, struct client *c,
	enum frame_part part, int x, int y, int size, const struct shade *s)
{
	bool down = c->pressed == part && c->pressed_inside;
	draw_bevel(cv, x, y, size, size, 1, s, down);

	/* Glyphs are themselves little raised bevels, as in mwm. */
	int g;
	switch (part) {
	case PART_MENU_BUTTON: {
		int gw = size * 11 / 20, gh = 4;
		draw_bevel(cv, x + (size - gw) / 2, y + (size - gh) / 2,
			gw, gh, 1, s, false);
		break;
	}
	case PART_MINIMIZE_BUTTON:
		g = 5;
		draw_bevel(cv, x + (size - g) / 2, y + (size - g) / 2,
			g, g, 1, s, false);
		break;
	case PART_MAXIMIZE_BUTTON:
		g = size * 11 / 20;
		draw_bevel(cv, x + (size - g) / 2, y + (size - g) / 2,
			g, g, 1, s, c->maxed_v && c->maxed_h);
		break;
	default:
		break;
	}
}

void frame_render(struct client *c)
{
	const int B = frame_border(c), T = frame_title(c);
	if (!B && !T)
		return; /* the client covers the whole frame */
	/* Mid wireframe drag c->w/h are the outline's, not the frame's;
	 * end_grab renders once the frame has them. */
	if (wm.grab_deferred && c == wm.grabbed)
		return;

	const int W = c->w + 2 * B, H = c->h + 2 * B + T;
	const int bv = MIN(theme.bevel, B);
	const bool full = B == theme.border;
	const bool maximized = c->maxed_v && c->maxed_h;
	const struct shade *s = c == wm.focus ? c->active_shade :
		c->inactive_shade;

	struct canvas cv;
	canvas_begin(&cv, W, H);
	fill_rect(&cv, s->bg, 0, 0, W, H);

	if (B) {
		/* Outer raised edge, inner sunken edge around the inside. */
		draw_bevel(&cv, 0, 0, W, H, bv, s, false);
		if (full)
			draw_bevel(&cv, B - 1, B - 1, W - 2 * B + 2,
				H - 2 * B + 2, 1, s, true);
	}

	/* Resize corner grooves, cutting the border at title-bar distance. */
	if (full && (c->funcs & FUNC_RESIZE) && !maximized) {
		int k = B + theme.title_height;
		draw_groove_v(&cv, k, bv, B - bv - 1, s);
		draw_groove_v(&cv, W - k - 2, bv, B - bv - 1, s);
		draw_groove_v(&cv, k, H - B + 1, B - bv - 1, s);
		draw_groove_v(&cv, W - k - 2, H - B + 1, B - bv - 1, s);
		draw_groove_h(&cv, bv, k, B - bv - 1, s);
		draw_groove_h(&cv, W - B + 1, k, B - bv - 1, s);
		draw_groove_h(&cv, bv, H - k - 2, B - bv - 1, s);
		draw_groove_h(&cv, W - B + 1, H - k - 2, B - bv - 1, s);
	}

	if (T) {
		struct title_layout tl;
		title_layout(c, &tl);
		if (tl.menu_x >= 0)
			draw_button(&cv, c, PART_MENU_BUTTON, tl.menu_x, B, T, s);
		draw_bevel(&cv, tl.title_x, B, tl.title_w, T, 1, s,
			c->pressed == PART_TITLE);
		draw_text(&cv, c->title, (XftColor *)&s->fg, tl.title_x + 4,
			B + 1, tl.title_w - 8, T - 2, true);
		if (tl.min_x >= 0)
			draw_button(&cv, c, PART_MINIMIZE_BUTTON, tl.min_x, B,
				T, s);
		if (tl.max_x >= 0)
			draw_button(&cv, c, PART_MAXIMIZE_BUTTON, tl.max_x, B,
				T, s);
	}

	canvas_commit(&cv, c->frame);
}

enum frame_part frame_hit(struct client *c, int fx, int fy, unsigned *edges)
{
	*edges = 0;
	const int B = frame_border(c), T = frame_title(c);
	const int W = c->w + 2 * B, H = c->h + 2 * B + T;

	if (fx < 0 || fy < 0 || fx >= W || fy >= H)
		return PART_NONE;
	if (fx >= B && fx < W - B && fy >= B + T && fy < H - B)
		return PART_CLIENT;
	if (T && fx >= B && fx < W - B && fy >= B && fy < B + T) {
		struct title_layout tl;
		title_layout(c, &tl);
		if (tl.menu_x >= 0 && fx < tl.menu_x + T)
			return PART_MENU_BUTTON;
		if (tl.max_x >= 0 && fx >= tl.max_x)
			return PART_MAXIMIZE_BUTTON;
		if (tl.min_x >= 0 && fx >= tl.min_x && fx < tl.min_x + T)
			return PART_MINIMIZE_BUTTON;
		return PART_TITLE;
	}
	/* Thin borders and maximized windows only move. */
	if (B != theme.border || !(c->funcs & FUNC_RESIZE) ||
			(c->maxed_v && c->maxed_h))
		return PART_TITLE;

	/* Border: the corner pieces extend B+T along each side. */
	int k = B + theme.title_height;
	bool in_lr = fx < B || fx >= W - B;
	bool in_tb = fy < B || fy >= H - B;
	if (fx < B || (in_tb && fx < k))
		*edges |= EDGE_LEFT;
	if (fx >= W - B || (in_tb && fx >= W - k))
		*edges |= EDGE_RIGHT;
	if (fy < B || (in_lr && fy < k))
		*edges |= EDGE_TOP;
	if (fy >= H - B || (in_lr && fy >= H - k))
		*edges |= EDGE_BOTTOM;
	return PART_RESIZE;
}

/* ---- icons -------------------------------------------------------------- */

#define LABEL_H 18

/*
 * Draw the best fitting _NET_WM_ICON image into the box, blended over
 * the icon background. Returns false when the client has none (or the
 * visual is not TrueColor), so the caller can draw the default image.
 */
static bool draw_net_icon(struct canvas *cv, struct client *c, int bx, int by,
	int bw, int bh, uint32_t bg)
{
	if (wm.visual->class != TrueColor)
		return false;
	unsigned long n;
	long *data = get_property(c->win, atoms[NET_WM_ICON], XA_CARDINAL, &n);
	if (!data)
		return false;

	/* Smallest image that still fills the box, else the largest one. */
	int want = MIN(bw, bh);
	long *best = NULL;
	unsigned long iw = 0, ih = 0;
	for (unsigned long i = 0; i + 2 <= n;) {
		unsigned long w = data[i], h = data[i + 1];
		if (!w || !h || w > 1024 || h > 1024 || i + 2 + w * h > n)
			break;
		bool fits = MIN(w, h) >= (unsigned long)want;
		bool best_fits = best && MIN(iw, ih) >= (unsigned long)want;
		if (!best || (fits && (!best_fits || w * h < iw * ih)) ||
				(!fits && !best_fits && w * h > iw * ih)) {
			best = data + i + 2;
			iw = w;
			ih = h;
		}
		i += 2 + w * h;
	}
	if (!best) {
		XFree(data);
		return false;
	}

	double scale = MIN((double)bw / iw, (double)bh / ih);
	if (scale > 1 && MIN(iw, ih) * 2 > (unsigned long)want)
		scale = 1; /* do not blow up icons that are nearly right */
	int dw = MAX(1, (int)(iw * scale)), dh = MAX(1, (int)(ih * scale));
	XImage *img = XCreateImage(wm.dpy, wm.visual, wm.depth, ZPixmap, 0,
		NULL, dw, dh, 32, 0);
	if (!img) {
		XFree(data);
		return false;
	}
	img->data = malloc((size_t)img->bytes_per_line * dh);
	if (!img->data) {
		XDestroyImage(img);
		XFree(data);
		return false;
	}
	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			uint32_t p = best[(unsigned long)(y / scale) * iw +
				(unsigned long)(x / scale)];
			uint32_t a = p >> 24, out = 0;
			for (int sh = 0; sh <= 16; sh += 8) {
				uint32_t fc = (p >> sh) & 0xff;
				uint32_t bc = (bg >> sh) & 0xff;
				out |= ((fc * a + bc * (255 - a)) / 255) << sh;
			}
			XPutPixel(img, x, y, truecolor_pixel(out));
		}
	}
	XPutImage(wm.dpy, cv->pm, wm.gc, img, 0, 0, bx + (bw - dw) / 2,
		by + (bh - dh) / 2, dw, dh);
	XDestroyImage(img);
	XFree(data);
	return true;
}

void icon_render(struct client *c)
{
	/* Icons wear the client's colours like its frame does. */
	const struct shade *s = c->inactive_shade;
	int img_h = ICON_H - LABEL_H;
	struct canvas cv;
	canvas_begin(&cv, ICON_W, ICON_H);

	fill_rect(&cv, s->bg, 0, 0, ICON_W, ICON_H);
	draw_bevel(&cv, 0, 0, ICON_W, img_h, 2, s, false);
	draw_bevel(&cv, 0, img_h, ICON_W, LABEL_H, 2, s, false);

	if (!draw_net_icon(&cv, c, 6, 4, ICON_W - 12, img_h - 8,
			s->bg_rgb)) {
		/* Default image: a miniature Motif window in a sunken well. */
		int wx = 6, wy = 6, ww = ICON_W - 12, wh = img_h - 12;
		fill_rect(&cv, theme.root, wx, wy, ww, wh);
		draw_bevel(&cv, wx, wy, ww, wh, 1, s, true);
		const struct shade *a = c->active_shade;
		int mx = wx + 8, my = wy + 6, mw = ww - 16, mh = wh - 12;
		fill_rect(&cv, a->bg, mx, my, mw, mh);
		draw_bevel(&cv, mx, my, mw, mh, 1, a, false);
		draw_bevel(&cv, mx + 2, my + 2, mw - 4, 7, 1, a, false);
		fill_rect(&cv, s->bg, mx + 2, my + 10, mw - 4, mh - 12);
	}

	const char *label = c->icon_name && *c->icon_name ?
		c->icon_name : c->title;
	draw_text(&cv, label, (XftColor *)&s->fg, 3, img_h + 1, ICON_W - 6,
		LABEL_H - 2, true);
	canvas_commit(&cv, c->icon_win);
}

static bool icon_slot_taken(struct client *self, int x, int y)
{
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (c != self && c->icon_win && c->minimized &&
				!c->hidden_with_parent && c->icon_placed &&
				c->icon_x == x && c->icon_y == y)
			return true;
	}
	return false;
}

/* Icons go along the bottom-left of the monitor, like mwm's default
 * iconPlacement "left bottom". A window keeps its slot when re-iconified. */
void icons_arrange(void)
{
	for (int i = wm.n_clients - 1; i >= 0; i--) {
		struct client *c = wm.clients[i];
		if (!c->minimized || c->hidden_with_parent || !c->icon_win)
			continue;
		struct rect a = monitor_usable_at(c->x + 1, c->y + 1);
		int row_y = a.y + a.h - ICON_H - 8;
		bool inside = c->icon_x >= a.x && c->icon_y >= a.y &&
			c->icon_x + ICON_W <= a.x + a.w &&
			c->icon_y + ICON_H <= a.y + a.h;
		if (!c->icon_placed || !inside ||
				icon_slot_taken(c, c->icon_x, c->icon_y)) {
			int per_row = MAX(1, (a.w - 8) / (ICON_W + 8));
			for (int k = 0;; k++) {
				int x = a.x + 8 + (k % per_row) * (ICON_W + 8);
				int y = row_y - (k / per_row) * (ICON_H + 8);
				if (!icon_slot_taken(c, x, y) || y < a.y) {
					c->icon_x = x;
					c->icon_y = y;
					break;
				}
			}
			c->icon_placed = true;
		}
		XMoveWindow(wm.dpy, c->icon_win, c->icon_x, c->icon_y);
	}
}

/* ---- feedback ----------------------------------------------------------- */

/* The little box mwm shows in the middle of the screen while a window is
 * moved ("+x+y") or resized ("80x24" in the client's size increments). */
void feedback_show(const char *text)
{
	const struct shade *s = &theme.inactive;
	int w = text_width(text, strlen(text)) + 24;
	int h = theme.font->ascent + theme.font->descent + 14;
	w = MAX(w, 80);

	struct client *c = wm.grabbed;
	struct rect m = c ? monitor_rect_at(c->x + 1, c->y + 1) :
		monitor_rect_at(0, 0);
	int x = m.x + (m.w - w) / 2, y = m.y + (m.h - h) / 2;

	if (!wm.feedback) {
		XSetWindowAttributes a = {
			.override_redirect = True,
			.save_under = True,
			.background_pixel = s->bg,
		};
		wm.feedback = XCreateWindow(wm.dpy, wm.root, x, y, w, h, 0,
			wm.depth, InputOutput, wm.visual,
			CWOverrideRedirect | CWSaveUnder | CWBackPixel, &a);
	}
	XMoveResizeWindow(wm.dpy, wm.feedback, x, y, w, h);

	struct canvas cv;
	canvas_begin(&cv, w, h);
	fill_rect(&cv, s->bg, 0, 0, w, h);
	draw_bevel(&cv, 0, 0, w, h, theme.bevel, s, false);
	draw_bevel(&cv, 4, 4, w - 8, h - 8, 1, s, true);
	draw_text(&cv, text, (XftColor *)&s->fg, 6, 4, w - 12, h - 8, true);
	canvas_commit(&cv, wm.feedback);
	XMapRaised(wm.dpy, wm.feedback);
}

void feedback_hide(void)
{
	if (wm.feedback)
		XUnmapWindow(wm.dpy, wm.feedback);
}

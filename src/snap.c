/*
 * Windows-style snapping. Dragging a window with the pointer against a
 * monitor edge snaps it to that half, against a corner to that quarter,
 * against the top edge to the whole work area. A Motif-bevelled outline
 * shows the target while dragging. Super+arrow keys step between the
 * same positions, the way Windows 11 does.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/extensions/shape.h>
#include "mwm.h"

#define SNAP_EDGE 2      /* pointer this close to a monitor edge */
#define SNAP_CORNER 64   /* corner zone along each edge */
#define PREVIEW_W 8      /* outline thickness */

static Window preview;
static int have_shape = -1;

/* Frame rectangle of a snap position inside the work area a. */
struct rect snap_rect(enum snap s, struct rect a)
{
	int hw = a.w / 2, hh = a.h / 2;
	switch (s) {
	case SNAP_LEFT:
		return (struct rect){ a.x, a.y, hw, a.h };
	case SNAP_RIGHT:
		return (struct rect){ a.x + hw, a.y, a.w - hw, a.h };
	case SNAP_TOP_LEFT:
		return (struct rect){ a.x, a.y, hw, hh };
	case SNAP_TOP_RIGHT:
		return (struct rect){ a.x + hw, a.y, a.w - hw, hh };
	case SNAP_BOTTOM_LEFT:
		return (struct rect){ a.x, a.y + hh, hw, a.h - hh };
	case SNAP_BOTTOM_RIGHT:
		return (struct rect){ a.x + hw, a.y + hh, a.w - hw, a.h - hh };
	default:
		return a;
	}
}

const char *snap_name(enum snap s)
{
	switch (s) {
	case SNAP_LEFT: return "Left half";
	case SNAP_RIGHT: return "Right half";
	case SNAP_TOP_LEFT: return "Top left";
	case SNAP_TOP_RIGHT: return "Top right";
	case SNAP_BOTTOM_LEFT: return "Bottom left";
	case SNAP_BOTTOM_RIGHT: return "Bottom right";
	case SNAP_MAXIMIZE: return "Maximize";
	default: return "";
	}
}

/* The snap position the pointer at (px, py) asks for while dragging c. */
enum snap snap_zone_at(struct client *c, int px, int py)
{
	if (!(c->funcs & FUNC_RESIZE) || c->fullscreen)
		return SNAP_NONE;
	struct rect m = monitor_rect_at(px, py);
	bool left = px <= m.x + SNAP_EDGE;
	bool right = px >= m.x + m.w - 1 - SNAP_EDGE;
	bool top = py <= m.y + SNAP_EDGE;
	bool bottom = py >= m.y + m.h - 1 - SNAP_EDGE;
	bool near_top = py < m.y + SNAP_CORNER;
	bool near_bottom = py >= m.y + m.h - SNAP_CORNER;
	bool near_left = px < m.x + SNAP_CORNER;
	bool near_right = px >= m.x + m.w - SNAP_CORNER;

	if (left || right) {
		if (near_top)
			return left ? SNAP_TOP_LEFT : SNAP_TOP_RIGHT;
		if (near_bottom)
			return left ? SNAP_BOTTOM_LEFT : SNAP_BOTTOM_RIGHT;
		return left ? SNAP_LEFT : SNAP_RIGHT;
	}
	if (top) {
		if (near_left)
			return SNAP_TOP_LEFT;
		if (near_right)
			return SNAP_TOP_RIGHT;
		return (c->funcs & FUNC_MAXIMIZE) ? SNAP_MAXIMIZE : SNAP_NONE;
	}
	if (bottom) {
		if (near_left)
			return SNAP_BOTTOM_LEFT;
		if (near_right)
			return SNAP_BOTTOM_RIGHT;
	}
	return SNAP_NONE;
}

/* Show the outline of snap position s on the monitor at (px, py), or
 * hide it for SNAP_NONE. It is a shaped ring, so the windows below stay
 * visible without a compositor. */
void snap_preview(enum snap s, int px, int py)
{
	if (have_shape < 0) {
		int ev, err;
		have_shape = XShapeQueryExtension(wm.dpy, &ev, &err);
	}
	if (s == SNAP_NONE || !have_shape) {
		if (preview)
			XUnmapWindow(wm.dpy, preview);
		return;
	}

	snap_preview_box(snap_rect(s, monitor_usable_at(px, py)));
}

/* The same ring around any box: where a window taken along would join. */
void snap_preview_box(struct rect r)
{
	if (have_shape < 0) {
		int ev, err;
		have_shape = XShapeQueryExtension(wm.dpy, &ev, &err);
	}
	if (!have_shape)
		return;
	const int t = PREVIEW_W;
	if (!preview) {
		XSetWindowAttributes a = {
			.override_redirect = True,
			.save_under = True,
			.background_pixel = theme.active.bg,
		};
		preview = XCreateWindow(wm.dpy, wm.root, r.x, r.y, r.w, r.h, 0,
			wm.depth, InputOutput, wm.visual,
			CWOverrideRedirect | CWSaveUnder | CWBackPixel, &a);
	}
	XMoveResizeWindow(wm.dpy, preview, r.x, r.y, r.w, r.h);

	XRectangle ring[] = {
		{ 0, 0, r.w, t },
		{ 0, r.h - t, r.w, t },
		{ 0, t, t, r.h - 2 * t },
		{ r.w - t, t, t, r.h - 2 * t },
	};
	XShapeCombineRectangles(wm.dpy, preview, ShapeBounding, 0, 0, ring,
		LENGTH(ring), ShapeSet, Unsorted);

	/* A frame like mwm's, in the dragged window's colour: raised
	 * outside, sunken towards the inside. */
	const struct shade *sh = wm.grabbed ? wm.grabbed->active_shade :
		&theme.active;
	struct canvas cv;
	canvas_begin(&cv, r.w, r.h);
	fill_rect(&cv, sh->bg, 0, 0, r.w, r.h);
	draw_bevel(&cv, 0, 0, r.w, r.h, theme.bevel, sh, false);
	draw_bevel(&cv, t - 2, t - 2, r.w - 2 * (t - 2), r.h - 2 * (t - 2), 2,
		sh, true);
	canvas_commit(&cv, preview);
	XMapRaised(wm.dpy, preview);
}

/* Super+arrow keys, as in Windows 11: halves left and right, up to a top
 * quarter or maximized, down to a bottom quarter, back, or minimized. */
void snap_step(struct client *c, enum action dir)
{
	if (!c || c->minimized || c->fullscreen || !(c->funcs & FUNC_RESIZE))
		return;
	enum snap cur = c->snap, next = cur;
	bool maxed = cur == SNAP_NONE && c->maxed_v && c->maxed_h;

	switch (dir) {
	case ACT_SNAP_LEFT:
		if (cur == SNAP_NONE)
			next = SNAP_LEFT;
		else if (cur == SNAP_RIGHT)
			next = SNAP_NONE;
		else if (cur == SNAP_TOP_RIGHT)
			next = SNAP_TOP_LEFT;
		else if (cur == SNAP_BOTTOM_RIGHT)
			next = SNAP_BOTTOM_LEFT;
		break;
	case ACT_SNAP_RIGHT:
		if (cur == SNAP_NONE)
			next = SNAP_RIGHT;
		else if (cur == SNAP_LEFT)
			next = SNAP_NONE;
		else if (cur == SNAP_TOP_LEFT)
			next = SNAP_TOP_RIGHT;
		else if (cur == SNAP_BOTTOM_LEFT)
			next = SNAP_BOTTOM_RIGHT;
		break;
	case ACT_SNAP_UP:
		if (maxed)
			return;
		if (cur == SNAP_NONE && (c->funcs & FUNC_MAXIMIZE))
			next = SNAP_MAXIMIZE;
		else if (cur == SNAP_LEFT)
			next = SNAP_TOP_LEFT;
		else if (cur == SNAP_RIGHT)
			next = SNAP_TOP_RIGHT;
		else if (cur == SNAP_BOTTOM_LEFT)
			next = SNAP_LEFT;
		else if (cur == SNAP_BOTTOM_RIGHT)
			next = SNAP_RIGHT;
		break;
	case ACT_SNAP_DOWN:
		if (maxed) {
			next = SNAP_NONE;
			break;
		}
		if (cur == SNAP_NONE) {
			if (c->funcs & FUNC_MINIMIZE)
				client_minimize(c);
			return;
		}
		if (cur == SNAP_LEFT)
			next = SNAP_BOTTOM_LEFT;
		else if (cur == SNAP_RIGHT)
			next = SNAP_BOTTOM_RIGHT;
		else if (cur == SNAP_TOP_LEFT)
			next = SNAP_LEFT;
		else if (cur == SNAP_TOP_RIGHT)
			next = SNAP_RIGHT;
		break;
	default:
		return;
	}
	if (next == cur && !maxed)
		return;

	struct rect b = client_box(c);
	client_snap(c, next, b.x + b.w / 2, b.y + b.h / 2);
}

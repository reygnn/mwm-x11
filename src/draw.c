/*
 * Theme colours, Motif bevel primitives and Xft text, all drawn into an
 * off-screen pixmap that then becomes a window's background.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mwm.h"

struct theme theme;

static uint32_t parse_env_color(const char *var, uint32_t fallback)
{
	const char *s = getenv(var);
	if (s) {
		if (*s == '#')
			s++;
		char *end;
		unsigned long v = strtoul(s, &end, 16);
		if (*end == '\0' && end - s == 6)
			return (uint32_t)v;
		wm_log("ignoring %s=%s, expected #rrggbb", var, getenv(var));
	}
	return fallback;
}

/* Move each channel of rgb the fraction f of the way towards `to`. */
static uint32_t mix(uint32_t rgb, uint32_t to, float f)
{
	uint32_t out = 0;
	for (int shift = 0; shift <= 16; shift += 8) {
		float c = (rgb >> shift) & 0xff, t = (to >> shift) & 0xff;
		out |= (uint32_t)(c + (t - c) * f + 0.5f) << shift;
	}
	return out;
}

static unsigned long alloc_pixel(uint32_t rgb)
{
	XColor c = {
		.red = ((rgb >> 16) & 0xff) * 257,
		.green = ((rgb >> 8) & 0xff) * 257,
		.blue = (rgb & 0xff) * 257,
		.flags = DoRed | DoGreen | DoBlue,
	};
	if (!XAllocColor(wm.dpy, wm.cmap, &c))
		return BlackPixel(wm.dpy, wm.screen);
	return c.pixel;
}

static void alloc_xft(XftColor *out, uint32_t rgb)
{
	XRenderColor rc = {
		((rgb >> 16) & 0xff) * 257, ((rgb >> 8) & 0xff) * 257,
		(rgb & 0xff) * 257, 0xffff,
	};
	XftColorAllocValue(wm.dpy, wm.visual, wm.cmap, &rc, out);
}

/*
 * Derive the top/bottom shadow colours from a background the way Motif's
 * XmeGetDefaultThreeDColors roughly does: lighten towards white for the
 * top shadow, darken towards black for the bottom shadow.
 */
static void shade_from(struct shade *s, uint32_t bg, uint32_t fg)
{
	s->bg_rgb = bg;
	s->bg = alloc_pixel(bg);
	s->top = alloc_pixel(mix(bg, 0xffffff, 0.55f));
	s->bottom = alloc_pixel(mix(bg, 0x000000, 0.5f));
	alloc_xft(&s->fg, fg);
}

void theme_init(void)
{
	/* Defaults follow the Motif 2.x look: #aeb2c3 grey-blue background,
	 * a stronger blue for the window that has the focus. Each one can be
	 * overridden through the environment, e.g. MWM_ACTIVE=#b24d7a. */
	uint32_t inactive = parse_env_color("MWM_INACTIVE", 0xaeb2c3);
	uint32_t active = parse_env_color("MWM_ACTIVE", 0x7387ad);
	uint32_t root = parse_env_color("MWM_ROOT", 0x5b7792);

	shade_from(&theme.inactive, inactive, 0x000000);
	shade_from(&theme.active, active, 0xffffff);
	alloc_xft(&theme.dim, mix(inactive, mix(inactive, 0, 0.5f), 0.5f));
	theme.root = alloc_pixel(root);

	const char *font = getenv("MWM_FONT");
	if (!font || !*font)
		font = "Sans:bold:size=10";
	theme.font = XftFontOpenName(wm.dpy, wm.screen, font);
	if (!theme.font)
		theme.font = XftFontOpenName(wm.dpy, wm.screen, "fixed");
	if (!theme.font) {
		fprintf(stderr, "mwm: cannot open font %s\n", font);
		exit(1);
	}

	theme.border = 6;
	theme.bevel = 2;
	/* Grow the title bar with the font, e.g. for large DPI settings. */
	theme.title_height = MAX(22, theme.font->ascent +
		theme.font->descent + 8);
}

/* Pixel value for an RGB colour on the default TrueColor visual. */
unsigned long truecolor_pixel(uint32_t rgb)
{
	const unsigned long masks[3] = {
		wm.visual->red_mask, wm.visual->green_mask, wm.visual->blue_mask,
	};
	unsigned long px = 0;
	for (int i = 0; i < 3; i++) {
		unsigned long m = masks[i];
		if (!m)
			continue;
		int shift = 0;
		while (!((m >> shift) & 1))
			shift++;
		unsigned long max = m >> shift;
		unsigned long c = (rgb >> (16 - 8 * i)) & 0xff;
		px |= ((c * max + 127) / 255) << shift;
	}
	return px;
}

/* ---- canvas ------------------------------------------------------------- */

void canvas_begin(struct canvas *cv, int w, int h)
{
	cv->w = MAX(w, 1);
	cv->h = MAX(h, 1);
	cv->pm = XCreatePixmap(wm.dpy, wm.root, cv->w, cv->h, wm.depth);
	cv->xft = XftDrawCreate(wm.dpy, cv->pm, wm.visual, wm.cmap);
}

void canvas_commit(struct canvas *cv, Window win)
{
	XftDrawDestroy(cv->xft);
	XSetWindowBackgroundPixmap(wm.dpy, win, cv->pm);
	XFreePixmap(wm.dpy, cv->pm);
	XClearWindow(wm.dpy, win);
}

void fill_rect(struct canvas *cv, unsigned long pixel, int x, int y,
	int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	XSetForeground(wm.dpy, wm.gc, pixel);
	XFillRectangle(wm.dpy, cv->pm, wm.gc, x, y, w, h);
}

static void fill_poly(struct canvas *cv, unsigned long pixel, XPoint *p,
	int n)
{
	XSetForeground(wm.dpy, wm.gc, pixel);
	XFillPolygon(wm.dpy, cv->pm, wm.gc, p, n, Nonconvex, CoordModeOrigin);
}

void draw_bevel(struct canvas *cv, int x, int y, int w, int h, int t,
	const struct shade *s, bool in)
{
	if (w <= 0 || h <= 0)
		return;
	/* Each shadow is a polygon so the diagonal joins at the top-right
	 * and bottom-left corners look exactly like Motif's. */
	XPoint tl[] = {
		{ x, y }, { x + w, y }, { x + w - t, y + t }, { x + t, y + t },
		{ x + t, y + h - t }, { x, y + h },
	};
	XPoint br[] = {
		{ x + w, y + h }, { x, y + h }, { x + t, y + h - t },
		{ x + w - t, y + h - t }, { x + w - t, y + t }, { x + w, y },
	};
	fill_poly(cv, in ? s->bottom : s->top, tl, LENGTH(tl));
	fill_poly(cv, in ? s->top : s->bottom, br, LENGTH(br));
}

void draw_groove_h(struct canvas *cv, int x, int y, int w,
	const struct shade *s)
{
	fill_rect(cv, s->bottom, x, y, w, 1);
	fill_rect(cv, s->top, x, y + 1, w, 1);
}

void draw_groove_v(struct canvas *cv, int x, int y, int h,
	const struct shade *s)
{
	fill_rect(cv, s->bottom, x, y, 1, h);
	fill_rect(cv, s->top, x + 1, y, 1, h);
}

/* ---- text --------------------------------------------------------------- */

int text_width(const char *text, int len)
{
	XGlyphInfo gi;
	XftTextExtentsUtf8(wm.dpy, theme.font, (const FcChar8 *)text, len, &gi);
	return gi.xOff;
}

void draw_text(struct canvas *cv, const char *text, XftColor *fg,
	int x, int y, int w, int h, bool center)
{
	if (!text || !*text || w <= 0)
		return;
	static const char ellipsis[] = "\xe2\x80\xa6";
	char buf[512];
	const char *out = text;
	int n = strlen(text);

	if (text_width(text, n) > w) {
		/* Cut at a UTF-8 boundary until text plus ellipsis fits. */
		int ew = text_width(ellipsis, 3);
		if (n > (int)sizeof(buf) - 4)
			n = sizeof(buf) - 4;
		while (n > 0) {
			n--;
			while (n > 0 && (text[n] & 0xc0) == 0x80)
				n--;
			if (text_width(text, n) + ew <= w)
				break;
		}
		memcpy(buf, text, n);
		memcpy(buf + n, ellipsis, 3);
		n += 3;
		out = buf;
	}

	int tw = text_width(out, n);
	int tx = center ? x + (w - tw) / 2 : x;
	int fh = theme.font->ascent + theme.font->descent;
	int ty = y + (h - fh) / 2 + theme.font->ascent;
	XRectangle clip = { x, y, w, h };
	XftDrawSetClipRectangles(cv->xft, 0, 0, &clip, 1);
	XftDrawStringUtf8(cv->xft, fg, theme.font, tx, ty,
		(const FcChar8 *)out, n);
	XftDrawSetClip(cv->xft, NULL);
}

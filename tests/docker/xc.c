/*
 * Test helper for review.sh:
 *   xc win TITLE [--geo WxH+X+Y] [--transient ID] [--minh N] [--usertime0]
 *                                 a window that closes on WM_DELETE_WINDOW
 *   xc state ID add|remove ATOM   send _NET_WM_STATE, as a client would
 *   xc grabbed                    "yes" if the keyboard is grabbed
 *   xc holdpointer SECONDS        hold a pointer grab, so that others fail
 *   xc rootchanged                tell the manager the root window changed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

static int win(Display *d, int argc, char **argv)
{
	int w = 300, h = 200, x = 50, y = 50, minh = 0, usertime0 = 0;
	Window transient = None;
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--geo") && i + 1 < argc)
			sscanf(argv[++i], "%dx%d+%d+%d", &w, &h, &x, &y);
		else if (!strcmp(argv[i], "--transient") && i + 1 < argc)
			transient = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--minh") && i + 1 < argc)
			minh = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--usertime0"))
			usertime0 = 1;
	}
	Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), x, y, w, h, 0,
		0, 0xdddddd);
	XStoreName(d, win, argv[2]);
	XClassHint ch = { (char *)"xc", (char *)"Xc" };
	XSetClassHint(d, win, &ch);
	XSizeHints sh = { 0 };
	if (minh) {
		sh.flags = PMinSize;
		sh.min_width = 50;
		sh.min_height = minh;
	}
	XSetWMNormalHints(d, win, &sh);
	if (transient)
		XSetTransientForHint(d, win, transient);
	if (usertime0) {
		long t = 0;
		XChangeProperty(d, win, XInternAtom(d, "_NET_WM_USER_TIME", False),
			XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&t, 1);
	}
	Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(d, win, &del, 1);
	XMapWindow(d, win);
	for (;;) {
		XEvent ev;
		XNextEvent(d, &ev);
		if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == del)
			return 0;
	}
}

static int state(Display *d, char **argv)
{
	XClientMessageEvent e = { 0 };
	e.type = ClientMessage;
	e.window = strtoul(argv[2], NULL, 0);
	e.message_type = XInternAtom(d, "_NET_WM_STATE", False);
	e.format = 32;
	e.data.l[0] = !strcmp(argv[3], "add");
	e.data.l[1] = XInternAtom(d, argv[4], False);
	e.data.l[3] = 1;
	XSendEvent(d, DefaultRootWindow(d), False,
		SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&e);
	XSync(d, False);
	return 0;
}

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	if (!d || argc < 2)
		return 2;
	if (!strcmp(argv[1], "win") && argc >= 3)
		return win(d, argc, argv);
	if (!strcmp(argv[1], "state") && argc == 5)
		return state(d, argv);
	if (!strcmp(argv[1], "grabbed")) {
		int r = XGrabKeyboard(d, DefaultRootWindow(d), False,
			GrabModeAsync, GrabModeAsync, CurrentTime);
		puts(r == GrabSuccess ? "no" : "yes");
		XUngrabKeyboard(d, CurrentTime);
		XSync(d, False);
		return 0;
	}
	if (!strcmp(argv[1], "holdpointer") && argc == 3) {
		XGrabPointer(d, DefaultRootWindow(d), False, 0, GrabModeAsync,
			GrabModeAsync, None, None, CurrentTime);
		XSync(d, False);
		sleep(atoi(argv[2]));
		return 0;
	}
	if (!strcmp(argv[1], "rootchanged")) {
		XConfigureEvent e = { 0 };
		e.type = ConfigureNotify;
		e.event = e.window = DefaultRootWindow(d);
		e.width = DisplayWidth(d, DefaultScreen(d));
		e.height = DisplayHeight(d, DefaultScreen(d));
		XSendEvent(d, DefaultRootWindow(d), False, StructureNotifyMask,
			(XEvent *)&e);
		XSync(d, False);
		return 0;
	}
	return 2;
}

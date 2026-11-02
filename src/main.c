/*
 * mwm - a Motif-style window manager for X11
 *
 * Startup (ICCCM 2.8 manager selection, optional replacement of a running
 * window manager), the event loop, and orderly shutdown or restart.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/extensions/Xrandr.h>
#include "mwm.h"

struct wm wm;
Atom atoms[ATOM_COUNT];

static bool debug;
static volatile sig_atomic_t got_signal;
static Atom wm_selection;

void wm_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("mwm: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

void wm_debug(const char *fmt, ...)
{
	if (!debug)
		return;
	va_list ap;
	va_start(ap, fmt);
	fputs("mwm: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static void die(const char *msg)
{
	fprintf(stderr, "mwm: %s\n", msg);
	exit(1);
}

/* Windows disappear all the time; errors about them are expected. */
static int xerror(Display *dpy, XErrorEvent *e)
{
	if (debug) {
		char text[128];
		XGetErrorText(dpy, e->error_code, text, sizeof(text));
		wm_debug("X error: %s (request %d, resource 0x%lx)", text,
			e->request_code, e->resourceid);
	}
	return 0;
}

static int xerror_startup(Display *dpy, XErrorEvent *e)
{
	(void)dpy;
	if (e->request_code == X_ChangeWindowAttributes &&
			e->error_code == BadAccess)
		die("another window manager is already running");
	return 0;
}

static void on_signal(int sig)
{
	got_signal = sig;
}

/* ---- startup ------------------------------------------------------------ */

/* A real server timestamp: ICCCM forbids CurrentTime for selections. */
static Time server_time(void)
{
	XEvent ev;
	XSelectInput(wm.dpy, wm.check, PropertyChangeMask);
	XChangeProperty(wm.dpy, wm.check, XA_WM_CLASS, XA_STRING, 8,
		PropModeAppend, NULL, 0);
	XWindowEvent(wm.dpy, wm.check, PropertyChangeMask, &ev);
	XSelectInput(wm.dpy, wm.check, NoEventMask);
	return ev.xproperty.time;
}

/* ICCCM 2.8: own WM_Sn; with -r take it over from the running manager
 * and wait for it to let go of the screen. */
static void acquire_wm_selection(bool replace)
{
	char name[32];
	snprintf(name, sizeof(name), "WM_S%d", wm.screen);
	wm_selection = XInternAtom(wm.dpy, name, False);

	Window old = XGetSelectionOwner(wm.dpy, wm_selection);
	if (old != None) {
		if (!replace)
			die("another window manager is running (use -r to "
				"replace it)");
		XSelectInput(wm.dpy, old, StructureNotifyMask);
	}
	Time t = server_time();
	XSetSelectionOwner(wm.dpy, wm_selection, wm.check, t);
	if (XGetSelectionOwner(wm.dpy, wm_selection) != wm.check)
		die("could not acquire the window manager selection");

	if (old != None) {
		XEvent ev;
		bool gone = false;
		for (int i = 0; i < 100 && !gone; i++) {
			if (XCheckTypedWindowEvent(wm.dpy, old, DestroyNotify,
					&ev))
				gone = true;
			else
				nanosleep(&(struct timespec){ 0, 50000000 }, NULL);
		}
		if (!gone)
			wm_log("old window manager did not exit, continuing");
	}

	XClientMessageEvent ev = {
		.type = ClientMessage,
		.window = wm.root,
		.message_type = atoms[MANAGER],
		.format = 32,
		.data.l = { (long)t, (long)wm_selection, (long)wm.check, 0, 0 },
	};
	XSendEvent(wm.dpy, wm.root, False, StructureNotifyMask, (XEvent *)&ev);
}

static void setup(bool replace)
{
	wm.screen = DefaultScreen(wm.dpy);
	wm.root = RootWindow(wm.dpy, wm.screen);
	wm.visual = DefaultVisual(wm.dpy, wm.screen);
	wm.depth = DefaultDepth(wm.dpy, wm.screen);
	wm.cmap = DefaultColormap(wm.dpy, wm.screen);
	wm.gc = XCreateGC(wm.dpy, wm.root, 0, NULL);

	char *names[ATOM_COUNT] = {
#define X(id, name) name,
		ATOM_LIST(X)
#undef X
	};
	XInternAtoms(wm.dpy, names, ATOM_COUNT, False, atoms);

	/* Mapped off-screen so it can hold the focus when no client does. */
	XSetWindowAttributes ca = { .override_redirect = True };
	wm.check = XCreateWindow(wm.dpy, wm.root, -10, -10, 1, 1, 0, 0,
		InputOnly, CopyFromParent, CWOverrideRedirect, &ca);
	XMapWindow(wm.dpy, wm.check);
	acquire_wm_selection(replace);

	XSetErrorHandler(xerror_startup);
	XSelectInput(wm.dpy, wm.root, SubstructureRedirectMask |
		SubstructureNotifyMask | StructureNotifyMask |
		ButtonPressMask | PropertyChangeMask);
	XSync(wm.dpy, False);
	XSetErrorHandler(xerror);

	int err;
	wm.have_xrandr = XRRQueryExtension(wm.dpy, &wm.xrandr_event, &err);
	if (wm.have_xrandr)
		XRRSelectInput(wm.dpy, wm.root, RRScreenChangeNotifyMask |
			RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);
	monitors_update();

	config_load();
	theme_init();
	input_init();
	ewmh_init();

	XSetWindowBackground(wm.dpy, wm.root, theme.root);
	XClearWindow(wm.dpy, wm.root);
	XDefineCursor(wm.dpy, wm.root, wm.cursor[CUR_DEFAULT]);
	XSetInputFocus(wm.dpy, wm.check, RevertToPointerRoot, CurrentTime);
}

/* Adopt windows that exist already: after a restart, or when replacing
 * another window manager. Transients last, so their parents are known. */
static void scan(void)
{
	Window r, p, *kids = NULL;
	unsigned n = 0;
	if (!XQueryTree(wm.dpy, wm.root, &r, &p, &kids, &n))
		return;
	for (int pass = 0; pass < 2; pass++) {
		for (unsigned i = 0; i < n; i++) {
			XWindowAttributes wa;
			Window t;
			if (kids[i] == wm.check ||
					!XGetWindowAttributes(wm.dpy, kids[i], &wa) ||
					wa.override_redirect)
				continue;
			bool transient = XGetTransientForHint(wm.dpy, kids[i],
				&t);
			if (transient != (pass == 1))
				continue;
			unsigned long nstate;
			long *state = get_property(kids[i], atoms[WM_STATE],
				atoms[WM_STATE], &nstate);
			bool iconic = state && state[0] == IconicState;
			if (state)
				XFree(state);
			if (wa.map_state == IsViewable || iconic)
				client_manage(kids[i], true);
		}
	}
	if (kids)
		XFree(kids);
	client_focus_next(NULL);
}

/* ---- event loop --------------------------------------------------------- */

static void note_time(XEvent *ev)
{
	switch (ev->type) {
	case KeyPress:
	case KeyRelease:
		wm.time = ev->xkey.time;
		break;
	case ButtonPress:
	case ButtonRelease:
		wm.time = ev->xbutton.time;
		break;
	case MotionNotify:
		wm.time = ev->xmotion.time;
		break;
	case EnterNotify:
	case LeaveNotify:
		wm.time = ev->xcrossing.time;
		break;
	case PropertyNotify:
		wm.time = ev->xproperty.time;
		break;
	}
}

static void map_request(XMapRequestEvent *e)
{
	struct client *c = client_find_win(e->window);
	if (!c)
		client_manage(e->window, false);
	else if (c->minimized)
		client_activate(c); /* ICCCM: map from Iconic means Normal */
}

static void unmap_notify(XUnmapEvent *e)
{
	struct client *c = client_find_win(e->window);
	if (!c)
		return;
	if (e->send_event) {
		/* ICCCM 4.1.4: synthetic unmap withdraws an iconic window. */
		if (c->minimized)
			client_unmanage(c, false);
		return;
	}
	if (c->ignore_unmap > 0) {
		c->ignore_unmap--;
		return;
	}
	client_unmanage(c, false);
}

static void focus_in(XFocusChangeEvent *e)
{
	if (e->mode == NotifyGrab || e->mode == NotifyUngrab ||
			e->detail == NotifyPointer || e->detail == NotifyInferior)
		return;
	/* A client took the focus itself (globally active input model). */
	struct client *c = client_find_win(e->window);
	if (c && c != wm.focus && !c->minimized)
		client_set_active(c);
}

static void handle_event(XEvent *ev)
{
	note_time(ev);
	switch (ev->type) {
	case ButtonPress:
		handle_button_press(&ev->xbutton);
		break;
	case ButtonRelease:
		handle_button_release(&ev->xbutton);
		break;
	case MotionNotify:
		handle_motion(&ev->xmotion);
		break;
	case KeyPress:
		handle_key_press(&ev->xkey);
		break;
	case EnterNotify:
	case LeaveNotify:
		handle_crossing(&ev->xcrossing);
		break;
	case MapRequest:
		map_request(&ev->xmaprequest);
		break;
	case ConfigureRequest:
		client_configure_request(&ev->xconfigurerequest);
		break;
	case UnmapNotify:
		unmap_notify(&ev->xunmap);
		break;
	case DestroyNotify: {
		struct client *c = client_find_win(ev->xdestroywindow.window);
		if (c)
			client_unmanage(c, true);
		break;
	}
	case ReparentNotify: {
		/* Someone else took the window (e.g. an XEmbed host). */
		struct client *c = client_find_win(ev->xreparent.window);
		if (c && ev->xreparent.parent != c->frame) {
			XRemoveFromSaveSet(wm.dpy, c->win);
			client_unmanage(c, true);
		}
		break;
	}
	case PropertyNotify:
		client_property_notify(&ev->xproperty);
		break;
	case ClientMessage:
		ewmh_client_message(&ev->xclient);
		break;
	case FocusIn:
		focus_in(&ev->xfocus);
		break;
	case ConfigureNotify:
		if (ev->xconfigure.window == wm.root) {
			if (wm.have_xrandr)
				XRRUpdateConfiguration(ev);
			goto screen_changed;
		}
		break;
	case MappingNotify:
		XRefreshKeyboardMapping(&ev->xmapping);
		if (ev->xmapping.request == MappingKeyboard ||
				ev->xmapping.request == MappingModifier) {
			update_numlock();
			grab_keys();
			for (int i = 0; i < wm.n_clients; i++)
				client_grab_buttons(wm.clients[i],
					wm.clients[i] == wm.focus);
		}
		break;
	case SelectionClear:
		if (ev->xselectionclear.selection == wm_selection) {
			wm_log("replaced by another window manager");
			wm.running = false;
		}
		break;
	default:
		if (wm.have_xrandr && (ev->type == wm.xrandr_event +
				RRScreenChangeNotify || ev->type ==
				wm.xrandr_event + RRNotify)) {
			XRRUpdateConfiguration(ev);
			goto screen_changed;
		}
		break;
	}
	return;

screen_changed:
	monitors_update();
	ewmh_update_geometry();
	ewmh_update_workarea();
	for (int i = 0; i < wm.n_clients; i++) {
		struct client *c = wm.clients[i];
		if (c->fullscreen || c->snap != SNAP_NONE || c->maxed_v ||
				c->maxed_h) {
			client_apply_state(c);
		} else {
			struct rect b = client_box(c);
			struct rect m = monitor_usable_at(b.x + b.w / 2,
				b.y + b.h / 2);
			if (b.x >= m.x + m.w || b.y >= m.y + m.h ||
					b.x + b.w <= m.x || b.y + b.h <= m.y)
				client_move(c, m.x + 32, m.y + 32);
		}
	}
	icons_arrange();
}

static void run(void)
{
	int fd = ConnectionNumber(wm.dpy);
	XEvent ev;
	wm.running = true;
	while (wm.running) {
		while (wm.running && XPending(wm.dpy)) {
			XNextEvent(wm.dpy, &ev);
			handle_event(&ev);
		}
		if (got_signal) {
			if (got_signal == SIGHUP)
				wm.restart = true;
			wm.running = false;
		}
		if (!wm.running)
			break;
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (select(fd + 1, &fds, NULL, NULL, NULL) < 0 && errno != EINTR)
			die("select failed");
	}
}

/* ---- autostart ---------------------------------------------------------- */

static bool in_path(const char *prog)
{
	const char *path = getenv("PATH");
	if (!path)
		return false;
	char *dirs = strdup(path), *save = NULL;
	bool found = false;
	for (char *d = strtok_r(dirs, ":", &save); d && !found;
			d = strtok_r(NULL, ":", &save)) {
		char buf[4096];
		snprintf(buf, sizeof(buf), "%s/%s", d, prog);
		found = access(buf, X_OK) == 0;
	}
	free(dirs);
	return found;
}

static const char *pick_terminal(void)
{
	const char *env = getenv("MWM_TERMINAL");
	if (env && *env)
		return env;
	static const char *candidates[] = {
		"xterm", "uxterm", "x-terminal-emulator", "urxvt",
		"alacritty", "konsole", "kitty", "xfce4-terminal",
		"gnome-terminal",
	};
	for (size_t i = 0; i < LENGTH(candidates); i++) {
		if (in_path(candidates[i]))
			return candidates[i];
	}
	return "xterm";
}

static void run_autostart(const char *startup_cmd)
{
	if (startup_cmd) {
		spawn(startup_cmd);
		return;
	}
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[4096];
	if (cfg && *cfg)
		snprintf(path, sizeof(path), "%s/mwm/autostart", cfg);
	else if (home)
		snprintf(path, sizeof(path), "%s/.config/mwm/autostart", home);
	else
		return;
	if (access(path, X_OK) == 0)
		spawn(path);
}

static void usage(const char *argv0)
{
	printf("Usage: %s [-r] [-s startup-command] [-d]\n"
		"\n"
		"  -r       replace the running window manager\n"
		"  -s CMD   run CMD once the window manager is up\n"
		"  -d       debug logging\n"
		"\n"
		"Environment: MWM_TERMINAL, MWM_ACTIVE, MWM_INACTIVE, MWM_ROOT,\n"
		"MWM_FONT (fontconfig pattern, e.g. \"Sans:bold:size=10\").\n"
		"Without -s, ~/.config/mwm/autostart is run if executable.\n",
		argv0);
}

int main(int argc, char *argv[])
{
	const char *startup_cmd = NULL;
	bool replace = false;
	int opt;
	while ((opt = getopt(argc, argv, "rs:dh")) != -1) {
		switch (opt) {
		case 'r':
			replace = true;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		case 'd':
			debug = true;
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	setlocale(LC_CTYPE, "");
	if (!(wm.dpy = XOpenDisplay(NULL)))
		die("cannot open display");

	struct sigaction sa = { .sa_handler = on_signal };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	setup(replace);
	wm.terminal = pick_terminal();
	scan();
	XSync(wm.dpy, False);
	wm_log("running on %s, terminal: %s", DisplayString(wm.dpy),
		wm.terminal);

	/* A restart re-executes us; the session is already running then. */
	if (getenv("MWM_RESTARTED"))
		unsetenv("MWM_RESTARTED");
	else
		run_autostart(startup_cmd);

	run();

	menu_close();
	end_grab();
	client_release_all();
	XDeleteProperty(wm.dpy, wm.root, atoms[NET_SUPPORTING_WM_CHECK]);
	XDeleteProperty(wm.dpy, wm.root, atoms[NET_ACTIVE_WINDOW]);
	XDestroyWindow(wm.dpy, wm.check);
	XSync(wm.dpy, False);
	XCloseDisplay(wm.dpy);

	if (wm.restart) {
		setenv("MWM_RESTARTED", "1", 1);
		execvp(argv[0], argv);
		perror("mwm: restart failed");
		return 1;
	}
	return 0;
}

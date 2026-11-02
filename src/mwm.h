/*
 * mwm - a Motif-style window manager for X11
 *
 * Shared types. A classic reparenting window manager: every managed client
 * gets a frame window that carries the Motif decoration as its background
 * pixmap, so the X server repaints it on its own and nothing has to be
 * drawn on Expose. One global `wm` holds the display state.
 */
#ifndef MWM_H
#define MWM_H

#include <stdbool.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define DOUBLE_CLICK_MS 400
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void wm_log(const char *fmt, ...);
/* A current server timestamp (one round trip). */
Time server_time(void);
/* Allocation that exits on failure: a window manager without memory
 * cannot do anything useful. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
void wm_debug(const char *fmt, ...);

/* ---- atoms -------------------------------------------------------------- */

#define ATOM_LIST(X) \
	X(UTF8_STRING, "UTF8_STRING") \
	X(MANAGER, "MANAGER") \
	X(WM_PROTOCOLS, "WM_PROTOCOLS") \
	X(WM_DELETE_WINDOW, "WM_DELETE_WINDOW") \
	X(WM_TAKE_FOCUS, "WM_TAKE_FOCUS") \
	X(WM_STATE, "WM_STATE") \
	X(WM_CHANGE_STATE, "WM_CHANGE_STATE") \
	X(MOTIF_WM_HINTS, "_MOTIF_WM_HINTS") \
	X(MOTIF_WM_INFO, "_MOTIF_WM_INFO") \
	X(GTK_SHOW_WINDOW_MENU, "_GTK_SHOW_WINDOW_MENU") \
	X(NET_SUPPORTED, "_NET_SUPPORTED") \
	X(NET_SUPPORTING_WM_CHECK, "_NET_SUPPORTING_WM_CHECK") \
	X(NET_CLIENT_LIST, "_NET_CLIENT_LIST") \
	X(NET_CLIENT_LIST_STACKING, "_NET_CLIENT_LIST_STACKING") \
	X(NET_NUMBER_OF_DESKTOPS, "_NET_NUMBER_OF_DESKTOPS") \
	X(NET_DESKTOP_GEOMETRY, "_NET_DESKTOP_GEOMETRY") \
	X(NET_DESKTOP_VIEWPORT, "_NET_DESKTOP_VIEWPORT") \
	X(NET_CURRENT_DESKTOP, "_NET_CURRENT_DESKTOP") \
	X(NET_DESKTOP_NAMES, "_NET_DESKTOP_NAMES") \
	X(NET_ACTIVE_WINDOW, "_NET_ACTIVE_WINDOW") \
	X(NET_WORKAREA, "_NET_WORKAREA") \
	X(NET_CLOSE_WINDOW, "_NET_CLOSE_WINDOW") \
	X(NET_MOVERESIZE_WINDOW, "_NET_MOVERESIZE_WINDOW") \
	X(NET_WM_MOVERESIZE, "_NET_WM_MOVERESIZE") \
	X(NET_RESTACK_WINDOW, "_NET_RESTACK_WINDOW") \
	X(NET_REQUEST_FRAME_EXTENTS, "_NET_REQUEST_FRAME_EXTENTS") \
	X(NET_WM_NAME, "_NET_WM_NAME") \
	X(NET_WM_ICON_NAME, "_NET_WM_ICON_NAME") \
	X(NET_WM_DESKTOP, "_NET_WM_DESKTOP") \
	X(NET_WM_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE") \
	X(NET_WM_WINDOW_TYPE_DESKTOP, "_NET_WM_WINDOW_TYPE_DESKTOP") \
	X(NET_WM_WINDOW_TYPE_DOCK, "_NET_WM_WINDOW_TYPE_DOCK") \
	X(NET_WM_WINDOW_TYPE_TOOLBAR, "_NET_WM_WINDOW_TYPE_TOOLBAR") \
	X(NET_WM_WINDOW_TYPE_MENU, "_NET_WM_WINDOW_TYPE_MENU") \
	X(NET_WM_WINDOW_TYPE_UTILITY, "_NET_WM_WINDOW_TYPE_UTILITY") \
	X(NET_WM_WINDOW_TYPE_SPLASH, "_NET_WM_WINDOW_TYPE_SPLASH") \
	X(NET_WM_WINDOW_TYPE_DIALOG, "_NET_WM_WINDOW_TYPE_DIALOG") \
	X(NET_WM_WINDOW_TYPE_DROPDOWN_MENU, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU") \
	X(NET_WM_WINDOW_TYPE_POPUP_MENU, "_NET_WM_WINDOW_TYPE_POPUP_MENU") \
	X(NET_WM_WINDOW_TYPE_TOOLTIP, "_NET_WM_WINDOW_TYPE_TOOLTIP") \
	X(NET_WM_WINDOW_TYPE_NOTIFICATION, "_NET_WM_WINDOW_TYPE_NOTIFICATION") \
	X(NET_WM_WINDOW_TYPE_COMBO, "_NET_WM_WINDOW_TYPE_COMBO") \
	X(NET_WM_WINDOW_TYPE_DND, "_NET_WM_WINDOW_TYPE_DND") \
	X(NET_WM_WINDOW_TYPE_NORMAL, "_NET_WM_WINDOW_TYPE_NORMAL") \
	X(NET_WM_STATE, "_NET_WM_STATE") \
	X(NET_WM_STATE_MODAL, "_NET_WM_STATE_MODAL") \
	X(NET_WM_STATE_STICKY, "_NET_WM_STATE_STICKY") \
	X(NET_WM_STATE_MAXIMIZED_VERT, "_NET_WM_STATE_MAXIMIZED_VERT") \
	X(NET_WM_STATE_MAXIMIZED_HORZ, "_NET_WM_STATE_MAXIMIZED_HORZ") \
	X(NET_WM_STATE_SKIP_TASKBAR, "_NET_WM_STATE_SKIP_TASKBAR") \
	X(NET_WM_STATE_SKIP_PAGER, "_NET_WM_STATE_SKIP_PAGER") \
	X(NET_WM_STATE_HIDDEN, "_NET_WM_STATE_HIDDEN") \
	X(NET_WM_STATE_FULLSCREEN, "_NET_WM_STATE_FULLSCREEN") \
	X(NET_WM_STATE_ABOVE, "_NET_WM_STATE_ABOVE") \
	X(NET_WM_STATE_BELOW, "_NET_WM_STATE_BELOW") \
	X(NET_WM_STATE_DEMANDS_ATTENTION, "_NET_WM_STATE_DEMANDS_ATTENTION") \
	X(NET_WM_STATE_FOCUSED, "_NET_WM_STATE_FOCUSED") \
	X(NET_WM_ALLOWED_ACTIONS, "_NET_WM_ALLOWED_ACTIONS") \
	X(NET_WM_ACTION_MOVE, "_NET_WM_ACTION_MOVE") \
	X(NET_WM_ACTION_RESIZE, "_NET_WM_ACTION_RESIZE") \
	X(NET_WM_ACTION_MINIMIZE, "_NET_WM_ACTION_MINIMIZE") \
	X(NET_WM_ACTION_MAXIMIZE_HORZ, "_NET_WM_ACTION_MAXIMIZE_HORZ") \
	X(NET_WM_ACTION_MAXIMIZE_VERT, "_NET_WM_ACTION_MAXIMIZE_VERT") \
	X(NET_WM_ACTION_FULLSCREEN, "_NET_WM_ACTION_FULLSCREEN") \
	X(NET_WM_ACTION_CLOSE, "_NET_WM_ACTION_CLOSE") \
	X(NET_WM_ACTION_ABOVE, "_NET_WM_ACTION_ABOVE") \
	X(NET_WM_ACTION_BELOW, "_NET_WM_ACTION_BELOW") \
	X(NET_WM_STRUT, "_NET_WM_STRUT") \
	X(NET_WM_STRUT_PARTIAL, "_NET_WM_STRUT_PARTIAL") \
	X(NET_WM_ICON, "_NET_WM_ICON") \
	X(NET_WM_PID, "_NET_WM_PID") \
	X(NET_WM_USER_TIME, "_NET_WM_USER_TIME") \
	X(NET_WM_USER_TIME_WINDOW, "_NET_WM_USER_TIME_WINDOW") \
	X(NET_FRAME_EXTENTS, "_NET_FRAME_EXTENTS") \
	X(NET_WM_PING, "_NET_WM_PING") \
	X(NET_WM_FULL_PLACEMENT, "_NET_WM_FULL_PLACEMENT")

enum atom_id {
#define X(id, name) id,
	ATOM_LIST(X)
#undef X
	ATOM_COUNT
};

extern Atom atoms[ATOM_COUNT];

/* ---- theme -------------------------------------------------------------- */

/* A Motif "3D colour set": background plus the two derived shadows. */
struct shade {
	uint32_t bg_rgb;
	unsigned long bg, top, bottom;
	XftColor fg;
};

struct theme {
	struct shade active;   /* focused frame */
	struct shade inactive; /* other frames, menus, icons */
	XftColor dim;          /* insensitive menu entries */
	unsigned long root;    /* desktop background */
	XftFont *font;
	int border;            /* resize border width */
	int title_height;
	int bevel;             /* shadow thickness */
};

extern struct theme theme;

/* Off-screen drawing target; committed as a window's background pixmap. */
struct canvas {
	Pixmap pm;
	XftDraw *xft;
	int w, h;
};

void theme_init(void);
void canvas_begin(struct canvas *cv, int w, int h);
void canvas_commit(struct canvas *cv, Window win);
void fill_rect(struct canvas *cv, unsigned long pixel, int x, int y,
	int w, int h);
/* Raised (in == false) or sunken (in == true) Motif bevel. */
void draw_bevel(struct canvas *cv, int x, int y, int w, int h, int t,
	const struct shade *s, bool in);
/* Etched-in groove line, as used for separators and resize corners. */
void draw_groove_h(struct canvas *cv, int x, int y, int w,
	const struct shade *s);
void draw_groove_v(struct canvas *cv, int x, int y, int h,
	const struct shade *s);
int text_width(const char *text, int len);
void draw_text(struct canvas *cv, const char *text, XftColor *fg,
	int x, int y, int w, int h, bool center);
unsigned long truecolor_pixel(uint32_t rgb);
uint32_t color_mix(uint32_t rgb, uint32_t to, float f);
/* Shared shade for a background; fg < 0 picks black or white. */
const struct shade *shade_get(uint32_t bg, int64_t fg);

/* ---- clients ------------------------------------------------------------ */

struct rect {
	int x, y, w, h;
};

/* Frame regions, used for hit testing, cursor shapes and button state. */
enum frame_part {
	PART_NONE = 0,
	PART_CLIENT,
	PART_TITLE,
	PART_MENU_BUTTON,
	PART_MINIMIZE_BUTTON,
	PART_MAXIMIZE_BUTTON,
	PART_RESIZE, /* see resize edges */
};

enum {
	EDGE_TOP = 1 << 0,
	EDGE_BOTTOM = 1 << 1,
	EDGE_LEFT = 1 << 2,
	EDGE_RIGHT = 1 << 3,
};

/* Decoration and function bits use the _MOTIF_WM_HINTS values. */
enum {
	DECOR_BORDER = 1 << 1,
	DECOR_RESIZEH = 1 << 2,
	DECOR_TITLE = 1 << 3,
	DECOR_MENU = 1 << 4,
	DECOR_MINIMIZE = 1 << 5,
	DECOR_MAXIMIZE = 1 << 6,
	DECOR_ALL = 0x7e,
};

enum {
	FUNC_RESIZE = 1 << 1,
	FUNC_MOVE = 1 << 2,
	FUNC_MINIMIZE = 1 << 3,
	FUNC_MAXIMIZE = 1 << 4,
	FUNC_CLOSE = 1 << 5,
	FUNC_ALL = 0x3e,
};

enum window_type {
	WT_NORMAL,
	WT_DIALOG,
	WT_UTILITY, /* also toolbars and torn-off menus */
	WT_SPLASH,
	WT_DOCK,
	WT_DESKTOP,
	WT_NOTIFICATION, /* and other popups that got mapped managed */
};

/* Snap positions; SNAP_MAXIMIZE is only a target, a client maximized
 * that way has SNAP_NONE and both maximize flags. */
enum snap {
	SNAP_NONE,
	SNAP_LEFT,
	SNAP_RIGHT,
	SNAP_TOP_LEFT,
	SNAP_TOP_RIGHT,
	SNAP_BOTTOM_LEFT,
	SNAP_BOTTOM_RIGHT,
	SNAP_MAXIMIZE,
};

/* Stacking layers, bottom to top. */
enum layer {
	LAYER_DESKTOP,
	LAYER_ICONS,
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_FULLSCREEN,
	LAYER_COUNT,
};

struct client {
	Window win;      /* the client's top-level window */
	Window frame;    /* our parent window carrying the decoration */
	Window icon_win; /* shown while minimized, created on demand */
	unsigned long serial; /* management order for _NET_CLIENT_LIST */

	/* Frame geometry: x/y is the outer top-left corner in root
	 * coordinates, w/h the client size. */
	int x, y, w, h;
	int old_bw; /* border width to restore on unmanage */
	Colormap cmap;

	char *title;
	char *icon_name;

	/* Frame colours, from mwmrc by WM_CLASS or the theme defaults. */
	const struct shade *active_shade;
	const struct shade *inactive_shade;

	/* WM_NORMAL_HINTS */
	int base_w, base_h, min_w, min_h, max_w, max_h, inc_w, inc_h;
	float min_aspect, max_aspect;
	int gravity;
	long size_flags;
	bool fixed;

	/* WM_HINTS / WM_PROTOCOLS */
	bool input;
	bool take_focus;
	bool delete_window;
	bool ping;
	bool urgent;
	Window transient_for;

	enum window_type type;
	long mwm_flags, mwm_funcs, mwm_decor; /* raw _MOTIF_WM_HINTS */
	unsigned decor; /* effective decorations */
	unsigned funcs; /* effective functions */

	bool minimized;
	bool hidden_with_parent; /* minimized along with its transient_for */
	bool maxed_v, maxed_h;
	bool fullscreen;
	enum snap snap; /* halves also set maxed_v, as EWMH sees them */
	bool above, below, sticky, skip_taskbar, skip_pager, modal;
	struct rect saved; /* geometry of the unmaximized axes */

	long strut[12]; /* _NET_WM_STRUT_PARTIAL layout */
	bool has_strut;

	int ignore_unmap; /* UnmapNotify events we caused ourselves */
	Cursor cursor;    /* shape currently set on the frame */

	int icon_x, icon_y;
	bool icon_placed;

	enum frame_part pressed; /* button held down, for drawing */
	bool pressed_inside;

	uint64_t ping_sent; /* monotonic ms, 0 when no ping is pending */
};

/* ---- menus -------------------------------------------------------------- */

enum action {
	ACT_NONE,
	ACT_RESTORE,
	ACT_MOVE,
	ACT_SIZE,
	ACT_MINIMIZE,
	ACT_MAXIMIZE,
	ACT_LOWER,
	ACT_CLOSE,
	ACT_NEW_WINDOW,
	ACT_SHUFFLE_UP,
	ACT_SHUFFLE_DOWN,
	ACT_REFRESH,
	ACT_RESTART_CONFIRM,
	ACT_RESTART,
	ACT_QUIT_CONFIRM,
	ACT_QUIT,
	ACT_WINDOW_MENU,
	ACT_TOGGLE_MAXIMIZE,
	ACT_TOGGLE_FULLSCREEN,
	ACT_SNAP_LEFT,
	ACT_SNAP_RIGHT,
	ACT_SNAP_UP,
	ACT_SNAP_DOWN,
};

struct menu_item {
	const char *label; /* NULL means separator */
	char mnemonic;     /* underlined letter, 0 for none */
	const char *accel; /* right-aligned accelerator text */
	enum action action;
	bool sensitive;
};

#define MENU_MAX_ITEMS 16

struct menu {
	Window win;
	bool open;
	const char *title;
	struct menu_item items[MENU_MAX_ITEMS];
	int n_items;
	int armed;             /* highlighted item, -1 for none */
	struct client *target; /* window menu: the window it acts on */
	int x, y, width, height;
	int item_y[MENU_MAX_ITEMS], item_h[MENU_MAX_ITEMS];
	Time opened;
	int open_x, open_y;  /* pointer when posted */
	bool moved;          /* pointer moved since then */
	bool pressed;        /* a button went down in the menu since then */
	bool from_button;    /* posted by the frame's menu button */
};

/* ---- global state ------------------------------------------------------- */

enum grab_mode {
	GRAB_NONE,
	GRAB_MOVE,
	GRAB_RESIZE,
	GRAB_BUTTON, /* a frame button is held down */
	GRAB_MENU,   /* a menu has the pointer grab */
};

enum cursor_id {
	CUR_DEFAULT,
	CUR_MOVE,
	CUR_TOP,
	CUR_BOTTOM,
	CUR_LEFT,
	CUR_RIGHT,
	CUR_TOP_LEFT,
	CUR_TOP_RIGHT,
	CUR_BOTTOM_LEFT,
	CUR_BOTTOM_RIGHT,
	CUR_COUNT,
};

#define MAX_MONITORS 16

struct wm {
	Display *dpy;
	int screen;
	Window root;
	Visual *visual;
	int depth;
	Colormap cmap;
	GC gc;
	int sw, sh; /* root size */

	/* _NET_SUPPORTING_WM_CHECK window, owner of WM_Sn and the place
	 * keyboard focus goes when no client has it. */
	Window check;

	/* Managed clients in stacking order, index 0 is the top. */
	struct client **clients;
	int n_clients, cap_clients;
	struct client *focus;
	unsigned long next_serial;

	struct rect mon[MAX_MONITORS];
	int n_mon;
	bool have_xrandr;
	int xrandr_event;

	Cursor cursor[CUR_COUNT];
	unsigned int numlock;
	Time time; /* timestamp of the last event that carried one */

	enum grab_mode grab;
	struct client *grabbed;
	int grab_x, grab_y;   /* pointer offset into the grabbed frame */
	struct rect grab_box; /* frame box at grab start (resize) */
	struct rect grab_orig; /* client geometry at grab start, for Escape */
	unsigned grab_edges;
	bool grab_until_click; /* move/size started from menu or keyboard */
	unsigned grab_button;  /* GRAB_BUTTON: the button held down */
	int grab_px, grab_py;  /* pointer at grab start */
	int grab_last_x, grab_last_y;
	bool grab_detached;    /* moved past the drag threshold */
	enum snap grab_zone;   /* snap target under the pointer */
	enum snap grab_orig_snap;
	bool grab_orig_max_v, grab_orig_max_h;

	/* Double-click detection */
	Time last_click;
	struct client *last_click_client;
	enum frame_part last_click_part;

	struct menu menu;
	Window feedback;

	int cascade;
	const char *terminal;
	bool running;
	bool restart;
};

extern struct wm wm;

/* ---- client.c ----------------------------------------------------------- */

struct client *client_find(Window w);
struct client *client_find_win(Window w);
void client_manage(Window w, bool startup);
void client_unmanage(struct client *c, bool destroyed);
void client_release_all(void);
void client_configure(struct client *c);
void client_move(struct client *c, int x, int y);
void client_send_configure(struct client *c);
void client_apply_size_hints(struct client *c, int *w, int *h, bool incr);
void client_focus(struct client *c);
void client_set_active(struct client *c);
void client_activate(struct client *c);
void client_focus_next(struct client *except);
void client_raise(struct client *c);
void client_lower(struct client *c);
void client_close(struct client *c);
void client_minimize(struct client *c);
void client_restore(struct client *c);
void client_set_maximized(struct client *c, bool v, bool h);
void client_set_fullscreen(struct client *c, bool fs);
void client_snap(struct client *c, enum snap s, int px, int py);
void client_apply_state(struct client *c);
void client_cycle(bool backwards);
void client_grab_buttons(struct client *c, bool focused);
void client_update_title(struct client *c);
void client_update_icon_name(struct client *c);
void client_update_size_hints(struct client *c);
void client_update_wm_hints(struct client *c);
void client_update_protocols(struct client *c);
void client_update_transient(struct client *c);
void client_update_type(struct client *c);
void client_update_motif_hints(struct client *c);
void client_update_decorations(struct client *c);
void client_update_strut(struct client *c);
void client_redecorate(struct client *c);
void client_configure_request(XConfigureRequestEvent *e);
void client_property_notify(XPropertyEvent *e);
void client_set_wm_state(struct client *c, long state);
void restack(void);
void monitors_update(void);
struct rect monitor_rect_at(int x, int y);
struct rect monitor_usable_at(int x, int y);
struct rect client_box(struct client *c);

/* ---- config.c ----------------------------------------------------------- */

void config_load(void);
const char *config_get(const char *resource);
bool config_color(const char *value, uint32_t *rgb);
void config_client_shades(struct client *c);

/* ---- ewmh.c ------------------------------------------------------------- */

void ewmh_init(void);
void ewmh_update_client_list(void);
void ewmh_update_stacking(const Window *top_first, int n);
void ewmh_update_workarea(void);
void ewmh_update_geometry(void);
void ewmh_set_active(struct client *c);
void ewmh_set_state(struct client *c);
void ewmh_set_allowed_actions(struct client *c);
void ewmh_set_frame_extents(struct client *c);
void ewmh_read_initial_state(struct client *c);
void ewmh_client_message(XClientMessageEvent *e);
void *get_property(Window w, Atom prop, Atom type, unsigned long *n);
bool get_cardinal(Window w, Atom prop, long *out);
Window get_window_prop(Window w, Atom prop);
char *get_text_property(Window w, Atom prop);

/* ---- frame.c ------------------------------------------------------------ */

int frame_border(struct client *c);
int frame_title(struct client *c);
int frame_extra_w(struct client *c);
int frame_extra_h(struct client *c);
void frame_render(struct client *c);
enum frame_part frame_hit(struct client *c, int fx, int fy, unsigned *edges);
void icon_render(struct client *c);
void icons_arrange(void);
void feedback_show(const char *text);
void feedback_hide(void);
#define ICON_W 72
#define ICON_H 72

/* ---- snap.c ------------------------------------------------------------- */

struct rect snap_rect(enum snap s, struct rect area);
const char *snap_name(enum snap s);
enum snap snap_zone_at(struct client *c, int px, int py);
void snap_preview(enum snap s, int px, int py);
void snap_step(struct client *c, enum action dir);

/* ---- menu.c ------------------------------------------------------------- */

void menu_open_window(struct client *c, int x, int y);
void menu_open_root(int x, int y);
void menu_close(void);
void menu_motion(int x, int y);
void menu_button(int x, int y, bool pressed, Time time);
void menu_key(KeySym sym);
void run_action(enum action action, struct client *c);

/* ---- input.c ------------------------------------------------------------ */

void input_init(void);
void grab_keys(void);
void update_numlock(void);
unsigned clean_mask(unsigned state);
void handle_button_press(XButtonEvent *e);
void handle_button_release(XButtonEvent *e);
void handle_motion(XMotionEvent *e);
void handle_key_press(XKeyEvent *e);
void handle_crossing(XCrossingEvent *e);
/* px/py: root position of the pointer when the grab was asked for. */
void begin_grab(struct client *c, enum grab_mode mode, unsigned edges,
	bool until_click, int px, int py);
void end_grab(void);
void spawn(const char *cmd);
Cursor edge_cursor(unsigned edges);

#endif

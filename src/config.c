/*
 * ~/.config/mwm/mwmrc: Motif-style resource lines for colours and font.
 *
 *   Mwm*activeBackground:          #7387ad    focused frames
 *   Mwm*background:                #aeb2c3    other frames, menus, icons
 *   Mwm*activeForeground / foreground         title text (default: auto)
 *   Mwm*fontList:                  Sans:bold:size=10
 *   Mwm*rootMenuItem:              Files: mfm     in the root menu, repeatable
 *   Mwm*runDialog:                 dmenu_run      the middle button on the desktop
 *   Mwm*screenSaver:               xset dpms force off     Super+Esc
 *   Mwm*XTerm*activeBackground:    #8a0303    per client, by WM_CLASS
 *   Mwm*firefox*activeBackground:  gold
 *   Mwm*DoNotTile:                 Navigator      Firefox floats in tiling mode
 *   Mwm*TileOnly:                  XTerm, mfm     only these tile
 *
 * The client part matches the WM_CLASS instance or class name (see
 * `xprop WM_CLASS`), ignoring case. Values are anything XParseColor takes:
 * #rrggbb, colour names, rgb:rr/gg/bb. Lines starting with ! are
 * comments, as in X resource files; there are no comments after a
 * value. Later lines win.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "mwm.h"

/* An unfocused client with its own colour shows this much of the
 * global inactive colour mixed in, so it reads as a paler version. */
#define PALE 0.55f

struct rule {
	char *client; /* NULL for global lines */
	char *resource;
	char *value;
	int line;
};

static struct rule *rules;
static int n_rules;
static char path[4096];

/* DoNotTile or TileOnly, whichever comes last, with its programs. */
static enum { FILTER_NONE, FILTER_DENY, FILTER_ONLY } tile_filter;
static char **tile_names;
static int n_tile_names;

/* The programs of Mwm*rootMenuItem; unlike other resources, every line counts. */
static struct {
	char *label, *command;
} root_items[CONFIG_MAX_ROOT_ITEMS];
static int n_root_items;

static char *trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = '\0';
	return s;
}

static void add_rule(char *key, char *value, int line)
{
	/* "Mwm*XTerm*activeBackground", '.' and '*' both separate. */
	char *parts[4];
	int n = 0;
	for (char *tok = strtok(key, "*."); tok && n < 4;
			tok = strtok(NULL, "*."))
		parts[n++] = tok;
	if (n < 2 || n > 3 || strcasecmp(parts[0], "mwm") != 0) {
		wm_log("%s:%d: expected Mwm*resource or Mwm*client*resource",
			path, line);
		return;
	}
	rules = xrealloc(rules, sizeof(*rules) * (n_rules + 1));
	rules[n_rules++] = (struct rule){
		.client = n == 3 ? xstrdup(parts[1]) : NULL,
		.resource = xstrdup(parts[n - 1]),
		.value = xstrdup(value),
		.line = line,
	};
}

/* The last global DoNotTile or TileOnly line decides, and an empty
 * one switches the filter off again. Programs are separated by commas;
 * WM_CLASS names may contain spaces. */
static void load_tile_filter(void)
{
	const struct rule *r = NULL;
	for (int i = 0; i < n_rules; i++) {
		if (!rules[i].client &&
				(!strcasecmp(rules[i].resource, "DoNotTile") ||
				!strcasecmp(rules[i].resource, "TileOnly")))
			r = &rules[i];
	}
	if (!r)
		return;
	char *list = xstrdup(r->value);
	for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
		char *name = trim(tok);
		if (!*name)
			continue;
		tile_names = xrealloc(tile_names,
			sizeof(*tile_names) * (n_tile_names + 1));
		tile_names[n_tile_names++] = xstrdup(name);
	}
	free(list);
	if (n_tile_names)
		tile_filter = strcasecmp(r->resource, "TileOnly") ?
			FILTER_DENY : FILTER_ONLY;
	wm_debug("%s:%d: %s with %d programs", path, r->line, r->resource,
		n_tile_names);
}

static void load_root_items(void)
{
	for (int i = 0; i < n_rules; i++) {
		if (rules[i].client || strcasecmp(rules[i].resource, "rootMenuItem"))
			continue;
		char *value = xstrdup(rules[i].value);
		char *colon = strchr(value, ':');
		char *command = colon ? trim(colon + 1) : NULL;
		if (colon)
			*colon = '\0';
		char *label = trim(value);
		if (!*label || !command || !*command) {
			wm_log("%s:%d: expected Mwm*rootMenuItem: label: command",
				path, rules[i].line);
		} else if (n_root_items == CONFIG_MAX_ROOT_ITEMS) {
			wm_log("%s:%d: only the first %d of Mwm*rootMenuItem are used",
				path, rules[i].line, CONFIG_MAX_ROOT_ITEMS);
		} else {
			root_items[n_root_items].label = xstrdup(label);
			root_items[n_root_items++].command = xstrdup(command);
		}
		free(value);
	}
}

int config_root_items(void)
{
	return n_root_items;
}

void config_root_item(int i, const char **label, const char **command)
{
	*label = root_items[i].label;
	*command = root_items[i].command;
}

void config_load(void)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg)
		snprintf(path, sizeof(path), "%s/mwm/mwmrc", cfg);
	else if (home)
		snprintf(path, sizeof(path), "%s/.config/mwm/mwmrc", home);
	else
		return;
	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char *buf = NULL;
	size_t cap = 0;
	int line = 0;
	while (getline(&buf, &cap, f) != -1) {
		line++;
		char *s = trim(buf);
		if (!*s || *s == '!')
			continue;
		char *colon = strchr(s, ':');
		if (!colon) {
			wm_log("%s:%d: missing ':'", path, line);
			continue;
		}
		*colon = '\0';
		add_rule(trim(s), trim(colon + 1), line);
	}
	free(buf);
	fclose(f);
	wm_debug("%s: %d rules", path, n_rules);
	load_tile_filter();
	load_root_items();
}

/* Global value of a resource, NULL when unset. */
const char *config_get(const char *resource)
{
	const char *v = NULL;
	for (int i = 0; i < n_rules; i++) {
		if (!rules[i].client &&
				strcasecmp(rules[i].resource, resource) == 0)
			v = rules[i].value;
	}
	return v;
}

static const struct rule *client_rule(const char *name, const char *class,
	const char *resource)
{
	const struct rule *r = NULL;
	for (int i = 0; i < n_rules; i++) {
		const char *c = rules[i].client;
		if (c && strcasecmp(rules[i].resource, resource) == 0 &&
				((name && strcasecmp(c, name) == 0) ||
				(class && strcasecmp(c, class) == 0)))
			r = &rules[i];
	}
	return r;
}

bool config_color(const char *value, uint32_t *rgb)
{
	XColor xc;
	if (!value || !XParseColor(wm.dpy, wm.cmap, value, &xc))
		return false;
	*rgb = (uint32_t)(xc.red >> 8) << 16 | (uint32_t)(xc.green >> 8) << 8 |
		(xc.blue >> 8);
	return true;
}

/* 1 or 0, or -1 when s is unset or no boolean; name is for the log. */
int config_bool(const char *name, const char *s)
{
	static const char *warned; /* log each bad value once, not per drag */
	if (!s || !*s)
		return -1;
	if (!strcasecmp(s, "true") || !strcasecmp(s, "on") ||
			!strcasecmp(s, "yes") || !strcmp(s, "1"))
		return 1;
	if (!strcasecmp(s, "false") || !strcasecmp(s, "off") ||
			!strcasecmp(s, "no") || !strcmp(s, "0"))
		return 0;
	if (s != warned) {
		wm_log("ignoring %s value %s, not a boolean", name, s);
		warned = s;
	}
	return -1;
}

static bool rule_color(const struct rule *r, uint32_t *rgb)
{
	if (!r)
		return false;
	if (config_color(r->value, rgb))
		return true;
	wm_log("%s:%d: unknown colour \"%s\"", path, r->line, r->value);
	return false;
}

/* Pick the frame colours for a client from its WM_CLASS. */
void config_client_shades(struct client *c)
{
	c->active_shade = &theme.active;
	c->inactive_shade = &theme.inactive;
	if (!n_rules)
		return;

	XClassHint ch = { 0 };
	if (!XGetClassHint(wm.dpy, c->win, &ch))
		return;
	uint32_t abg, afg, ibg, ifg;
	bool has_abg = rule_color(client_rule(ch.res_name, ch.res_class,
		"activeBackground"), &abg);
	bool has_afg = rule_color(client_rule(ch.res_name, ch.res_class,
		"activeForeground"), &afg);
	bool has_ibg = rule_color(client_rule(ch.res_name, ch.res_class,
		"background"), &ibg);
	bool has_ifg = rule_color(client_rule(ch.res_name, ch.res_class,
		"foreground"), &ifg);
	if (ch.res_name)
		XFree(ch.res_name);
	if (ch.res_class)
		XFree(ch.res_class);

	if (has_abg || has_afg) {
		if (!has_abg)
			abg = theme.active.bg_rgb;
		/* Title text the program does not set itself comes from
		 * the global entry, if there is one. */
		c->active_shade = shade_get(abg, has_afg ? (int64_t)afg :
			theme.active_fg);
	}
	if (has_abg || has_ibg || has_ifg) {
		/* Unfocused: a paler version of the client's own colour. */
		if (!has_ibg)
			ibg = has_abg ? color_mix(abg, theme.inactive.bg_rgb,
				PALE) : theme.inactive.bg_rgb;
		c->inactive_shade = shade_get(ibg, has_ifg ? (int64_t)ifg :
			theme.inactive_fg);
	}
}

/* Whether the tiling layout may take c, from its WM_CLASS and the
 * DoNotTile or TileOnly list. */
void config_client_tiling(struct client *c)
{
	c->no_tile = false;
	if (tile_filter == FILTER_NONE)
		return;
	XClassHint ch = { 0 };
	bool listed = false;
	if (XGetClassHint(wm.dpy, c->win, &ch)) {
		for (int i = 0; i < n_tile_names && !listed; i++)
			listed = (ch.res_name &&
				!strcasecmp(tile_names[i], ch.res_name)) ||
				(ch.res_class &&
				!strcasecmp(tile_names[i], ch.res_class));
		if (ch.res_name)
			XFree(ch.res_name);
		if (ch.res_class)
			XFree(ch.res_class);
	}
	c->no_tile = tile_filter == FILTER_DENY ? listed : !listed;
}

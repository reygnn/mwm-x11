/*
 * ~/.config/mwm/mwmrc: Motif-style resource lines for colours and font.
 *
 *   Mwm*activeBackground:          #7387ad    focused frames
 *   Mwm*background:                #aeb2c3    other frames, menus, icons
 *   Mwm*activeForeground / foreground         title text (default: auto)
 *   Mwm*fontList:                  Sans:bold:size=10
 *   Mwm*XTerm*activeBackground:    #8a0303    per client, by WM_CLASS
 *   Mwm*firefox*activeBackground:  gold
 *
 * The client part matches the WM_CLASS instance or class name (see
 * `xprop WM_CLASS`), ignoring case. Values are anything XParseColor takes:
 * #rrggbb, colour names, rgb:rr/gg/bb. Lines starting with ! or # are
 * comments; later lines win.
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
	rules = realloc(rules, sizeof(*rules) * (n_rules + 1));
	rules[n_rules++] = (struct rule){
		.client = n == 3 ? strdup(parts[1]) : NULL,
		.resource = strdup(parts[n - 1]),
		.value = strdup(value),
		.line = line,
	};
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

	char buf[1024];
	int line = 0;
	while (fgets(buf, sizeof(buf), f)) {
		line++;
		char *s = trim(buf);
		if (!*s || *s == '!' || *s == '#')
			continue;
		char *colon = strchr(s, ':');
		if (!colon) {
			wm_log("%s:%d: missing ':'", path, line);
			continue;
		}
		*colon = '\0';
		add_rule(trim(s), trim(colon + 1), line);
	}
	fclose(f);
	wm_debug("%s: %d rules", path, n_rules);
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
		c->active_shade = shade_get(abg, has_afg ? (int64_t)afg : -1);
	}
	if (has_abg || has_ibg || has_ifg) {
		/* Unfocused: a paler version of the client's own colour. */
		if (!has_ibg)
			ibg = has_abg ? color_mix(abg, theme.inactive.bg_rgb,
				PALE) : theme.inactive.bg_rgb;
		c->inactive_shade = shade_get(ibg, has_ifg ? (int64_t)ifg : -1);
	}
}

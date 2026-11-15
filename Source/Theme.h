#pragma once

/*
 * The ImplusOS desktop theme: one accent colour on a fixed light or dark
 * neutral palette. The window manager, the login screen and every UI.h
 * application draw with the same theme_t, so they agree without each
 * keeping a palette of its own.
 *
 * The theme files (the shipped windowmanager/Resource/Themes/Default.theme,
 * then /var/System/shell.theme on top of it) hold the two keys the palette
 * is built from:
 *
 *     accent = #6750A4
 *     mode   = light        ; or dark
 *
 * Colour format throughout: 0xAARRGGBB.
 */

#include <stdbool.h>
#include <stdint.h>

#define THEME_ACCENT_DEFAULT 0xFF6750A4u

/* Font sizes, in pixels. */
#define THEME_FONT_SMALL 12.0f   /* captions, status lines, units */
#define THEME_FONT_BODY  14.0f   /* running text, buttons         */
#define THEME_FONT_LARGE 16.0f   /* list rows, section titles     */
#define THEME_FONT_TITLE 22.0f   /* one per window at most        */

/* Corner radii, in pixels. */
#define THEME_RADIUS_SMALL  4
#define THEME_RADIUS        8
#define THEME_RADIUS_LARGE 16

/* Opacity (percent) of `text` laid over something the pointer is on,
 * and of content that is disabled. */
#define THEME_HOVER_PCT     8u
#define THEME_DISABLED_PCT 38u

typedef struct {
    bool     dark;
    uint32_t accent;        /* primary buttons, focus, selection marks */
    uint32_t on_accent;     /* text and icons on `accent`              */
    uint32_t accent_soft;   /* selected rows, highlighted chips        */
    uint32_t bg;            /* behind everything (window, desktop)     */
    uint32_t surface;       /* lists, cards, menus, dialogs            */
    uint32_t surface_alt;   /* toolbars, text fields, normal buttons   */
    uint32_t border;        /* dividers and outlines                   */
    uint32_t text;
    uint32_t text_dim;      /* secondary text, captions                */
    uint32_t danger;        /* errors, destructive actions             */
    uint32_t on_danger;
} theme_t;

/* Build the palette for `accent` (alpha is ignored) in light or dark. */
void theme_init(theme_t *theme, uint32_t accent, bool dark);

/* Read `accent` and `mode` from a theme file. Keys that are missing leave
 * the out values untouched. Returns false if the file cannot be read. */
bool theme_read_file(const char *path, uint32_t *accent, bool *dark);

/* The accent and mode the desktop is using now (defaults when no theme
 * file can be read). Either pointer may be NULL. */
void theme_get_desktop(uint32_t *accent, bool *dark);

/* theme_init() with the desktop's accent and mode. */
void theme_load_desktop(theme_t *theme);

/* `#RRGGBB`, `#AARRGGBB`, `0x...` or bare hex; `fallback` if malformed. */
uint32_t theme_parse_color(const char *text, uint32_t fallback);

/* `b` laid over `a` at `percent` (0..100); the result is opaque. */
uint32_t color_mix(uint32_t a, uint32_t b, uint32_t percent);

/* `argb` with its alpha set to `percent` of opaque. */
uint32_t color_alpha(uint32_t argb, uint32_t percent);

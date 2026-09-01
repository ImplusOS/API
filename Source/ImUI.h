#pragma once

/*
 * ImUI -- a small retained-mode widget toolkit for ImplusOS applications.
 *
 * An app links this (it is part of the shared API objects), creates an
 * imui_app_t, builds a widget tree either in C or from an XML string
 * (imui_load_xml), sets callbacks, and calls imui_run(). Rendering is
 * client-side into the window backing store with a palette that mirrors
 * the window manager's plasma.theme, so apps look at home on the desktop.
 *
 * Widget tree (all via imui_add / imui_label / ... helpers):
 *   IMUI_ROW / IMUI_COL   flex container (pad, gap, per-child grow)
 *   IMUI_LABEL            static / dynamic text
 *   IMUI_BUTTON           text (+ optional leading icon), on_click
 *   IMUI_ICONBUTTON       icon only (toolbars), on_click
 *   IMUI_DIVIDER          1px separator
 *   IMUI_SPACER           flexible gap (grow=1)
 *   IMUI_TEXTBOX          single-line editable text, on_submit / on_change
 *   IMUI_LIST             scrollable rows {icon,text,subtext}, on_activate
 *   IMUI_TEXTAREA         multi-line editor buffer, on_change
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    IMUI_ICON_NONE = 0,
    IMUI_ICON_FOLDER, IMUI_ICON_FOLDER_OPEN, IMUI_ICON_FILE, IMUI_ICON_DRIVE,
    IMUI_ICON_BACK, IMUI_ICON_FORWARD, IMUI_ICON_UP, IMUI_ICON_HOME,
    IMUI_ICON_REFRESH, IMUI_ICON_NEW, IMUI_ICON_OPEN, IMUI_ICON_SAVE,
    IMUI_ICON_SEARCH, IMUI_ICON_TRASH, IMUI_ICON_COPY, IMUI_ICON_CUT,
    IMUI_ICON_PASTE, IMUI_ICON_EDIT, IMUI_ICON_CHECK, IMUI_ICON_CLOSE,
} imui_icon_t;

typedef enum {
    IMUI_ROW = 0, IMUI_COL, IMUI_LABEL, IMUI_BUTTON, IMUI_ICONBUTTON,
    IMUI_DIVIDER, IMUI_SPACER, IMUI_TEXTBOX, IMUI_LIST, IMUI_TEXTAREA,
} imui_kind_t;

typedef struct imui_widget imui_widget_t;
typedef struct imui_app imui_app_t;

typedef void (*imui_cb_t)(imui_app_t *app, imui_widget_t *w, void *user);

typedef struct {
    imui_icon_t icon;
    char text[128];
    char subtext[96];
} imui_row_t;

struct imui_widget {
    imui_kind_t kind;
    char        id[32];

    /* layout */
    int32_t  pad, gap;
    uint32_t grow;              /* flex weight along parent's main axis */
    uint32_t min_w, min_h;
    bool     fill_bg;           /* draw surface background + border */

    /* content */
    char        text[192];
    imui_icon_t icon;

    /* list */
    imui_row_t *rows;
    uint32_t    row_count, row_cap;
    int32_t     sel;
    uint32_t    scroll;

    /* textbox / textarea */
    char    *buf;
    uint32_t buf_len, buf_cap;
    uint32_t caret;
    uint32_t ta_scroll;        /* first visible line for textarea */
    bool     editable;

    /* callbacks */
    imui_cb_t on_click;
    imui_cb_t on_activate;     /* list row chosen */
    imui_cb_t on_submit;       /* textbox Enter */
    imui_cb_t on_change;
    void     *user;

    /* tree */
    imui_widget_t *parent;
    imui_widget_t *children[32];
    uint32_t       child_count;

    /* computed each frame */
    int32_t rx, ry; uint32_t rw, rh;
    bool    hover, pressed;
};

struct imui_app {
    uint32_t    win;
    uint32_t   *fb;            /* backing store */
    uint32_t    fb_w, fb_h;
    imui_widget_t *root;
    imui_widget_t *focus;
    bool        running;
    bool        needs_layout;
    bool        needs_paint;
    void       *user;
    /* palette (mirrors plasma.theme) */
    uint32_t c_bg, c_surface, c_surface_alt, c_hover, c_text, c_text_dim,
             c_accent, c_accent_soft, c_border, c_danger, c_selection;
};

/* lifecycle */
imui_app_t   *imui_create(uint32_t w, uint32_t h, const char *title);
void          imui_destroy(imui_app_t *app);
int           imui_run(imui_app_t *app);
void          imui_quit(imui_app_t *app);
void          imui_request_paint(imui_app_t *app);

/* tree building (C) */
imui_widget_t *imui_add(imui_widget_t *parent, imui_kind_t kind, const char *id);
imui_widget_t *imui_container(imui_widget_t *parent, imui_kind_t row_or_col,
                              int32_t pad, int32_t gap);
imui_widget_t *imui_label(imui_widget_t *parent, const char *text);
imui_widget_t *imui_button(imui_widget_t *parent, const char *text,
                           imui_icon_t icon, imui_cb_t cb, void *user);
imui_widget_t *imui_iconbutton(imui_widget_t *parent, imui_icon_t icon,
                               imui_cb_t cb, void *user);
imui_widget_t *imui_spacer(imui_widget_t *parent);
imui_widget_t *imui_divider(imui_widget_t *parent);
imui_widget_t *imui_textbox(imui_widget_t *parent, const char *text);
imui_widget_t *imui_list(imui_widget_t *parent);
imui_widget_t *imui_textarea(imui_widget_t *parent);

/* tree building (XML). Returns app->root or NULL. */
imui_widget_t *imui_load_xml(imui_app_t *app, const char *xml);

/* widget helpers */
void imui_set_text(imui_app_t *app, imui_widget_t *w, const char *text);
const char *imui_get_text(const imui_widget_t *w);
imui_widget_t *imui_find(imui_app_t *app, const char *id);

void imui_list_clear(imui_widget_t *list);
void imui_list_add(imui_widget_t *list, imui_icon_t icon,
                   const char *text, const char *subtext);
int  imui_list_selected(const imui_widget_t *list);

void imui_textarea_set(imui_widget_t *ta, const char *text);
const char *imui_textarea_get(const imui_widget_t *ta);

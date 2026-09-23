#pragma once

/*
 * ImUI -- a small retained-mode widget toolkit for ImplusOS applications.
 *
 * An app links this (it is part of the shared API objects), creates an
 * imui_app_t, builds a widget tree either in C or from an XML string
 * (imui_load_xml), sets callbacks, and calls imui_run(). Rendering is
 * client-side into the window backing store.
 *
 * Every widget is a Material Design 3 component drawn from the tokens in
 * API/Source/Material.h, and the scheme is read at startup from the theme
 * file the window manager is using (m3_load_desktop_theme), so an app
 * follows the desktop -- including a retint or a switch to the dark scheme
 * -- without knowing anything about it.
 *
 * Widget tree (all via imui_add / imui_label / ... helpers):
 *   IMUI_ROW / IMUI_COL   flex container (pad, gap, per-child grow). With
 *                         fill_bg it is an M3 outlined card, or -- set to
 *                         IMUI_FILLED -- an edge-to-edge toolbar bar.
 *   IMUI_LABEL            static / dynamic text
 *   IMUI_BUTTON           text (+ optional leading icon), on_click;
 *                         M3 emphasis chosen with imui_set_variant()
 *   IMUI_ICONBUTTON       icon only (toolbars), on_click
 *   IMUI_DIVIDER          1px separator
 *   IMUI_SPACER           flexible gap (grow=1)
 *   IMUI_TEXTBOX          single-line editable text, on_submit / on_change
 *   IMUI_LIST             scrollable rows {icon,text,subtext}, on_activate
 *   IMUI_TEXTAREA         multi-line editor buffer, on_change
 *
 * Clearing `editable` on a textbox or textarea makes it read-only: the caret
 * still moves (so the keyboard scrolls a long buffer) but nothing types into
 * it. That is what a log/output pane wants.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "Input.h"
#include "Material.h"

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

/* Material Design 3's button emphasis ladder, highest first. A toolbar
 * wants IMUI_TEXT (the default); the one action a dialog is asking for
 * wants IMUI_FILLED; the rest sit in between. */
typedef enum {
    IMUI_TEXT = 0,
    IMUI_FILLED,
    IMUI_TONAL,
    IMUI_OUTLINED,
} imui_variant_t;

/* Which M3 type role a label is set in. */
typedef enum {
    IMUI_BODY = 0,      /* body-medium -- running text, the default      */
    IMUI_TITLE,         /* title-medium -- the name of a pane or section */
    IMUI_HEADLINE,      /* headline-small -- one per window at most      */
    IMUI_LABEL_SMALL,   /* label-small -- captions, status, units        */
} imui_type_role_t;

typedef struct imui_widget imui_widget_t;
typedef struct imui_app imui_app_t;

typedef void (*imui_cb_t)(imui_app_t *app, imui_widget_t *w, void *user);
/* Runs before the focused widget sees a key, so an app can take a shortcut
 * (Ctrl+S and friends) no matter where the focus is. Return true to consume
 * the event; false hands it to the widget as usual. */
typedef bool (*imui_key_cb_t)(imui_app_t *app, const input_keyboard_event_t *ev);

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
    imui_variant_t  variant;    /* buttons: M3 emphasis     */
    imui_type_role_t type_role; /* labels: M3 type role     */
    bool     single_click;      /* lists: activate on the first click */

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
    uint32_t ta_vis;           /* textarea lines that fit; set by paint */
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
    /* Double-click recognition, kept per list rather than per app: two
     * lists both clicked on row 0 within 350 ms are two single clicks. */
    uint64_t dbl_ms;
    int32_t  dbl_idx;
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
    /* Called once per event-loop iteration (~60 Hz), before layout and paint.
     * An app that has to watch something the toolkit knows nothing about -- a
     * file descriptor, a socket, a clock -- polls it from here instead of
     * writing its own event loop. `w` is the root widget. */
    imui_cb_t   on_tick;
    /* Every pressed key goes here first; see imui_key_cb_t. NULL (the
     * default) means the focused widget handles every key, as before. */
    imui_key_cb_t on_key;
    /* The desktop's Material Design 3 scheme and type scale, loaded from the
     * shell's theme file. An app may overwrite individual roles after
     * imui_create() if it really has to, but the point of having them here
     * is that it does not. */
    m3_scheme_t     color;
    m3_typescale_t  type;
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

/* Set a button's M3 emphasis, or a label's M3 type role. Both return the
 * widget so they can be chained onto the call that created it. */
imui_widget_t *imui_set_variant(imui_widget_t *button, imui_variant_t variant);

/* Make a list fire on_activate on a single click instead of a double one.
 * A list of actions wants this; a list of files does not. */
imui_widget_t *imui_set_single_click(imui_widget_t *list, bool enable);
imui_widget_t *imui_set_type_role(imui_widget_t *label, imui_type_role_t role);
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

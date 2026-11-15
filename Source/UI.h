#pragma once

/*
 * UI -- a small widget toolkit for ImplusOS applications.
 *
 *     static void on_save(ui_widget_t *w) { ... }
 *
 *     ui_app_t *app = ui_create(640, 480, "Notes");
 *     ui_widget_t *bar = ui_bar(app->root, 56);
 *     ui_button(bar, "Save", UI_ICON_SAVE, on_save, NULL)->primary = true;
 *     ui_widget_t *text = ui_textarea(app->root);
 *     ui_run();
 *
 * One window per process: ui_create() opens it, ui_run() is the event loop
 * and exits the process when the window is closed or ui_quit() is called.
 * Everything is drawn client-side into the window's backing store with the
 * desktop theme (app->theme, see Theme.h).
 *
 * Layout. A ROW or COL lays its children along its main axis: a child with
 * grow > 0 takes a share of the space left over (in proportion to grow), any
 * other child its natural size or min_w / min_h, whichever is larger. Every
 * child fills the cross axis. Containers made by ui_row() / ui_col() grow.
 *
 * Widget fields are public: set grow, min_w, min_h, pad, gap, primary, user,
 * editable, single_click ... directly after creating the widget.
 */

#include <stdbool.h>
#include <stdint.h>

#include "Input.h"
#include "Theme.h"

typedef enum {
    UI_ICON_NONE = 0,
    UI_ICON_FOLDER, UI_ICON_FILE, UI_ICON_DRIVE,
    UI_ICON_BACK, UI_ICON_FORWARD, UI_ICON_UP, UI_ICON_HOME,
    UI_ICON_REFRESH, UI_ICON_NEW, UI_ICON_OPEN, UI_ICON_SAVE,
    UI_ICON_SEARCH, UI_ICON_TRASH, UI_ICON_EDIT, UI_ICON_CHECK,
    UI_ICON_CLOSE, UI_ICON_APPS, UI_ICON_MEMORY, UI_ICON_SETTINGS,
    UI_ICON_RESTART, UI_ICON_MENU,
    UI_ICON_COUNT
} ui_icon_t;

typedef enum {
    UI_ROW = 0, UI_COL,     /* containers                                   */
    UI_LABEL,               /* text                                          */
    UI_BUTTON,              /* text and/or icon; on_action when clicked      */
    UI_SPACER,              /* flexible gap                                  */
    UI_TEXTBOX,             /* one editable line; on_action on Enter         */
    UI_TEXTAREA,            /* multi-line editor                             */
    UI_LIST,                /* rows of {icon, text, subtext}; on_action      */
    UI_CANVAS,              /* drawn by the app (on_paint / on_pointer)      */
} ui_kind_t;

typedef struct ui_widget ui_widget_t;
typedef struct ui_canvas ui_canvas_t;

/* A pointer event as a canvas sees it, in canvas coordinates. The canvas a
 * button went down on keeps getting events until every button is up (so a
 * drag may leave it); the canvas the pointer just left gets one event with
 * `inside` false. */
typedef struct {
    int32_t x, y;
    uint8_t buttons;        /* INPUT_MOUSE_BTN_* held now            */
    uint8_t pressed;        /* ... of those, the ones just pressed   */
    uint8_t released;       /* INPUT_MOUSE_BTN_* just released       */
    int8_t  wheel;          /* > 0 up, < 0 down                      */
    bool    inside;
} ui_pointer_t;

typedef void (*ui_cb_t)(ui_widget_t *w);
typedef void (*ui_paint_cb_t)(ui_widget_t *w, ui_canvas_t *c);
typedef void (*ui_pointer_cb_t)(ui_widget_t *w, const ui_pointer_t *p);

typedef struct {
    ui_icon_t icon;
    char text[128];
    char subtext[96];
} ui_row_t;

struct ui_widget {
    ui_kind_t kind;

    /* layout */
    int32_t  pad, gap;          /* containers                              */
    uint32_t grow;              /* share of the parent's spare main axis   */
    uint32_t min_w, min_h;
    bool     bar;               /* container drawn as a toolbar strip      */

    /* content */
    char      text[192];        /* label / button text                     */
    ui_icon_t icon;
    float     font;             /* label size in px; 0 = THEME_FONT_BODY   */
    bool      dim;              /* label in text_dim                       */
    bool      primary;          /* button drawn in the accent colour       */

    /* list */
    ui_row_t *rows;
    uint32_t  row_count, row_cap;
    int32_t   sel;              /* selected row, -1 for none               */
    uint32_t  scroll;           /* first visible row                       */
    bool      single_click;     /* activate on one click, not two          */

    /* textbox / textarea */
    char     *buf;
    uint32_t  len, cap, caret;
    bool      editable;         /* false: read-only (caret still moves)    */

    /* callbacks */
    ui_cb_t         on_action;  /* button click, list activate, Enter      */
    ui_cb_t         on_change;  /* textbox / textarea contents changed     */
    ui_paint_cb_t   on_paint;   /* canvas                                  */
    ui_pointer_cb_t on_pointer; /* canvas                                  */
    void           *user;

    /* tree */
    ui_widget_t *parent;
    ui_widget_t *children[32];
    uint32_t     child_count;

    /* toolkit-private */
    int32_t  x, y, w, h;
    bool     hover, pressed;
    uint32_t top_line, vis_lines;
    uint64_t click_ms;
    int32_t  click_row;
};

typedef struct {
    uint32_t     window;
    uint32_t    *fb;
    uint32_t     fb_w, fb_h;
    ui_widget_t *root;          /* a COL with no padding                   */
    ui_widget_t *focus;
    theme_t      theme;
    /* Called every loop iteration (~60 Hz) before painting -- for polling
     * whatever the toolkit does not know about. */
    void (*on_tick)(void);
    /* Sees every key press before the focused widget; return true to
     * consume it (app-wide shortcuts). */
    bool (*on_key)(const input_keyboard_event_t *ev);
} ui_app_t;

/* lifecycle */
ui_app_t *ui_create(uint32_t width, uint32_t height, const char *title);
_Noreturn void ui_run(void);
void      ui_quit(void);
void      ui_redraw(void);              /* repaint on the next iteration */

/* widgets */
ui_widget_t *ui_row(ui_widget_t *parent, int32_t pad, int32_t gap);
ui_widget_t *ui_col(ui_widget_t *parent, int32_t pad, int32_t gap);
ui_widget_t *ui_bar(ui_widget_t *parent, uint32_t height);
ui_widget_t *ui_label(ui_widget_t *parent, const char *text);
ui_widget_t *ui_title(ui_widget_t *parent, const char *text);
ui_widget_t *ui_caption(ui_widget_t *parent, const char *text);
ui_widget_t *ui_button(ui_widget_t *parent, const char *text, ui_icon_t icon,
                       ui_cb_t on_action, void *user);
ui_widget_t *ui_spacer(ui_widget_t *parent);
ui_widget_t *ui_textbox(ui_widget_t *parent, const char *text);
ui_widget_t *ui_textarea(ui_widget_t *parent);
ui_widget_t *ui_list(ui_widget_t *parent);
ui_widget_t *ui_canvas(ui_widget_t *parent, ui_paint_cb_t on_paint,
                       ui_pointer_cb_t on_pointer, void *user);

/* Text of a label, button, textbox or textarea. Setting a textarea moves
 * its caret to the start. */
void        ui_set_text(ui_widget_t *w, const char *text);
const char *ui_get_text(const ui_widget_t *w);
void        ui_focus(ui_widget_t *w);

void ui_list_clear(ui_widget_t *list);
void ui_list_add(ui_widget_t *list, ui_icon_t icon, const char *text,
                 const char *subtext);

/* ---------------------------------------------------------------- canvas
 * Coordinates are relative to the canvas and clipped to it. Colours blend
 * by their alpha. */
struct ui_canvas {
    int32_t w, h;               /* size of the canvas                      */
    /* toolkit-private */
    uint32_t *px;
    int32_t   stride, ox, oy, cx0, cy0, cx1, cy1;
};

void ui_draw_fill(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h,
                  uint32_t argb);
void ui_draw_round(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t radius, uint32_t argb);
void ui_draw_frame(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h,
                   uint32_t argb);
void ui_draw_line(ui_canvas_t *c, int32_t x0, int32_t y0, int32_t x1,
                  int32_t y1, int32_t thickness, uint32_t argb);
/* Text with its visual top at y; clip_w > 0 stops at that width. */
void ui_draw_text(ui_canvas_t *c, int32_t x, int32_t y, const char *text,
                  float px, uint32_t argb, int32_t clip_w);
void ui_draw_icon(ui_canvas_t *c, int32_t x, int32_t y, int32_t size,
                  ui_icon_t icon, uint32_t argb);
int32_t ui_text_width(const char *text, float px);

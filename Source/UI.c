/* UI -- widget toolkit for ImplusOS applications. See UI.h. */

#include "UI.h"

#include "File.h"
#include "Process.h"
#include "Serial.h"
#include "Window.h"

/* The kernel sends the real arrows as KEY_UP 0xE048 and so on; the keypad
 * scan codes 0x47..0x53 only look like them. */
#include "../../../Kernel/Source/include/kernel/keycodes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ stb libs */

static void *ui_realloc(void *p, size_t old_size, size_t new_size)
{
    if (new_size == 0) { free(p); return NULL; }
    void *q = malloc(new_size);
    if (q && p) memcpy(q, p, old_size < new_size ? old_size : new_size);
    if (q) free(p);
    return q;
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x, u) ((void)(u), malloc(x))
#define STBTT_free(x, u)   ((void)(u), free(x))
#define STBTT_fmod(x, y)   fmod(x, y)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "Header/stb_truetype.h"
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_MALLOC(z)              malloc(z)
#define STBI_REALLOC(p, z)          ui_realloc(p, 0, z)
#define STBI_REALLOC_SIZED(p, o, z) ui_realloc(p, o, z)
#define STBI_FREE(p)                free(p)
#include "Header/stb_image.h"
#pragma GCC diagnostic pop

static ui_app_t *g_ui;
static bool      g_dirty = true;
static bool      g_running = true;

/* --------------------------------------------------------------- files */

static uint8_t *read_file(const char *path, uint32_t max, uint32_t *out_size)
{
    file_stat_t st;
    if (file_stat(path, &st) < 0 || !st.exists || st.is_dir ||
        st.size == 0 || st.size > max)
        return NULL;
    int32_t fd = file_open(path, 0);
    if (fd < 0) return NULL;
    uint8_t *buf = malloc(st.size);
    uint32_t got = 0;
    while (buf && got < st.size) {
        int64_t n = file_read(fd, buf + got, st.size - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    file_close(fd);
    if (buf && got != st.size) { free(buf); buf = NULL; }
    if (buf) *out_size = got;
    return buf;
}

/* ---------------------------------------------------------------- font */

static stbtt_fontinfo g_font;
static bool           g_font_ok;

static void font_init(void)
{
    static const char *const paths[] = {
        "/Userland/com.ImplusOS.windowmanager/Resource/Fonts/NotoSansJP-Regular.ttf",
        "/BootManager/Resource/Fonts/NotoSansJP-Regular.ttf",
    };
    for (unsigned i = 0; i < 2 && !g_font_ok; ++i) {
        uint32_t size = 0;
        uint8_t *buf = read_file(paths[i], 64u * 1024u * 1024u, &size);
        if (!buf) continue;
        int off = stbtt_GetFontOffsetForIndex(buf, 0);
        if (off >= 0 && stbtt_InitFont(&g_font, buf, off)) g_font_ok = true;
        else free(buf);
    }
}

/* -------------------------------------------------------------- raster */

/* All drawing goes through a ui_canvas_t in absolute surface coordinates
 * (ox/oy are applied by the public ui_draw_* wrappers). */

static inline uint32_t blend(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24;
    if (a == 0xFF) return s;
    if (a == 0) return d;
    uint32_t r = (((s >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255;
    uint32_t g = (((s >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255;
    uint32_t b = ((s & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline void put(ui_canvas_t *c, int x, int y, uint32_t col)
{
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1) return;
    uint32_t *p = &c->px[y * c->stride + x];
    *p = blend(col, *p);
}

static void fill(ui_canvas_t *c, int x, int y, int w, int h, uint32_t col)
{
    if (x < c->cx0) { w -= c->cx0 - x; x = c->cx0; }
    if (y < c->cy0) { h -= c->cy0 - y; y = c->cy0; }
    if (x + w > c->cx1) w = c->cx1 - x;
    if (y + h > c->cy1) h = c->cy1 - y;
    for (int j = 0; j < h; ++j) {
        uint32_t *row = &c->px[(y + j) * c->stride + x];
        for (int i = 0; i < w; ++i) row[i] = blend(col, row[i]);
    }
}

static void round_rect(ui_canvas_t *c, int x, int y, int w, int h, int r, uint32_t col)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 1) { fill(c, x, y, w, h, col); return; }
    fill(c, x + r, y, w - 2 * r, h, col);
    fill(c, x, y + r, r, h - 2 * r, col);
    fill(c, x + w - r, y + r, r, h - 2 * r, col);
    for (int j = 0; j < r; ++j)
        for (int i = 0; i < r; ++i) {
            int dx = r - 1 - i, dy = r - 1 - j;
            if (dx * dx + dy * dy > r * r) continue;
            put(c, x + i, y + j, col);
            put(c, x + w - 1 - i, y + j, col);
            put(c, x + i, y + h - 1 - j, col);
            put(c, x + w - 1 - i, y + h - 1 - j, col);
        }
}

/* A rounded box with a `t`-pixel outline. */
static void boxed(ui_canvas_t *c, int x, int y, int w, int h, int r,
                  uint32_t bg, uint32_t outline, int t)
{
    round_rect(c, x, y, w, h, r, outline);
    if (w > 2 * t && h > 2 * t)
        round_rect(c, x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, bg);
}

static void frame(ui_canvas_t *c, int x, int y, int w, int h, uint32_t col)
{
    fill(c, x, y, w, 1, col);
    fill(c, x, y + h - 1, w, 1, col);
    fill(c, x, y, 1, h, col);
    fill(c, x + w - 1, y, 1, h, col);
}

static void line(ui_canvas_t *c, int x0, int y0, int x1, int y1, int th, uint32_t col)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; ++i) {
        int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        for (int oy = -th / 2; oy <= th / 2; ++oy)
            for (int ox = -th / 2; ox <= th / 2; ++ox)
                put(c, x + ox, y + oy, col);
    }
}

/* ---------------------------------------------------------------- text */

static int next_cp(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    int cp = *p;
    if (cp < 0x80) p += 1;
    else if ((cp & 0xE0) == 0xC0 && p[1]) {
        cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
    } else if ((cp & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
    } else p += 1;
    *pp = p;
    return cp;
}

/* Width of the first `n` bytes of `s` (n < 0: all of it). */
static int text_w_n(const char *s, int n, float px)
{
    if (!g_font_ok || !s) return 0;
    float sc = stbtt_ScaleForPixelHeight(&g_font, px);
    const unsigned char *p = (const unsigned char *)s;
    const unsigned char *end = n < 0 ? NULL : p + n;
    int w = 0, prev = 0;
    while (*p && (!end || p < end)) {
        int cp = next_cp(&p);
        if (prev) w += (int)((float)stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sc);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        w += (int)((float)adv * sc);
        prev = cp;
    }
    return w;
}

static void glyph(ui_canvas_t *c, int x, int y, int cp, float sc, uint32_t col)
{
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&g_font, cp, sc, sc, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0) return;
    uint8_t *bm = malloc((size_t)(bw * bh));
    if (!bm) return;
    stbtt_MakeCodepointBitmap(&g_font, bm, bw, bh, bw, sc, sc, cp);
    uint32_t rgb = col & 0x00FFFFFFu, ca = col >> 24;
    for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i) {
            uint32_t a = bm[j * bw + i] * ca / 255u;
            if (a) put(c, x + x0 + i, y + y0 + j, (a << 24) | rgb);
        }
    free(bm);
}

/* Text with its visual top at `top`, stopped at clip_w px (0 = no clip). */
static void text(ui_canvas_t *c, int x, int top, const char *s, float px,
                 uint32_t col, int clip_w)
{
    if (!g_font_ok || !s) return;
    float sc = stbtt_ScaleForPixelHeight(&g_font, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    int baseline = top + (int)((float)asc * sc);
    int pen = x, prev = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        int cp = next_cp(&p);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        if (clip_w > 0 && pen + (int)((float)adv * sc) > x + clip_w) break;
        if (prev) pen += (int)((float)stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sc);
        if (cp != ' ') glyph(c, pen, baseline, cp, sc, col);
        pen += (int)((float)adv * sc);
        prev = cp;
    }
}

/* Text vertically centred in a box of height h. */
static void text_mid(ui_canvas_t *c, int x, int y, int h, const char *s,
                     float px, uint32_t col, int clip_w)
{
    text(c, x, y + (h - (int)px - 3) / 2, s, px, col, clip_w);
}

/* --------------------------------------------------------------- icons */

/* White RGBA PNGs shipped with the window manager, tinted at blit time. */
#define ICON_DIR "/Userland/com.ImplusOS.windowmanager/Resource/Icons/md/"

static const char *const k_icon_file[UI_ICON_COUNT] = {
    [UI_ICON_FOLDER]   = "folder",       [UI_ICON_FILE]     = "description",
    [UI_ICON_DRIVE]    = "storage",      [UI_ICON_BACK]     = "arrow_back",
    [UI_ICON_FORWARD]  = "arrow_forward",[UI_ICON_UP]       = "arrow_upward",
    [UI_ICON_HOME]     = "home",         [UI_ICON_REFRESH]  = "refresh",
    [UI_ICON_NEW]      = "note_add",     [UI_ICON_OPEN]     = "file_open",
    [UI_ICON_SAVE]     = "save",         [UI_ICON_SEARCH]   = "search",
    [UI_ICON_TRASH]    = "delete",       [UI_ICON_EDIT]     = "edit",
    [UI_ICON_CHECK]    = "check",        [UI_ICON_CLOSE]    = "close",
    [UI_ICON_APPS]     = "apps",         [UI_ICON_MEMORY]   = "memory",
    [UI_ICON_SETTINGS] = "settings",     [UI_ICON_RESTART]  = "restart_alt",
    [UI_ICON_MENU]     = "menu",
};

static struct { uint8_t *alpha; int w, h; bool tried; } g_icons[UI_ICON_COUNT];

static void icon(ui_canvas_t *c, int x, int y, int sz, ui_icon_t k, uint32_t col)
{
    if ((int)k <= 0 || k >= UI_ICON_COUNT || !k_icon_file[k] || sz <= 0) return;
    if (!g_icons[k].tried) {
        g_icons[k].tried = true;
        char path[160];
        snprintf(path, sizeof(path), "%s%s.png", ICON_DIR, k_icon_file[k]);
        uint32_t size = 0;
        uint8_t *enc = read_file(path, 4u * 1024u * 1024u, &size);
        int w = 0, h = 0, n = 0;
        uint8_t *rgba = enc ? stbi_load_from_memory(enc, (int)size, &w, &h, &n, 4) : NULL;
        free(enc);
        if (rgba) {
            g_icons[k].alpha = malloc((size_t)(w * h));
            if (g_icons[k].alpha) {
                for (int i = 0; i < w * h; ++i) g_icons[k].alpha[i] = rgba[i * 4 + 3];
                g_icons[k].w = w;
                g_icons[k].h = h;
            }
            stbi_image_free(rgba);
        }
    }
    const uint8_t *a = g_icons[k].alpha;
    int iw = g_icons[k].w, ih = g_icons[k].h;
    if (!a) return;
    uint32_t rgb = col & 0x00FFFFFFu, ca = col >> 24;
    int dn = sz > 1 ? sz - 1 : 1;
    for (int j = 0; j < sz; ++j) {
        uint32_t fy = (uint32_t)((int64_t)j * (ih - 1) * 65536 / dn);
        int y0 = (int)(fy >> 16), y1 = y0 + 1 < ih ? y0 + 1 : y0;
        uint32_t wy = (fy >> 8) & 0xFF;
        for (int i = 0; i < sz; ++i) {
            uint32_t fx = (uint32_t)((int64_t)i * (iw - 1) * 65536 / dn);
            int x0 = (int)(fx >> 16), x1 = x0 + 1 < iw ? x0 + 1 : x0;
            uint32_t wx = (fx >> 8) & 0xFF;
            uint32_t top = a[y0 * iw + x0] * (256 - wx) + a[y0 * iw + x1] * wx;
            uint32_t bot = a[y1 * iw + x0] * (256 - wx) + a[y1 * iw + x1] * wx;
            uint32_t v = ((top * (256 - wy) + bot * wy) >> 16) * ca / 255u;
            if (v) put(c, x + i, y + j, (v << 24) | rgb);
        }
    }
}

/* -------------------------------------------------------------- canvas */

void ui_draw_fill(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col)
{
    if (c && w > 0 && h > 0) fill(c, c->ox + x, c->oy + y, w, h, col);
}
void ui_draw_round(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t r, uint32_t col)
{
    if (c) round_rect(c, c->ox + x, c->oy + y, w, h, r, col);
}
void ui_draw_frame(ui_canvas_t *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col)
{
    if (c && w > 0 && h > 0) frame(c, c->ox + x, c->oy + y, w, h, col);
}
void ui_draw_line(ui_canvas_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  int32_t th, uint32_t col)
{
    if (c) line(c, c->ox + x0, c->oy + y0, c->ox + x1, c->oy + y1, th > 0 ? th : 1, col);
}
void ui_draw_text(ui_canvas_t *c, int32_t x, int32_t y, const char *s, float px,
                  uint32_t col, int32_t clip_w)
{
    if (c) text(c, c->ox + x, c->oy + y, s, px, col, clip_w);
}
void ui_draw_icon(ui_canvas_t *c, int32_t x, int32_t y, int32_t size, ui_icon_t k,
                  uint32_t col)
{
    if (c) icon(c, c->ox + x, c->oy + y, size, k, col);
}
int32_t ui_text_width(const char *s, float px) { return text_w_n(s, -1, px); }

/* ----------------------------------------------------------------- tree */

static ui_widget_t *add(ui_widget_t *parent, ui_kind_t kind)
{
    ui_widget_t *w = calloc(1, sizeof(*w));
    if (!w) {
        serial_write_string("[ui] out of memory\n");
        process_exit(1);
    }
    w->kind = kind;
    w->sel = -1;
    w->click_row = -1;
    w->editable = true;
    if (parent && parent->child_count < 32) {
        w->parent = parent;
        parent->children[parent->child_count++] = w;
    }
    return w;
}

static void set_buf(ui_widget_t *w, const char *s)
{
    uint32_t n = s ? (uint32_t)strlen(s) : 0u;
    if (n + 1u > w->cap) {
        uint32_t cap = n + 1024u;
        w->buf = ui_realloc(w->buf, w->cap, cap);
        w->cap = w->buf ? cap : 0u;
        if (!w->buf) { w->len = w->caret = 0; return; }
    }
    if (n) memcpy(w->buf, s, n);
    w->buf[n] = '\0';
    w->len = n;
}

ui_widget_t *ui_row(ui_widget_t *p, int32_t pad, int32_t gap)
{
    ui_widget_t *w = add(p, UI_ROW);
    w->pad = pad; w->gap = gap; w->grow = 1;
    return w;
}
ui_widget_t *ui_col(ui_widget_t *p, int32_t pad, int32_t gap)
{
    ui_widget_t *w = add(p, UI_COL);
    w->pad = pad; w->gap = gap; w->grow = 1;
    return w;
}
ui_widget_t *ui_bar(ui_widget_t *p, uint32_t height)
{
    ui_widget_t *w = ui_row(p, 8, 6);
    w->grow = 0; w->min_h = height; w->bar = true;
    return w;
}
ui_widget_t *ui_label(ui_widget_t *p, const char *t)
{
    ui_widget_t *w = add(p, UI_LABEL);
    if (t) strncpy(w->text, t, sizeof(w->text) - 1);
    return w;
}
ui_widget_t *ui_title(ui_widget_t *p, const char *t)
{
    ui_widget_t *w = ui_label(p, t);
    w->font = THEME_FONT_TITLE;
    return w;
}
ui_widget_t *ui_caption(ui_widget_t *p, const char *t)
{
    ui_widget_t *w = ui_label(p, t);
    w->font = THEME_FONT_SMALL; w->dim = true;
    return w;
}
ui_widget_t *ui_button(ui_widget_t *p, const char *t, ui_icon_t ic, ui_cb_t cb, void *user)
{
    ui_widget_t *w = add(p, UI_BUTTON);
    if (t) strncpy(w->text, t, sizeof(w->text) - 1);
    w->icon = ic; w->on_action = cb; w->user = user;
    return w;
}
ui_widget_t *ui_spacer(ui_widget_t *p)
{
    ui_widget_t *w = add(p, UI_SPACER);
    w->grow = 1;
    return w;
}
ui_widget_t *ui_textbox(ui_widget_t *p, const char *t)
{
    ui_widget_t *w = add(p, UI_TEXTBOX);
    set_buf(w, t);
    w->caret = w->len;
    return w;
}
ui_widget_t *ui_textarea(ui_widget_t *p)
{
    ui_widget_t *w = add(p, UI_TEXTAREA);
    w->grow = 1;
    set_buf(w, "");
    return w;
}
ui_widget_t *ui_list(ui_widget_t *p)
{
    ui_widget_t *w = add(p, UI_LIST);
    w->grow = 1;
    return w;
}
ui_widget_t *ui_canvas(ui_widget_t *p, ui_paint_cb_t on_paint,
                       ui_pointer_cb_t on_pointer, void *user)
{
    ui_widget_t *w = add(p, UI_CANVAS);
    w->grow = 1;
    w->on_paint = on_paint; w->on_pointer = on_pointer; w->user = user;
    return w;
}

void ui_set_text(ui_widget_t *w, const char *t)
{
    if (!w) return;
    if (w->kind == UI_TEXTBOX || w->kind == UI_TEXTAREA) {
        set_buf(w, t);
        w->caret = w->kind == UI_TEXTBOX ? w->len : 0u;
        w->top_line = 0;
        w->painted_caret = UINT32_MAX;     /* view follows the caret again */
    } else {
        strncpy(w->text, t ? t : "", sizeof(w->text) - 1);
        w->text[sizeof(w->text) - 1] = '\0';
    }
    g_dirty = true;
}

const char *ui_get_text(const ui_widget_t *w)
{
    if (!w) return "";
    if (w->kind == UI_TEXTBOX || w->kind == UI_TEXTAREA) return w->buf ? w->buf : "";
    return w->text;
}

void ui_focus(ui_widget_t *w)
{
    if (g_ui) g_ui->focus = w;
    g_dirty = true;
}

void ui_list_clear(ui_widget_t *l)
{
    if (!l) return;
    l->row_count = 0; l->sel = -1; l->scroll = 0;
    g_dirty = true;
}

void ui_list_add(ui_widget_t *l, ui_icon_t ic, const char *t, const char *sub)
{
    if (!l) return;
    if (l->row_count >= l->row_cap) {
        uint32_t cap = l->row_cap ? l->row_cap * 2 : 64;
        ui_row_t *rows = ui_realloc(l->rows, l->row_cap * sizeof(ui_row_t),
                                    cap * sizeof(ui_row_t));
        if (!rows) return;
        l->rows = rows; l->row_cap = cap;
    }
    ui_row_t *r = &l->rows[l->row_count++];
    memset(r, 0, sizeof(*r));
    r->icon = ic;
    if (t) strncpy(r->text, t, sizeof(r->text) - 1);
    if (sub) strncpy(r->subtext, sub, sizeof(r->subtext) - 1);
    g_dirty = true;
}

/* -------------------------------------------------------------- layout */

#define ROW_H    48     /* list row; shared by paint and hit test */
#define CTRL_H   36     /* button, textbox                        */
#define LIST_PAD 4

static float label_px(const ui_widget_t *w) { return w->font > 0 ? w->font : THEME_FONT_BODY; }

/* Natural size of a leaf along one axis. Containers, lists, textareas and
 * canvases have none: they ask for space with grow or min_w / min_h. */
static int natural(const ui_widget_t *w, bool horizontal)
{
    switch (w->kind) {
    case UI_LABEL:
        return horizontal ? text_w_n(w->text, -1, label_px(w)) + 4 : (int)label_px(w) + 10;
    case UI_BUTTON:
        if (!w->text[0]) return CTRL_H;
        return horizontal ? text_w_n(w->text, -1, THEME_FONT_BODY) + (w->icon ? 50 : 28)
                          : CTRL_H;
    case UI_TEXTBOX: return horizontal ? 160 : CTRL_H;
    default:         return 0;
    }
}

static void layout(ui_widget_t *w, int x, int y, int cw, int ch)
{
    w->x = x; w->y = y; w->w = cw; w->h = ch;
    if (w->kind != UI_ROW && w->kind != UI_COL) return;
    int n = (int)w->child_count;
    if (n == 0) return;

    bool horiz = w->kind == UI_ROW;
    int inner_w = cw - 2 * w->pad, inner_h = ch - 2 * w->pad;
    int avail = (horiz ? inner_w : inner_h) - w->gap * (n - 1);

    int fixed = 0, grow_sum = 0;
    for (int i = 0; i < n; ++i) {
        ui_widget_t *c = w->children[i];
        int min = horiz ? (int)c->min_w : (int)c->min_h;
        if (c->grow) grow_sum += (int)c->grow;
        else fixed += natural(c, horiz) > min ? natural(c, horiz) : min;
    }
    /* Fixed children that do not fit shrink proportionally rather than run
     * past the end and push the last one out of the window. */
    int num = 1, den = 1;
    if (fixed > avail && fixed > 0) { num = avail > 0 ? avail : 0; den = fixed; }
    int spare = avail - fixed * num / den;
    if (spare < 0) spare = 0;

    int cur = horiz ? x + w->pad : y + w->pad;
    for (int i = 0; i < n; ++i) {
        ui_widget_t *c = w->children[i];
        int min = horiz ? (int)c->min_w : (int)c->min_h, m;
        if (c->grow && grow_sum) {
            m = spare * (int)c->grow / grow_sum;
            if (m < min && min <= avail) m = min;
        } else {
            m = natural(c, horiz);
            if (m < min) m = min;
            m = m * num / den;
        }
        if (m < 0) m = 0;
        if (horiz) layout(c, cur, y + w->pad, m, inner_h);
        else       layout(c, x + w->pad, cur, inner_w, m);
        cur += m + w->gap;
    }
}

/* --------------------------------------------------------------- paint */

static int list_visible(const ui_widget_t *w)
{
    int vis = (w->h - 2 * LIST_PAD) / ROW_H;
    return vis > 0 ? vis : 1;
}

static void list_clamp(ui_widget_t *w)
{
    int max = (int)w->row_count - list_visible(w);
    if (max < 0) max = 0;
    if ((int)w->scroll > max) w->scroll = (uint32_t)max;
}

static void paint_textarea(ui_canvas_t *c, const theme_t *t, ui_widget_t *w, bool focus)
{
    int x = w->x, y = w->y, pad = 10;
    boxed(c, x, y, w->w, w->h, THEME_RADIUS, t->surface,
          focus ? t->accent : t->border, focus ? 2 : 1);
    float px = THEME_FONT_BODY;
    int lh = (int)px + 6;
    int vis = (w->h - 2 * pad) / lh;
    if (vis < 1) vis = 1;
    w->vis_lines = (uint32_t)vis;

    /* caret line/column and line count */
    uint32_t cl = 0, line_start = 0, lines = 1;
    for (uint32_t i = 0; i < w->len; ++i) {
        if (w->buf[i] != '\n') continue;
        if (i < w->caret) { cl++; line_start = i + 1; }
        lines++;
    }
    /* The view follows the caret when the caret has moved; otherwise it
     * stays where the wheel put it. */
    if (w->caret != w->painted_caret) {
        w->painted_caret = w->caret;
        if (cl < w->top_line) w->top_line = cl;
        if (cl >= w->top_line + (uint32_t)vis) w->top_line = cl - (uint32_t)vis + 1;
    }
    if (w->top_line + (uint32_t)vis > lines)
        w->top_line = lines > (uint32_t)vis ? lines - (uint32_t)vis : 0u;

    char buf[512];
    const char *p = w->buf;
    for (uint32_t ln = 0; p; ++ln) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (ln >= w->top_line + (uint32_t)vis) break;
        if (ln >= w->top_line) {
            if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
            memcpy(buf, p, n); buf[n] = '\0';
            text(c, x + pad, y + pad + (int)(ln - w->top_line) * lh, buf, px, t->text,
                 w->w - 2 * pad);
        }
        p = e ? e + 1 : NULL;
    }
    if (focus) {
        int cx = x + pad + text_w_n(w->buf + line_start, (int)(w->caret - line_start), px);
        fill(c, cx, y + pad + (int)(cl - w->top_line) * lh, 2, lh, t->accent);
    }
}

static void paint(ui_canvas_t *c, ui_widget_t *w)
{
    const theme_t *t = &g_ui->theme;
    int x = w->x, y = w->y, cw = w->w, ch = w->h;
    bool focus = g_ui->focus == w;
    uint32_t hover = color_alpha(t->text, w->pressed ? 14u : THEME_HOVER_PCT);

    switch (w->kind) {
    case UI_ROW: case UI_COL:
        if (w->bar) {
            fill(c, x, y, cw, ch, t->surface_alt);
            fill(c, x, y + ch - 1, cw, 1, t->border);
        }
        for (uint32_t i = 0; i < w->child_count; ++i) paint(c, w->children[i]);
        break;

    case UI_LABEL:
        text_mid(c, x + 2, y, ch, w->text, label_px(w), w->dim ? t->text_dim : t->text,
                 cw - 4);
        break;

    case UI_BUTTON: {
        uint32_t fg = t->text;
        if (w->primary) {
            round_rect(c, x, y, cw, ch, THEME_RADIUS, t->accent);
            fg = t->on_accent;
        } else if (w->text[0]) {
            boxed(c, x, y, cw, ch, THEME_RADIUS, t->surface, t->border, 1);
        }
        if (w->hover || w->pressed)
            round_rect(c, x, y, cw, ch, THEME_RADIUS,
                       w->primary ? color_alpha(t->on_accent, 14u) : hover);
        if (!w->text[0]) {
            icon(c, x + (cw - 20) / 2, y + (ch - 20) / 2, 20, w->icon, fg);
            break;
        }
        /* icon + label, centred as a group */
        int content = text_w_n(w->text, -1, THEME_FONT_BODY) + (w->icon ? 24 : 0);
        int tx = x + (cw - content) / 2;
        if (tx < x + 12) tx = x + 12;
        if (w->icon) { icon(c, tx, y + (ch - 18) / 2, 18, w->icon, fg); tx += 24; }
        text_mid(c, tx, y, ch, w->text, THEME_FONT_BODY, fg, x + cw - 8 - tx);
        break;
    }

    case UI_TEXTBOX: {
        boxed(c, x, y, cw, ch, THEME_RADIUS, t->surface,
              focus ? t->accent : t->border, focus ? 2 : 1);
        float px = label_px(w);
        int ty = y + (ch - (int)px - 3) / 2;
        text(c, x + 10, ty, w->buf, px, t->text, cw - 20);
        if (focus)
            fill(c, x + 10 + text_w_n(w->buf, (int)w->caret, px), ty - 2, 2,
                 (int)px + 6, t->accent);
        break;
    }

    case UI_TEXTAREA:
        paint_textarea(c, t, w, focus);
        break;

    case UI_LIST: {
        boxed(c, x, y, cw, ch, THEME_RADIUS, t->surface, focus ? t->accent : t->border, 1);
        list_clamp(w);
        int vis = list_visible(w);
        for (int i = 0; i < vis && w->scroll + (uint32_t)i < w->row_count; ++i) {
            uint32_t ri = w->scroll + (uint32_t)i;
            const ui_row_t *r = &w->rows[ri];
            int ry = y + LIST_PAD + i * ROW_H;
            if ((int)ri == w->sel)
                round_rect(c, x + 4, ry + 2, cw - 8, ROW_H - 4, THEME_RADIUS, t->accent_soft);
            if (r->icon) icon(c, x + 14, ry + (ROW_H - 22) / 2, 22, r->icon, t->text);
            int tx = x + (r->icon ? 48 : 16), avail = cw - (tx - x) - 14;
            if (r->subtext[0]) {
                text(c, tx, ry + 6, r->text, THEME_FONT_BODY, t->text, avail);
                text(c, tx, ry + 26, r->subtext, THEME_FONT_SMALL, t->text_dim, avail);
            } else {
                text_mid(c, tx, ry, ROW_H, r->text, THEME_FONT_BODY, t->text, avail);
            }
        }
        if ((int)w->row_count > vis) {
            int track = ch - 2 * LIST_PAD;
            int th = track * vis / (int)w->row_count;
            if (th < 24) th = 24;
            int ty = (track - th) * (int)w->scroll / ((int)w->row_count - vis);
            round_rect(c, x + cw - 7, y + LIST_PAD + ty, 4, th, 2, t->border);
        }
        break;
    }

    case UI_CANVAS: {
        if (!w->on_paint || cw <= 0 || ch <= 0) break;
        ui_canvas_t sub = *c;
        sub.ox = x; sub.oy = y; sub.w = cw; sub.h = ch;
        if (x > sub.cx0) sub.cx0 = x;
        if (y > sub.cy0) sub.cy0 = y;
        if (x + cw < sub.cx1) sub.cx1 = x + cw;
        if (y + ch < sub.cy1) sub.cy1 = y + ch;
        if (sub.cx0 < sub.cx1 && sub.cy0 < sub.cy1) w->on_paint(w, &sub);
        break;
    }

    case UI_SPACER:
        break;
    }
}

/* -------------------------------------------------------------- events */

static ui_widget_t *hit(ui_widget_t *w, int x, int y)
{
    if (x < w->x || x >= w->x + w->w || y < w->y || y >= w->y + w->h) return NULL;
    for (int i = (int)w->child_count - 1; i >= 0; --i) {
        ui_widget_t *h = hit(w->children[i], x, y);
        if (h) return h;
    }
    if (w->kind == UI_ROW || w->kind == UI_COL || w->kind == UI_SPACER ||
        w->kind == UI_LABEL)
        return NULL;
    return w;
}

static void clear_hover(ui_widget_t *w)
{
    w->hover = w->pressed = false;
    for (uint32_t i = 0; i < w->child_count; ++i) clear_hover(w->children[i]);
}

static void changed(ui_widget_t *w)
{
    if (w->on_change) w->on_change(w);
}

static void buf_insert(ui_widget_t *w, const char *s, uint32_t n)
{
    if (w->len + n + 1u > w->cap) {
        uint32_t cap = w->cap + n + 2048u;
        char *b = ui_realloc(w->buf, w->cap, cap);
        if (!b) return;
        w->buf = b; w->cap = cap;
    }
    memmove(w->buf + w->caret + n, w->buf + w->caret, w->len - w->caret + 1u);
    memcpy(w->buf + w->caret, s, n);
    w->len += n; w->caret += n;
    changed(w);
}

static void buf_backspace(ui_widget_t *w)
{
    if (w->caret == 0) return;
    memmove(w->buf + w->caret - 1, w->buf + w->caret, w->len - w->caret + 1u);
    w->caret--; w->len--;
    changed(w);
}

static uint32_t line_start(const ui_widget_t *w, uint32_t i)
{
    while (i > 0 && w->buf[i - 1] != '\n') i--;
    return i;
}

static uint32_t line_end(const ui_widget_t *w, uint32_t i)
{
    while (i < w->len && w->buf[i] != '\n') i++;
    return i;
}

/* Move the caret `lines` up (< 0) or down, keeping its column. */
static void caret_lines(ui_widget_t *w, int lines)
{
    uint32_t col = w->caret - line_start(w, w->caret);
    uint32_t pos = w->caret;
    for (; lines < 0; ++lines) {
        uint32_t s = line_start(w, pos);
        if (s == 0) break;
        pos = s - 1;                /* the newline ending the line above */
    }
    for (; lines > 0; --lines) {
        uint32_t e = line_end(w, pos);
        if (e >= w->len) break;
        pos = e + 1;
    }
    uint32_t s = line_start(w, pos), len = line_end(w, pos) - s;
    w->caret = s + (col < len ? col : len);
}

static void key_text(ui_widget_t *f, const input_keyboard_event_t *ev)
{
    char c = (char)ev->ascii;
    bool multi = f->kind == UI_TEXTAREA;
    if (ev->keycode == KEY_LEFT)       { if (f->caret > 0) f->caret--; }
    else if (ev->keycode == KEY_RIGHT) { if (f->caret < f->len) f->caret++; }
    else if (multi && ev->keycode == KEY_UP)   caret_lines(f, -1);
    else if (multi && ev->keycode == KEY_DOWN) caret_lines(f, 1);
    else if (multi && (ev->keycode == KEY_PAGEUP || ev->keycode == KEY_PAGEDOWN)) {
        int page = f->vis_lines ? (int)f->vis_lines : 16;
        caret_lines(f, ev->keycode == KEY_PAGEUP ? -page : page);
    } else if (c == '\r' || c == '\n') {
        if (!multi) { if (f->on_action) f->on_action(f); }
        else if (f->editable) buf_insert(f, "\n", 1);
    } else if (!f->editable) {
        return;
    } else if (c == 8 || c == 127) buf_backspace(f);
    else if (multi && c == '\t') buf_insert(f, "    ", 4);
    else if (c >= 32 && c < 127) buf_insert(f, &c, 1);
}

static void key_list(ui_widget_t *f, const input_keyboard_event_t *ev)
{
    int n = (int)f->row_count, sel = f->sel, page = list_visible(f);
    if (n == 0) return;
    switch (ev->keycode) {
    case KEY_UP:       sel = sel > 0 ? sel - 1 : 0; break;
    case KEY_DOWN:     sel = sel + 1 < n ? sel + 1 : n - 1; break;
    case KEY_PAGEUP:   sel = sel < 0 ? 0 : sel - page; break;
    case KEY_PAGEDOWN: sel = sel < 0 ? page - 1 : sel + page; break;
    case KEY_HOME:     sel = 0; break;
    case KEY_END:      sel = n - 1; break;
    default:
        if ((ev->ascii == '\r' || ev->ascii == '\n') && sel >= 0 && f->on_action)
            f->on_action(f);
        return;
    }
    if (sel < 0) sel = 0;
    if (sel > n - 1) sel = n - 1;
    f->sel = sel;
    /* bring the selection into view */
    if (sel < (int)f->scroll) f->scroll = (uint32_t)sel;
    else if (sel >= (int)f->scroll + page) f->scroll = (uint32_t)(sel - page + 1);
}

static void scroll_by(ui_widget_t *w, int delta)
{
    if (w->kind == UI_LIST) {
        int next = (int)w->scroll + delta;
        w->scroll = next < 0 ? 0u : (uint32_t)next;
        list_clamp(w);
    } else {
        int next = (int)w->top_line + delta;   /* clamped by the painter */
        w->top_line = next < 0 ? 0u : (uint32_t)next;
    }
}

static void press_list(ui_widget_t *h, int my)
{
    int idx = (int)h->scroll + (my - (h->y + LIST_PAD)) / ROW_H;
    if (idx < 0 || idx >= (int)h->row_count) return;
    uint64_t now = get_uptime_ms();
    bool dbl = idx == h->click_row && now - h->click_ms < 350u;
    h->sel = idx;
    h->click_ms = now;
    h->click_row = idx;
    if ((dbl || h->single_click) && h->on_action) h->on_action(h);
}

/* ------------------------------------------------------------ lifecycle */

ui_app_t *ui_create(uint32_t width, uint32_t height, const char *title)
{
    if (g_ui) return g_ui;
    font_init();
    ui_app_t *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->window = window_create(width, height, title ? title : "App");
    if (!app->window) { free(app); return NULL; }
    app->fb = window_get_backing_store(app->window, &app->fb_w, &app->fb_h);
    window_subscribe_keyboard(app->window);
    window_subscribe_mouse(app->window);
    app->root = add(NULL, UI_COL);
    app->root->grow = 1;
    theme_load_desktop(&app->theme);
    g_ui = app;
    return app;
}

void ui_quit(void) { g_running = false; }
void ui_redraw(void) { g_dirty = true; }

static void route_mouse(const input_mouse_event_t *me)
{
    static bool btn_prev;
    static uint8_t buttons_prev;
    static ui_widget_t *capture, *last;
    ui_app_t *app = g_ui;
    int mx = me->x, my = me->y;
    bool btn = (me->buttons & INPUT_MOUSE_BTN_LEFT) != 0;
    ui_widget_t *h = hit(app->root, mx, my);

    clear_hover(app->root);
    if (h) h->hover = true;
    if (btn && !btn_prev && h) {
        h->pressed = true;
        app->focus = (h->kind == UI_TEXTBOX || h->kind == UI_TEXTAREA ||
                      h->kind == UI_LIST) ? h : NULL;
        if (h->kind == UI_LIST) press_list(h, my);
    } else if (!btn && btn_prev && h && h->kind == UI_BUTTON && h->on_action) {
        h->on_action(h);
    }
    btn_prev = btn;

    if (me->wheel != 0) {
        ui_widget_t *s = h;
        while (s && s->kind != UI_LIST && s->kind != UI_TEXTAREA) s = s->parent;
        if (s) scroll_by(s, me->wheel > 0 ? -3 : 3);
    }

    /* canvases get the pointer raw, with capture while a button is held */
    uint8_t buttons = me->buttons & (INPUT_MOUSE_BTN_LEFT | INPUT_MOUSE_BTN_RIGHT |
                                     INPUT_MOUSE_BTN_MIDDLE);
    uint8_t down = (uint8_t)(buttons & ~buttons_prev);
    uint8_t up = (uint8_t)(buttons_prev & ~buttons);
    ui_widget_t *under = h && h->kind == UI_CANVAS ? h : NULL;
    if (!capture && down && under) capture = under;
    ui_widget_t *target = capture ? capture : under;
    if (last && last != target && last->on_pointer) {
        ui_pointer_t left = { mx - last->x, my - last->y, buttons, 0, 0, 0, false };
        last->on_pointer(last, &left);
    }
    last = target;
    if (target && target->on_pointer) {
        ui_pointer_t p = { mx - target->x, my - target->y, buttons, down, up,
                           me->wheel, target == under };
        target->on_pointer(target, &p);
    }
    if (!buttons) capture = NULL;
    buttons_prev = buttons;
}

_Noreturn void ui_run(void)
{
    ui_app_t *app = g_ui;
    if (!app || !app->fb) process_exit(1);
    bool need_layout = true;

    while (g_running) {
        if (app->on_tick) app->on_tick();

        uint32_t rx, ry, rw, rh;
        if (window_get_rect(app->window, &rx, &ry, &rw, &rh) < 0) break;
        if ((rw && rw != app->fb_w) || (rh && rh != app->fb_h)) {
            uint32_t bw = 0, bh = 0;
            uint32_t *fb = window_get_backing_store(app->window, &bw, &bh);
            if (fb) { app->fb = fb; app->fb_w = bw; app->fb_h = bh; }
            need_layout = true;
        }

        input_mouse_event_t me;
        while (window_input_mouse_poll(&me) > 0) {
            route_mouse(&me);
            g_dirty = true;
        }

        input_keyboard_event_t ke;
        while (window_input_keyboard_poll(&ke) > 0) {
            if (!ke.pressed) continue;
            g_dirty = true;
            if (app->on_key && app->on_key(&ke)) continue;
            ui_widget_t *f = app->focus;
            if (!f) continue;
            if (f->kind == UI_TEXTBOX || f->kind == UI_TEXTAREA) key_text(f, &ke);
            else if (f->kind == UI_LIST) key_list(f, &ke);
        }

        if (need_layout) {
            layout(app->root, 0, 0, (int)app->fb_w, (int)app->fb_h);
            need_layout = false;
            g_dirty = true;
        }
        if (g_dirty) {
            g_dirty = false;
            ui_canvas_t c = { (int32_t)app->fb_w, (int32_t)app->fb_h, app->fb,
                              (int32_t)app->fb_w, 0, 0, 0, 0,
                              (int32_t)app->fb_w, (int32_t)app->fb_h };
            fill(&c, 0, 0, c.w, c.h, app->theme.bg);
            paint(&c, app->root);
            window_damage(app->window, 0, 0, app->fb_w, app->fb_h);
        }
        sleep_ms(16);
    }
    /* _start has no caller to return to: leaving it jumps to address 0. */
    window_destroy(app->window);
    process_exit(0);
    for (;;) {}
}

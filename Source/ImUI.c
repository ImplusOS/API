/* ImUI -- retained-mode widget toolkit for ImplusOS apps. See ImUI.h. */

#include "ImUI.h"

#include "Graphics.h"
#include "Window.h"
#include "Input.h"
#include "File.h"
#include "Process.h"

#include "XMLParser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------- stb font */

static void *imui_realloc_sized(void *p, size_t o, size_t n) {
    if (n == 0) { if (p) free(p); return NULL; }
    void *q = malloc(n);
    if (q && p) { memcpy(q, p, o < n ? o : n); free(p); }
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

static stbtt_fontinfo g_font;
static uint8_t       *g_font_buf;
static int            g_font_ok;

static void font_init(void) {
    if (g_font_ok) return;
    static const char *paths[] = {
        "/Userland/com.ImplusOS.windowmanager/Resource/Fonts/NotoSansJP-Regular.ttf",
        "/BootManager/Resource/Fonts/NotoSansJP-Regular.ttf",
    };
    for (unsigned k = 0; k < 2; ++k) {
        file_stat_t st;
        if (file_stat(paths[k], &st) < 0 || !st.exists || st.is_dir || st.size == 0) continue;
        int32_t fd = file_open(paths[k], 0);
        if (fd < 0) continue;
        uint8_t *b = malloc(st.size);
        if (!b) { file_close(fd); continue; }
        uint32_t got = 0;
        while (got < st.size) { int64_t n = file_read(fd, b + got, st.size - got);
            if (n <= 0) break; got += (uint32_t)n; }
        file_close(fd);
        if (got != st.size) { free(b); continue; }
        int off = stbtt_GetFontOffsetForIndex(b, 0);
        if (off < 0 || !stbtt_InitFont(&g_font, b, off)) { free(b); continue; }
        g_font_buf = b; g_font_ok = 1; return;
    }
}

/* ------------------------------------------------------------- raster */

typedef struct { uint32_t *px; int w, h; } surf_t;

static inline uint32_t blend(uint32_t s, uint32_t d) {
    uint32_t a = s >> 24;
    if (a == 0xFF) return s | 0xFF000000u;
    if (a == 0) return d;
    uint32_t sr = (s >> 16) & 0xFF, sg = (s >> 8) & 0xFF, sb = s & 0xFF;
    uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    uint32_t r = (sr * a + dr * (255 - a)) / 255;
    uint32_t g = (sg * a + dg * (255 - a)) / 255;
    uint32_t b = (sb * a + db * (255 - a)) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
static inline void sput(surf_t *s, int x, int y, uint32_t c) {
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h) return;
    uint32_t *p = &s->px[y * s->w + x];
    *p = blend(c, *p);
}
static void sfill(surf_t *s, int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    for (int j = 0; j < h; ++j) {
        uint32_t *row = &s->px[(y + j) * s->w + x];
        for (int i = 0; i < w; ++i) row[i] = blend(c, row[i]);
    }
}
static void sround(surf_t *s, int x, int y, int w, int h, int r, uint32_t c) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 1) { sfill(s, x, y, w, h, c); return; }
    sfill(s, x + r, y, w - 2 * r, h, c);
    sfill(s, x, y + r, r, h - 2 * r, c);
    sfill(s, x + w - r, y + r, r, h - 2 * r, c);
    int r2 = r * r;
    for (int j = 0; j < r; ++j)
        for (int i = 0; i < r; ++i) {
            int dx = r - 1 - i, dy = r - 1 - j;
            if (dx * dx + dy * dy <= r2) {
                sput(s, x + i, y + j, c);
                sput(s, x + w - 1 - i, y + j, c);
                sput(s, x + i, y + h - 1 - j, c);
                sput(s, x + w - 1 - i, y + h - 1 - j, c);
            }
        }
}
static void sstroke(surf_t *s, int x, int y, int w, int h, uint32_t c) {
    sfill(s, x, y, w, 1, c);
    sfill(s, x, y + h - 1, w, 1, c);
    sfill(s, x, y, 1, h, c);
    sfill(s, x + w - 1, y, 1, h, c);
}
static void sline(surf_t *s, int x0, int y0, int x1, int y1, int th, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
              ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; ++i) {
        int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        for (int oy = -th / 2; oy <= th / 2; ++oy)
            for (int ox = -th / 2; ox <= th / 2; ++ox)
                sput(s, x + ox, y + oy, c);
    }
}
static void sdisc(surf_t *s, int cx, int cy, int r, uint32_t c) {
    for (int j = -r; j <= r; ++j)
        for (int i = -r; i <= r; ++i)
            if (i * i + j * j <= r * r) sput(s, cx + i, cy + j, c);
}

/* ------------------------------------------------------------- text */

static float scale_for(float px) { return g_font_ok ? stbtt_ScaleForPixelHeight(&g_font, px) : 0; }

static int text_w(const char *s, float px) {
    if (!g_font_ok || !s) return 0;
    float sc = scale_for(px);
    int w = 0, prev = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        int cp = *p;
        if (cp < 0x80) p += 1;
        else if ((cp & 0xE0) == 0xC0) { cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((cp & 0xF0) == 0xE0) { cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else p += 1;
        if (prev) w += (int)(stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sc);
        int adv, lsb; stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        w += (int)(adv * sc);
        prev = cp;
    }
    return w;
}

static void draw_glyph(surf_t *s, int x, int y, int cp, float sc, uint32_t col) {
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&g_font, cp, sc, sc, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0) return;
    uint8_t *bm = malloc((size_t)(bw * bh));
    if (!bm) return;
    stbtt_MakeCodepointBitmap(&g_font, bm, bw, bh, bw, sc, sc, cp);
    uint32_t rgb = col & 0x00FFFFFFu;
    for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i) {
            uint8_t a = bm[j * bw + i];
            if (a) sput(s, x + x0 + i, y + y0 + j, ((uint32_t)a << 24) | rgb);
        }
    free(bm);
}

static int text_w_prefix(const char *s, uint32_t n, float px) {
    char tmp[256];
    uint32_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
    memcpy(tmp, s, k); tmp[k] = '\0';
    return text_w(tmp, px);
}

/* draw text with visual top at y_top, clipped to clip_w px (0 = no clip) */
static void draw_text(surf_t *s, int x, int y_top, const char *str, float px,
                      uint32_t col, int clip_w) {
    if (!g_font_ok || !str) return;
    float sc = scale_for(px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    int baseline = y_top + (int)(asc * sc);
    int pen = x, prev = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p;) {
        int cp = *p;
        if (cp < 0x80) p += 1;
        else if ((cp & 0xE0) == 0xC0) { cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((cp & 0xF0) == 0xE0) { cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else p += 1;
        int adv, lsb; stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        if (clip_w && pen + (int)(adv * sc) > x + clip_w) break;
        if (prev) pen += (int)(stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sc);
        if (cp != ' ') draw_glyph(s, pen, baseline, cp, sc, col);
        pen += (int)(adv * sc);
        prev = cp;
    }
}

/* ------------------------------------------------------------- icons */

static void imui_icon(surf_t *s, int x, int y, int sz, imui_icon_t k, uint32_t c)
{
#define IX(n) (x + (n) * sz / 24)
#define IY(n) (y + (n) * sz / 24)
#define IS(n) ((n) * sz / 24 < 1 ? 1 : (n) * sz / 24)
    switch (k) {
    case IMUI_ICON_FOLDER: case IMUI_ICON_FOLDER_OPEN:
        sround(s, IX(3), IY(6), IS(7), IS(3), IS(1), c);
        sround(s, IX(3), IY(8), IS(18), IS(12), IS(2), c);
        break;
    case IMUI_ICON_FILE: case IMUI_ICON_OPEN:
        sround(s, IX(5), IY(3), IS(11), IS(18), IS(2), c);
        sround(s, IX(13), IY(3), IS(5), IS(6), IS(1), (c & 0xFFFFFF) | 0x88000000u);
        break;
    case IMUI_ICON_DRIVE:
        sround(s, IX(3), IY(6), IS(18), IS(12), IS(2), c);
        sdisc(s, IX(17), IY(12), IS(2), (c & 0xFFFFFF) | 0xAA000000u);
        break;
    case IMUI_ICON_BACK:
        sline(s, IX(15), IY(5), IX(8), IY(12), IS(3), c);
        sline(s, IX(8), IY(12), IX(15), IY(19), IS(3), c);
        break;
    case IMUI_ICON_FORWARD:
        sline(s, IX(9), IY(5), IX(16), IY(12), IS(3), c);
        sline(s, IX(16), IY(12), IX(9), IY(19), IS(3), c);
        break;
    case IMUI_ICON_UP:
        sline(s, IX(5), IY(15), IX(12), IY(8), IS(3), c);
        sline(s, IX(12), IY(8), IX(19), IY(15), IS(3), c);
        sfill(s, IX(11), IY(8), IS(2), IS(11), c);
        break;
    case IMUI_ICON_HOME:
        sline(s, IX(4), IY(12), IX(12), IY(4), IS(3), c);
        sline(s, IX(12), IY(4), IX(20), IY(12), IS(3), c);
        sround(s, IX(7), IY(11), IS(10), IS(10), IS(1), c);
        break;
    case IMUI_ICON_REFRESH:
        for (int a = 40; a < 320; a += 12) {
            double rad = a * 3.14159 / 180.0;
            sdisc(s, x + sz / 2 + (int)(cos(rad) * sz * 0.32),
                     y + sz / 2 + (int)(sin(rad) * sz * 0.32), IS(1), c);
        }
        sline(s, IX(17), IY(3), IX(20), IY(9), IS(3), c);
        break;
    case IMUI_ICON_NEW:
        sround(s, IX(5), IY(3), IS(14), IS(18), IS(2), c);
        sfill(s, IX(11), IY(8), IS(2), IS(9), (c & 0xFFFFFF) | 0xCC000000u);
        sfill(s, IX(8), IY(11), IS(8), IS(2), (c & 0xFFFFFF) | 0xCC000000u);
        break;
    case IMUI_ICON_SAVE:
        sround(s, IX(4), IY(4), IS(16), IS(16), IS(2), c);
        sfill(s, IX(8), IY(4), IS(8), IS(6), (c & 0xFFFFFF) | 0x99000000u);
        sround(s, IX(8), IY(13), IS(8), IS(6), IS(1), (c & 0xFFFFFF) | 0x99000000u);
        break;
    case IMUI_ICON_SEARCH:
        for (int a = 0; a < 360; a += 8) {
            double rad = a * 3.14159 / 180.0;
            sdisc(s, x + sz * 10 / 24 + (int)(cos(rad) * sz * 0.28),
                     y + sz * 10 / 24 + (int)(sin(rad) * sz * 0.28), IS(1), c);
        }
        sline(s, IX(15), IY(15), IX(20), IY(20), IS(3), c);
        break;
    case IMUI_ICON_TRASH:
        sround(s, IX(6), IY(6), IS(12), IS(15), IS(1), c);
        sfill(s, IX(4), IY(4), IS(16), IS(2), c);
        break;
    case IMUI_ICON_COPY:
        sround(s, IX(4), IY(4), IS(12), IS(12), IS(1), c);
        sround(s, IX(9), IY(9), IS(12), IS(12), IS(1), c);
        break;
    case IMUI_ICON_CUT:
        sdisc(s, IX(7), IY(17), IS(3), c); sdisc(s, IX(17), IY(17), IS(3), c);
        sline(s, IX(7), IY(17), IX(19), IY(4), IS(2), c);
        sline(s, IX(17), IY(17), IX(5), IY(4), IS(2), c);
        break;
    case IMUI_ICON_PASTE:
        sround(s, IX(5), IY(4), IS(14), IS(17), IS(2), c);
        sfill(s, IX(9), IY(2), IS(6), IS(4), c);
        break;
    case IMUI_ICON_EDIT:
        sline(s, IX(4), IY(20), IX(16), IY(8), IS(4), c);
        sround(s, IX(15), IY(4), IS(5), IS(5), IS(1), c);
        break;
    case IMUI_ICON_CHECK:
        sline(s, IX(5), IY(12), IX(10), IY(18), IS(3), c);
        sline(s, IX(10), IY(18), IX(19), IY(6), IS(3), c);
        break;
    case IMUI_ICON_CLOSE:
        sline(s, IX(6), IY(6), IX(18), IY(18), IS(3), c);
        sline(s, IX(18), IY(6), IX(6), IY(18), IS(3), c);
        break;
    default: break;
    }
#undef IX
#undef IY
#undef IS
}

/* ------------------------------------------------------------- tree */

static imui_widget_t *w_new(imui_kind_t k) {
    imui_widget_t *w = calloc(1, sizeof(*w));
    w->kind = k;
    w->grow = (k == IMUI_SPACER) ? 1u : 0u;
    return w;
}
static void w_attach(imui_widget_t *parent, imui_widget_t *c) {
    if (!parent || parent->child_count >= 32) return;
    c->parent = parent;
    parent->children[parent->child_count++] = c;
}

imui_widget_t *imui_add(imui_widget_t *parent, imui_kind_t kind, const char *id) {
    imui_widget_t *w = w_new(kind);
    if (id) { strncpy(w->id, id, sizeof(w->id) - 1); }
    w_attach(parent, w);
    return w;
}
imui_widget_t *imui_container(imui_widget_t *p, imui_kind_t roc, int32_t pad, int32_t gap) {
    imui_widget_t *w = w_new(roc == IMUI_ROW ? IMUI_ROW : IMUI_COL);
    w->pad = pad; w->gap = gap; w->grow = 1;
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_label(imui_widget_t *p, const char *t) {
    imui_widget_t *w = w_new(IMUI_LABEL);
    if (t) strncpy(w->text, t, sizeof(w->text) - 1);
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_button(imui_widget_t *p, const char *t, imui_icon_t ic, imui_cb_t cb, void *u) {
    imui_widget_t *w = w_new(IMUI_BUTTON);
    if (t) strncpy(w->text, t, sizeof(w->text) - 1);
    w->icon = ic; w->on_click = cb; w->user = u;
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_iconbutton(imui_widget_t *p, imui_icon_t ic, imui_cb_t cb, void *u) {
    imui_widget_t *w = w_new(IMUI_ICONBUTTON);
    w->icon = ic; w->on_click = cb; w->user = u; w->min_w = 32; w->min_h = 32;
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_spacer(imui_widget_t *p) { return imui_add(p, IMUI_SPACER, NULL); }
imui_widget_t *imui_divider(imui_widget_t *p) { return imui_add(p, IMUI_DIVIDER, NULL); }
imui_widget_t *imui_textbox(imui_widget_t *p, const char *t) {
    imui_widget_t *w = w_new(IMUI_TEXTBOX);
    w->buf_cap = 512; w->buf = calloc(1, w->buf_cap); w->editable = true; w->min_h = 32;
    if (t) { strncpy(w->buf, t, w->buf_cap - 1); w->buf_len = (uint32_t)strlen(w->buf); w->caret = w->buf_len; }
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_list(imui_widget_t *p) {
    imui_widget_t *w = w_new(IMUI_LIST);
    w->grow = 1; w->sel = -1; w->fill_bg = true;
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_textarea(imui_widget_t *p) {
    imui_widget_t *w = w_new(IMUI_TEXTAREA);
    w->grow = 1; w->fill_bg = true; w->editable = true;
    w->buf_cap = 4096; w->buf = calloc(1, w->buf_cap);
    w_attach(p, w);
    return w;
}

static void w_free(imui_widget_t *w) {
    if (!w) return;
    for (uint32_t i = 0; i < w->child_count; ++i) w_free(w->children[i]);
    free(w->rows); free(w->buf); free(w);
}

/* ------------------------------------------------------------- list ops */

void imui_list_clear(imui_widget_t *l) { if (l) { l->row_count = 0; l->sel = -1; l->scroll = 0; } }
void imui_list_add(imui_widget_t *l, imui_icon_t ic, const char *t, const char *sub) {
    if (!l) return;
    if (l->row_count >= l->row_cap) {
        uint32_t nc = l->row_cap ? l->row_cap * 2 : 64;
        l->rows = imui_realloc_sized(l->rows, l->row_cap * sizeof(imui_row_t), nc * sizeof(imui_row_t));
        l->row_cap = nc;
    }
    imui_row_t *r = &l->rows[l->row_count++];
    memset(r, 0, sizeof(*r));
    r->icon = ic;
    if (t) strncpy(r->text, t, sizeof(r->text) - 1);
    if (sub) strncpy(r->subtext, sub, sizeof(r->subtext) - 1);
}
int imui_list_selected(const imui_widget_t *l) { return l ? l->sel : -1; }

void imui_textarea_set(imui_widget_t *ta, const char *t) {
    if (!ta || !ta->buf) return;
    uint32_t n = t ? (uint32_t)strlen(t) : 0;
    if (n + 1 > ta->buf_cap) {
        uint32_t nc = n + 4096;
        ta->buf = imui_realloc_sized(ta->buf, ta->buf_cap, nc);
        ta->buf_cap = nc;
    }
    if (t) memcpy(ta->buf, t, n);
    ta->buf[n] = '\0';
    ta->buf_len = n; ta->caret = 0; ta->ta_scroll = 0;
}
const char *imui_textarea_get(const imui_widget_t *ta) { return ta && ta->buf ? ta->buf : ""; }

void imui_set_text(imui_app_t *app, imui_widget_t *w, const char *t) {
    if (!w) return;
    if (w->kind == IMUI_TEXTBOX) {
        strncpy(w->buf, t ? t : "", w->buf_cap - 1);
        w->buf[w->buf_cap - 1] = '\0';
        w->buf_len = (uint32_t)strlen(w->buf); w->caret = w->buf_len;
    } else {
        strncpy(w->text, t ? t : "", sizeof(w->text) - 1);
        w->text[sizeof(w->text) - 1] = '\0';
    }
    if (app) app->needs_paint = true;
}
const char *imui_get_text(const imui_widget_t *w) {
    if (!w) return "";
    return (w->kind == IMUI_TEXTBOX || w->kind == IMUI_TEXTAREA) ? (w->buf ? w->buf : "") : w->text;
}
static imui_widget_t *find_rec(imui_widget_t *w, const char *id) {
    if (!w) return NULL;
    if (w->id[0] && strcmp(w->id, id) == 0) return w;
    for (uint32_t i = 0; i < w->child_count; ++i) {
        imui_widget_t *r = find_rec(w->children[i], id);
        if (r) return r;
    }
    return NULL;
}
imui_widget_t *imui_find(imui_app_t *app, const char *id) {
    return app ? find_rec(app->root, id) : NULL;
}

/* ------------------------------------------------------------- XML */

static imui_icon_t icon_by_name(const char *n) {
    if (!n) return IMUI_ICON_NONE;
    struct { const char *k; imui_icon_t v; } m[] = {
        {"folder", IMUI_ICON_FOLDER}, {"file", IMUI_ICON_FILE}, {"drive", IMUI_ICON_DRIVE},
        {"back", IMUI_ICON_BACK}, {"forward", IMUI_ICON_FORWARD}, {"up", IMUI_ICON_UP},
        {"home", IMUI_ICON_HOME}, {"refresh", IMUI_ICON_REFRESH}, {"new", IMUI_ICON_NEW},
        {"open", IMUI_ICON_OPEN}, {"save", IMUI_ICON_SAVE}, {"search", IMUI_ICON_SEARCH},
        {"trash", IMUI_ICON_TRASH}, {"copy", IMUI_ICON_COPY}, {"cut", IMUI_ICON_CUT},
        {"paste", IMUI_ICON_PASTE}, {"edit", IMUI_ICON_EDIT},
    };
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); ++i)
        if (strcmp(n, m[i].k) == 0) return m[i].v;
    return IMUI_ICON_NONE;
}

static imui_widget_t *xml_build(imui_widget_t *parent, xml_node_t *n) {
    if (!n) return NULL;
    imui_widget_t *w = NULL;
    const char *tag = n->tag;
    const char *id = xml_get_attr(n, "id");
    const char *grow = xml_get_attr(n, "grow");
    const char *pad = xml_get_attr(n, "pad");
    const char *gap = xml_get_attr(n, "gap");

    if (!strcmp(tag, "row") || !strcmp(tag, "col")) {
        w = imui_container(parent, tag[0] == 'r' ? IMUI_ROW : IMUI_COL,
                           pad ? atoi(pad) : 0, gap ? atoi(gap) : 0);
        const char *fill = xml_get_attr(n, "fill");
        if (fill && atoi(fill)) w->fill_bg = true;
    } else if (!strcmp(tag, "label")) {
        w = imui_label(parent, n->text[0] ? n->text : xml_get_attr(n, "text"));
    } else if (!strcmp(tag, "button")) {
        w = imui_button(parent, n->text[0] ? n->text : xml_get_attr(n, "text"),
                        icon_by_name(xml_get_attr(n, "icon")), NULL, NULL);
    } else if (!strcmp(tag, "iconbutton")) {
        w = imui_iconbutton(parent, icon_by_name(xml_get_attr(n, "icon")), NULL, NULL);
    } else if (!strcmp(tag, "spacer")) {
        w = imui_spacer(parent);
    } else if (!strcmp(tag, "divider")) {
        w = imui_divider(parent);
    } else if (!strcmp(tag, "textbox")) {
        w = imui_textbox(parent, xml_get_attr(n, "text"));
    } else if (!strcmp(tag, "list")) {
        w = imui_list(parent);
    } else if (!strcmp(tag, "textarea")) {
        w = imui_textarea(parent);
    } else {
        return NULL;
    }
    if (id) strncpy(w->id, id, sizeof(w->id) - 1);
    if (grow) w->grow = (uint32_t)atoi(grow);
    for (uint32_t i = 0; i < n->child_count; ++i)
        xml_build(w, n->children[i]);
    return w;
}

imui_widget_t *imui_load_xml(imui_app_t *app, const char *xml) {
    if (!app || !xml) return NULL;
    xml_node_t *root = xml_parse(xml);
    if (!root) return NULL;
    if (app->root) w_free(app->root);
    app->root = w_new(IMUI_COL);
    app->root->grow = 1;
    xml_build(app->root, root);
    xml_free(root);
    app->needs_layout = app->needs_paint = true;
    return app->root;
}

/* ------------------------------------------------------------- lifecycle */

imui_app_t *imui_create(uint32_t w, uint32_t h, const char *title) {
    font_init();
    imui_app_t *app = calloc(1, sizeof(*app));
    app->win = window_create(w, h, title ? title : "App");
    if (!app->win) { free(app); return NULL; }
    uint32_t bw = 0, bh = 0;
    app->fb = window_get_backing_store(app->win, &bw, &bh);
    app->fb_w = bw; app->fb_h = bh;
    window_subscribe_keyboard(app->win);
    window_subscribe_mouse(app->win);
    app->running = true;
    app->needs_layout = app->needs_paint = true;
    app->root = w_new(IMUI_COL);
    app->root->grow = 1;

    app->c_bg          = 0xFFF5F5F5;
    app->c_surface     = 0xFFFFFFFF;
    app->c_surface_alt = 0xFFF3F4F6;
    app->c_hover       = 0x14000000;
    app->c_text        = 0xFF1C1B1F;
    app->c_text_dim    = 0xFF49454F;
    app->c_accent      = 0xFF3B82F6;
    app->c_accent_soft = 0x1F3B82F6;
    app->c_border      = 0x22000000;
    app->c_danger      = 0xFFB3261E;
    app->c_selection   = 0x243B82F6;
    return app;
}

void imui_destroy(imui_app_t *app) {
    if (!app) return;
    w_free(app->root);
    if (app->win) window_destroy(app->win);
    free(app);
}
void imui_quit(imui_app_t *app) { if (app) app->running = false; }
void imui_request_paint(imui_app_t *app) { if (app) app->needs_paint = true; }

/* ------------------------------------------------------------- layout */

static int leaf_main(imui_app_t *app, imui_widget_t *w, bool horizontal) {
    (void)app;
    switch (w->kind) {
    case IMUI_LABEL:   return horizontal ? text_w(w->text, 14) + 4 : 20;
    case IMUI_BUTTON:  return horizontal ? text_w(w->text, 14) + (w->icon ? 44 : 24) : 32;
    case IMUI_ICONBUTTON: return 34;
    case IMUI_DIVIDER: return 1;
    case IMUI_TEXTBOX: return horizontal ? 120 : 32;
    case IMUI_SPACER:  return 0;
    default:           return 0; /* containers/list/textarea rely on grow */
    }
}

static void layout(imui_app_t *app, imui_widget_t *w, int x, int y, int cw, int ch) {
    w->rx = x; w->ry = y; w->rw = (uint32_t)cw; w->rh = (uint32_t)ch;
    if (w->kind != IMUI_ROW && w->kind != IMUI_COL) return;

    bool horiz = (w->kind == IMUI_ROW);
    int px = x + w->pad, py = y + w->pad;
    int inner_w = cw - 2 * w->pad, inner_h = ch - 2 * w->pad;
    int main_avail = (horiz ? inner_w : inner_h);
    int n = (int)w->child_count;
    if (n == 0) return;
    main_avail -= w->gap * (n - 1);

    int fixed = 0, grow_sum = 0;
    for (int i = 0; i < n; ++i) {
        imui_widget_t *c = w->children[i];
        if (c->grow > 0) grow_sum += (int)c->grow;
        else fixed += leaf_main(app, c, horiz);
    }
    int flex = main_avail - fixed;
    if (flex < 0) flex = 0;

    int cur = horiz ? px : py;
    for (int i = 0; i < n; ++i) {
        imui_widget_t *c = w->children[i];
        int m = (c->grow > 0 && grow_sum > 0)
                ? (flex * (int)c->grow / grow_sum)
                : leaf_main(app, c, horiz);
        int cm = (int)c->min_w;
        if (!horiz) cm = (int)c->min_h;
        if (m < cm) m = cm;
        if (horiz)
            layout(app, c, cur, py, m, inner_h);
        else
            layout(app, c, px, cur, inner_w, m);
        cur += m + w->gap;
    }
}

/* ------------------------------------------------------------- paint */

static void paint(imui_app_t *app, surf_t *s, imui_widget_t *w) {
    int x = w->rx, y = w->ry, cw = (int)w->rw, ch = (int)w->rh;

    switch (w->kind) {
    case IMUI_ROW: case IMUI_COL:
        if (w->fill_bg) {
            sround(s, x, y, cw, ch, 8, app->c_surface);
            sstroke(s, x, y, cw, ch, app->c_border);
        }
        for (uint32_t i = 0; i < w->child_count; ++i) paint(app, s, w->children[i]);
        break;
    case IMUI_LABEL:
        draw_text(s, x + 2, y + (ch - 15) / 2, w->text, 14, app->c_text, cw - 4);
        break;
    case IMUI_BUTTON: {
        uint32_t bg = w->pressed ? app->c_accent_soft : w->hover ? app->c_hover : 0;
        if (bg) sround(s, x, y, cw, ch, 8, bg);
        int tx = x + 10;
        if (w->icon) { imui_icon(s, x + 8, y + (ch - 18) / 2, 18, w->icon, app->c_text); tx = x + 32; }
        draw_text(s, tx, y + (ch - 15) / 2, w->text, 14, app->c_text, cw - (tx - x) - 6);
        break;
    }
    case IMUI_ICONBUTTON: {
        uint32_t bg = w->pressed ? app->c_accent_soft : w->hover ? app->c_hover : 0;
        if (bg) sround(s, x + 1, y + 1, cw - 2, ch - 2, 8, bg);
        imui_icon(s, x + (cw - 20) / 2, y + (ch - 20) / 2, 20, w->icon, app->c_text);
        break;
    }
    case IMUI_DIVIDER:
        if (cw > ch) sfill(s, x, y + ch / 2, cw, 1, app->c_border);
        else sfill(s, x + cw / 2, y, 1, ch, app->c_border);
        break;
    case IMUI_SPACER: break;
    case IMUI_TEXTBOX: {
        bool focus = (app->focus == w);
        sround(s, x, y, cw, ch, 8, app->c_surface_alt);
        if (focus) sstroke(s, x, y, cw, ch, app->c_accent);
        else sstroke(s, x, y, cw, ch, app->c_border);
        draw_text(s, x + 10, y + (ch - 15) / 2, w->buf, 14, app->c_text, cw - 20);
        if (focus) {
            int caret_x = x + 10 + text_w_prefix(w->buf, w->caret, 14);
            sfill(s, caret_x, y + 7, 1, ch - 14, app->c_accent);
        }
        break;
    }
    case IMUI_LIST: {
        sround(s, x, y, cw, ch, 8, app->c_surface);
        sstroke(s, x, y, cw, ch, app->c_border);
        int rh = 40;
        int vis = (ch - 8) / rh;
        if ((int)w->scroll > (int)w->row_count - vis && (int)w->row_count > vis)
            w->scroll = w->row_count - vis;
        if ((int)w->row_count <= vis) w->scroll = 0;
        for (int i = 0; i < vis; ++i) {
            uint32_t ri = w->scroll + (uint32_t)i;
            if (ri >= w->row_count) break;
            imui_row_t *r = &w->rows[ri];
            int ry = y + 4 + i * rh;
            bool sel = ((int)ri == w->sel);
            if (sel) {
                sfill(s, x + 3, ry, cw - 6, rh, app->c_selection);
                sfill(s, x + 3, ry, 3, rh, app->c_accent);
            }
            if (r->icon) imui_icon(s, x + 12, ry + (rh - 22) / 2, 22, r->icon, app->c_text);
            int tx = x + 44;
            if (r->subtext[0]) {
                draw_text(s, tx, ry + 5, r->text, 13, app->c_text, cw - (tx - x) - 12);
                draw_text(s, tx, ry + 21, r->subtext, 11, app->c_text_dim, cw - (tx - x) - 12);
            } else {
                draw_text(s, tx, ry + (rh - 14) / 2, r->text, 13, app->c_text, cw - (tx - x) - 12);
            }
        }
        if ((int)w->row_count > vis) {
            int track_h = ch - 8;
            int th = track_h * vis / (int)w->row_count; if (th < 20) th = 20;
            int ty = w->row_count > (uint32_t)vis
                   ? (track_h - th) * (int)w->scroll / ((int)w->row_count - vis) : 0;
            sround(s, x + cw - 6, y + 4 + ty, 3, th, 1, app->c_text_dim);
        }
        break;
    }
    case IMUI_TEXTAREA: {
        bool focus = (app->focus == w);
        sround(s, x, y, cw, ch, 8, app->c_surface);
        sstroke(s, x, y, cw, ch, focus ? app->c_accent : app->c_border);
        int lh = 18, pad = 8;
        int vis = (ch - 2 * pad) / lh;
        /* find caret line/col + total lines */
        uint32_t line = 0, col = 0, cl = 0, cc = 0;
        for (uint32_t i = 0; i < w->buf_len; ++i) {
            if (i == w->caret) { cl = line; cc = col; }
            if (w->buf[i] == '\n') { line++; col = 0; } else col++;
        }
        if (w->caret >= w->buf_len) { cl = line; cc = col; }
        if (cl < w->ta_scroll) w->ta_scroll = cl;
        if ((int)cl >= (int)w->ta_scroll + vis) w->ta_scroll = cl - (uint32_t)vis + 1;
        /* draw visible lines */
        const char *p = w->buf;
        uint32_t ln = 0;
        char lbuf[512];
        while (*p || ln <= line) {
            const char *e = p;
            while (*e && *e != '\n') e++;
            if (ln >= w->ta_scroll && (int)ln < (int)w->ta_scroll + vis) {
                size_t n = (size_t)(e - p); if (n > sizeof(lbuf) - 1) n = sizeof(lbuf) - 1;
                memcpy(lbuf, p, n); lbuf[n] = '\0';
                draw_text(s, x + pad, y + pad + (int)(ln - w->ta_scroll) * lh,
                          lbuf, 14, app->c_text, cw - 2 * pad);
            }
            if (!*e) break;
            p = e + 1; ln++;
        }
        if (focus && (int)cl >= (int)w->ta_scroll && (int)cl < (int)w->ta_scroll + vis) {
            /* caret x from the caret column */
            const char *ls = w->buf;
            uint32_t k = 0;
            for (uint32_t i = 0; i < w->buf_len && k < cl; ++i) if (w->buf[i] == '\n') { k++; ls = w->buf + i + 1; }
            char cbuf[512]; uint32_t cn = cc < sizeof(cbuf) - 1 ? cc : sizeof(cbuf) - 1;
            memcpy(cbuf, ls, cn); cbuf[cn] = '\0';
            int caret_x = x + pad + text_w(cbuf, 14);
            sfill(s, caret_x, y + pad + (int)(cl - w->ta_scroll) * lh, 1, lh, app->c_accent);
        }
        break;
    }
    }
}

/* ------------------------------------------------------------- events */

static imui_widget_t *hit(imui_widget_t *w, int x, int y) {
    if (x < w->rx || x >= w->rx + (int)w->rw || y < w->ry || y >= w->ry + (int)w->rh)
        return NULL;
    for (int i = (int)w->child_count - 1; i >= 0; --i) {
        imui_widget_t *h = hit(w->children[i], x, y);
        if (h) return h;
    }
    if (w->kind == IMUI_ROW || w->kind == IMUI_COL || w->kind == IMUI_SPACER ||
        w->kind == IMUI_DIVIDER || w->kind == IMUI_LABEL)
        return NULL;
    return w;
}

static void clear_hover(imui_widget_t *w) {
    w->hover = w->pressed = false;
    for (uint32_t i = 0; i < w->child_count; ++i) clear_hover(w->children[i]);
}

static void ta_insert(imui_widget_t *w, const char *ins, uint32_t n) {
    if (w->buf_len + n + 1 > w->buf_cap) {
        uint32_t nc = w->buf_cap + n + 2048;
        w->buf = imui_realloc_sized(w->buf, w->buf_cap, nc);
        w->buf_cap = nc;
    }
    memmove(w->buf + w->caret + n, w->buf + w->caret, w->buf_len - w->caret + 1);
    memcpy(w->buf + w->caret, ins, n);
    w->buf_len += n; w->caret += n;
}
static void ta_backspace(imui_widget_t *w) {
    if (w->caret == 0) return;
    memmove(w->buf + w->caret - 1, w->buf + w->caret, w->buf_len - w->caret + 1);
    w->caret--; w->buf_len--;
}

static void handle_key(imui_app_t *app, const input_keyboard_event_t *ev) {
    imui_widget_t *f = app->focus;
    if (!f) return;
    if (f->kind == IMUI_TEXTBOX) {
        if (ev->ascii == '\r' || ev->ascii == '\n') {
            if (f->on_submit) f->on_submit(app, f, f->user);
        } else if (ev->ascii == 8 || ev->ascii == 127) {
            if (f->caret > 0) {
                memmove(f->buf + f->caret - 1, f->buf + f->caret, f->buf_len - f->caret + 1);
                f->caret--; f->buf_len--;
                if (f->on_change) f->on_change(app, f, f->user);
            }
        } else if (ev->keycode == 0x4B && f->caret > 0) { f->caret--; }       /* left */
        else if (ev->keycode == 0x4D && f->caret < f->buf_len) { f->caret++; } /* right */
        else if (ev->ascii >= 32 && ev->ascii < 127 && f->buf_len + 1 < f->buf_cap) {
            memmove(f->buf + f->caret + 1, f->buf + f->caret, f->buf_len - f->caret + 1);
            f->buf[f->caret++] = (char)ev->ascii; f->buf_len++;
            if (f->on_change) f->on_change(app, f, f->user);
        }
        app->needs_paint = true;
    } else if (f->kind == IMUI_TEXTAREA) {
        char c = (char)ev->ascii;
        if (c == '\r' || c == '\n') ta_insert(f, "\n", 1);
        else if (c == 8 || c == 127) ta_backspace(f);
        else if (ev->keycode == 0x4B) { if (f->caret > 0) f->caret--; }      /* left */
        else if (ev->keycode == 0x4D) { if (f->caret < f->buf_len) f->caret++; } /* right */
        else if (ev->keycode == 0x48) {                                       /* up */
            uint32_t bol = f->caret; while (bol > 0 && f->buf[bol - 1] != '\n') bol--;
            uint32_t col = f->caret - bol;
            if (bol > 0) {
                uint32_t pbol = bol - 1; while (pbol > 0 && f->buf[pbol - 1] != '\n') pbol--;
                uint32_t plen = bol - 1 - pbol;
                f->caret = pbol + (col < plen ? col : plen);
            }
        } else if (ev->keycode == 0x50) {                                     /* down */
            uint32_t eol = f->caret; while (eol < f->buf_len && f->buf[eol] != '\n') eol++;
            uint32_t bol = f->caret; while (bol > 0 && f->buf[bol - 1] != '\n') bol--;
            uint32_t col = f->caret - bol;
            if (eol < f->buf_len) {
                uint32_t nbol = eol + 1, nEol = nbol;
                while (nEol < f->buf_len && f->buf[nEol] != '\n') nEol++;
                uint32_t nlen = nEol - nbol;
                f->caret = nbol + (col < nlen ? col : nlen);
            }
        } else if (c >= 32 && c < 127) { char cc = c; ta_insert(f, &cc, 1); }
        else if (c == '\t') ta_insert(f, "    ", 4);
        if (f->on_change) f->on_change(app, f, f->user);
        app->needs_paint = true;
    } else if (f->kind == IMUI_LIST) {
        if (ev->keycode == 0x48 && f->sel > 0) f->sel--;                      /* up */
        else if (ev->keycode == 0x50 && f->sel + 1 < (int)f->row_count) f->sel++; /* down */
        else if ((ev->ascii == '\r' || ev->ascii == '\n') && f->sel >= 0 && f->on_activate)
            f->on_activate(app, f, f->user);
        app->needs_paint = true;
    }
}

int imui_run(imui_app_t *app) {
    if (!app || !app->fb) return -1;
    int prev_mx = -1, prev_my = -1, prev_btn = 0;

    while (app->running) {
        /* window closed by the WM? */
        uint32_t rx, ry, rw, rh;
        if (window_get_rect(app->win, &rx, &ry, &rw, &rh) < 0) { app->running = false; break; }
        if ((rw && rw != app->fb_w) || (rh && rh != app->fb_h)) {
            uint32_t bw = 0, bh = 0;
            uint32_t *nfb = window_get_backing_store(app->win, &bw, &bh);
            if (nfb) { app->fb = nfb; app->fb_w = bw; app->fb_h = bh; }
            app->needs_layout = app->needs_paint = true;
        }

        input_mouse_event_t me;
        bool mouse_moved = false;
        while (window_input_mouse_poll(&me) > 0) {
            int mx = me.x, my = me.y;
            int btn = (me.buttons & INPUT_MOUSE_BTN_LEFT) ? 1 : 0;
            imui_widget_t *h = hit(app->root, mx, my);
            clear_hover(app->root);
            if (h) h->hover = true;
            if (btn && !prev_btn) {          /* press */
                if (h) {
                    h->pressed = true;
                    if (h->kind == IMUI_TEXTBOX || h->kind == IMUI_TEXTAREA || h->kind == IMUI_LIST)
                        app->focus = h;
                    else
                        app->focus = NULL;
                    if (h->kind == IMUI_LIST) {
                        int rh2 = 40;
                        int idx = (int)h->scroll + (my - (h->ry + 4)) / rh2;
                        if (idx >= 0 && idx < (int)h->row_count) {
                            static uint64_t last_ms; static int last_idx;
                            uint64_t now = get_uptime_ms();
                            bool dbl = (idx == last_idx && now - last_ms < 350);
                            h->sel = idx;
                            last_ms = now; last_idx = idx;
                            if (dbl && h->on_activate) h->on_activate(app, h, h->user);
                        }
                    }
                }
            } else if (!btn && prev_btn) {   /* release -> click */
                if (h && h->pressed && (h->kind == IMUI_BUTTON || h->kind == IMUI_ICONBUTTON)
                    && h->on_click)
                    h->on_click(app, h, h->user);
                clear_hover(app->root);
                if (h) h->hover = true;
            }
            prev_btn = btn;
            if (mx != prev_mx || my != prev_my) mouse_moved = true;
            prev_mx = mx; prev_my = my;
        }
        if (mouse_moved) app->needs_paint = true;

        input_keyboard_event_t ke;
        while (window_input_keyboard_poll(&ke) > 0) {
            if (ke.pressed) handle_key(app, &ke);
        }

        if (app->needs_layout) {
            layout(app, app->root, 0, 0, (int)app->fb_w, (int)app->fb_h);
            app->needs_layout = false;
            app->needs_paint = true;
        }
        if (app->needs_paint) {
            surf_t s = { app->fb, (int)app->fb_w, (int)app->fb_h };
            sfill(&s, 0, 0, s.w, s.h, app->c_bg);
            paint(app, &s, app->root);
            window_damage(app->win, 0, 0, app->fb_w, app->fb_h);
            app->needs_paint = false;
        }
        sleep_ms(16);
    }
    return 0;
}

/* ImUI -- retained-mode widget toolkit for ImplusOS apps. See ImUI.h. */

#include "ImUI.h"

#include "Graphics.h"
#include "Window.h"
#include "Input.h"
#include "File.h"
#include "Process.h"

#include "XMLParser.h"

/* The keypad scan codes 0x47..0x53 look like the navigation keys, but the
 * kernel sends the real arrows as KEY_UP 0xE048, KEY_DOWN 0xE050 and so on.
 * Comparing against the bare codes meant no list or caret ever moved. */
#include "../../../Kernel/Source/include/kernel/keycodes.h"

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
#define STBI_REALLOC(p, z)          imui_realloc_sized(p, 0, z)
#define STBI_REALLOC_SIZED(p, o, z) imui_realloc_sized(p, o, z)
#define STBI_FREE(p)                free(p)
#include "Header/stb_image.h"
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

/* M3 list-item height: 56px carries a line of supporting text, which is
 * what most of these rows have. Shared by the painter and the hit test --
 * they drifted apart once, and a click then selected the wrong row. */
#define IMUI_LIST_ROW_H 56

/* ------------------------------------------------------- M3 shapes */

/* An M3 shape with independent corners, in the order M3 writes them:
 * top-left, top-right, bottom-right, bottom-left. A filled text field is
 * round at the top and square at the bottom, which is the whole reason
 * this exists. */
static void sround4(surf_t *s, int x, int y, int w, int h,
                    int tl, int tr, int br, int bl, uint32_t c) {
    int lim = (w < h ? w : h) / 2;
    if (tl > lim) tl = lim;
    if (tr > lim) tr = lim;
    if (br > lim) br = lim;
    if (bl > lim) bl = lim;
    for (int j = 0; j < h; ++j) {
        int cut_l = 0, cut_r = 0;
        for (int pass = 0; pass < 2; ++pass) {
            int r = pass == 0 ? (j < h / 2 ? tl : bl) : (j < h / 2 ? tr : br);
            if (r <= 0) continue;
            int dy = j < h / 2 ? r - 1 - j : j - (h - r);
            if (dy < 0) continue;
            int cut = r;
            for (int i = 0; i < r; ++i) {
                int dx = r - 1 - i;
                if (dx * dx + dy * dy <= r * r) { cut = i; break; }
            }
            if (pass == 0) cut_l = cut; else cut_r = cut;
        }
        if (cut_l + cut_r >= w) continue;
        sfill(s, x + cut_l, y + j, w - cut_l - cut_r, 1, c);
    }
}

/* An M3 outlined container: the outline shape with the fill laid back
 * inside it, which is a ring without needing a second coverage pass. */
static void soutlined(surf_t *s, int x, int y, int w, int h, int r,
                      uint32_t fill, uint32_t outline, int thickness) {
    sround(s, x, y, w, h, r, outline);
    if (w > thickness * 2 && h > thickness * 2) {
        sround(s, x + thickness, y + thickness, w - thickness * 2,
               h - thickness * 2, r > thickness ? r - thickness : 0, fill);
    }
}

/* The shortest-side radius of an M3 "full" (stadium) shape. */
static int sfull(int w, int h) { return (w < h ? w : h) / 2; }

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
/*
 * Icons are the shared external Material Design assets the window manager
 * ships in Resource/Icons/md/ (white RGBA PNGs); ImUI loads them once and
 * tints at blit time.
 */
#define IMUI_ICON_DIR "/Userland/com.ImplusOS.windowmanager/Resource/Icons/md/"

static const char *const imui_icon_file[] = {
    [IMUI_ICON_FOLDER]      = "folder",
    [IMUI_ICON_FOLDER_OPEN] = "folder_open",
    [IMUI_ICON_FILE]        = "description",
    [IMUI_ICON_DRIVE]       = "storage",
    [IMUI_ICON_BACK]        = "arrow_back",
    [IMUI_ICON_FORWARD]     = "arrow_forward",
    [IMUI_ICON_UP]          = "arrow_upward",
    [IMUI_ICON_HOME]        = "home",
    [IMUI_ICON_REFRESH]     = "refresh",
    [IMUI_ICON_NEW]         = "note_add",
    [IMUI_ICON_OPEN]        = "file_open",
    [IMUI_ICON_SAVE]        = "save",
    [IMUI_ICON_SEARCH]      = "search",
    [IMUI_ICON_TRASH]       = "delete",
    [IMUI_ICON_COPY]        = "content_copy",
    [IMUI_ICON_CUT]         = "content_cut",
    [IMUI_ICON_PASTE]       = "content_paste",
    [IMUI_ICON_EDIT]        = "edit",
    [IMUI_ICON_CHECK]       = "check",
    [IMUI_ICON_CLOSE]       = "close",
};
#define IMUI_ICON_COUNT ((int)(sizeof(imui_icon_file) / sizeof(imui_icon_file[0])))

typedef struct { uint32_t *px; int w, h; int tried; } imui_icon_cache_t;
static imui_icon_cache_t g_icon_cache[IMUI_ICON_COUNT];

static uint32_t *load_png_rgba(const char *path, int *w, int *h) {
    file_stat_t st;
    if (file_stat(path, &st) < 0 || !st.exists || st.is_dir ||
        st.size == 0 || st.size > 4u * 1024u * 1024u)
        return NULL;
    int32_t fd = file_open(path, 0);
    if (fd < 0) return NULL;
    uint8_t *enc = malloc(st.size);
    if (!enc) { file_close(fd); return NULL; }
    uint32_t got = 0;
    while (got < st.size) {
        int64_t n = file_read(fd, enc + got, st.size - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    file_close(fd);
    if (got != st.size) { free(enc); return NULL; }
    int c;
    uint8_t *rgba = stbi_load_from_memory(enc, (int)st.size, w, h, &c, 4);
    free(enc);
    if (!rgba) return NULL;
    uint32_t *out = malloc((size_t)(*w) * (size_t)(*h) * 4);
    if (!out) { stbi_image_free(rgba); return NULL; }
    for (int i = 0; i < (*w) * (*h); ++i)
        out[i] = ((uint32_t)rgba[i*4+3] << 24) | ((uint32_t)rgba[i*4] << 16) |
                 ((uint32_t)rgba[i*4+1] << 8) | rgba[i*4+2];
    stbi_image_free(rgba);
    return out;
}

static const imui_icon_cache_t *imui_icon_get(imui_icon_t k) {
    if ((int)k <= 0 || (int)k >= IMUI_ICON_COUNT || !imui_icon_file[k]) return NULL;
    imui_icon_cache_t *ic = &g_icon_cache[k];
    if (!ic->tried) {
        ic->tried = 1;
        char path[160];
        snprintf(path, sizeof(path), "%s%s.png", IMUI_ICON_DIR, imui_icon_file[k]);
        ic->px = load_png_rgba(path, &ic->w, &ic->h);
    }
    return ic->px ? ic : NULL;
}

static void imui_icon(surf_t *s, int x, int y, int sz, imui_icon_t k, uint32_t c) {
    const imui_icon_cache_t *ic = imui_icon_get(k);
    if (!ic || sz <= 0) return;
    uint32_t rgb = c & 0x00FFFFFFu;
    uint32_t ta = (c >> 24) ? (c >> 24) : 255u;
    int dn = sz > 1 ? sz - 1 : 1;
    for (int j = 0; j < sz; ++j) {
        uint32_t fy = (uint32_t)((int64_t)j * (ic->h - 1) * 65536 / dn);
        uint32_t y0 = fy >> 16, y1 = y0 + 1 < (uint32_t)ic->h ? y0 + 1 : y0;
        uint32_t wy = (fy >> 8) & 0xFF;
        for (int i = 0; i < sz; ++i) {
            uint32_t fx = (uint32_t)((int64_t)i * (ic->w - 1) * 65536 / dn);
            uint32_t x0 = fx >> 16, x1 = x0 + 1 < (uint32_t)ic->w ? x0 + 1 : x0;
            uint32_t wx = (fx >> 8) & 0xFF;
            uint32_t a00 = ic->px[y0*ic->w + x0] >> 24, a10 = ic->px[y0*ic->w + x1] >> 24;
            uint32_t a01 = ic->px[y1*ic->w + x0] >> 24, a11 = ic->px[y1*ic->w + x1] >> 24;
            uint32_t top = a00*(256-wx) + a10*wx;
            uint32_t bot = a01*(256-wx) + a11*wx;
            uint32_t sa = (top*(256-wy) + bot*wy) >> 16;
            if (!sa) continue;
            uint32_t a = sa * ta / 255u;
            if (a) sput(s, x + i, y + j, (a << 24) | rgb);
        }
    }
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
    w->icon = ic; w->on_click = cb; w->user = u; w->min_w = 40; w->min_h = 40;
    w_attach(p, w);
    return w;
}
imui_widget_t *imui_set_variant(imui_widget_t *b, imui_variant_t v) {
    if (b) b->variant = v;
    return b;
}
imui_widget_t *imui_set_single_click(imui_widget_t *l, bool enable) {
    if (l) l->single_click = enable;
    return l;
}
imui_widget_t *imui_set_type_role(imui_widget_t *l, imui_type_role_t r) {
    if (l) l->type_role = r;
    return l;
}
imui_widget_t *imui_spacer(imui_widget_t *p) { return imui_add(p, IMUI_SPACER, NULL); }
imui_widget_t *imui_divider(imui_widget_t *p) { return imui_add(p, IMUI_DIVIDER, NULL); }
imui_widget_t *imui_textbox(imui_widget_t *p, const char *t) {
    imui_widget_t *w = w_new(IMUI_TEXTBOX);
    w->buf_cap = 512; w->buf = calloc(1, w->buf_cap); w->editable = true; w->min_h = 56;
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
        const char *role = xml_get_attr(n, "role");
        if (role) {
            if (!strcmp(role, "headline")) w->type_role = IMUI_HEADLINE;
            else if (!strcmp(role, "title")) w->type_role = IMUI_TITLE;
            else if (!strcmp(role, "small")) w->type_role = IMUI_LABEL_SMALL;
        }
    } else if (!strcmp(tag, "button")) {
        w = imui_button(parent, n->text[0] ? n->text : xml_get_attr(n, "text"),
                        icon_by_name(xml_get_attr(n, "icon")), NULL, NULL);
        const char *variant = xml_get_attr(n, "variant");
        if (variant) {
            if (!strcmp(variant, "filled")) w->variant = IMUI_FILLED;
            else if (!strcmp(variant, "tonal")) w->variant = IMUI_TONAL;
            else if (!strcmp(variant, "outlined")) w->variant = IMUI_OUTLINED;
        }
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

    /* The desktop's own scheme, not a copy of its defaults: retinting the
     * shell retints every app built on this the next time it starts. */
    m3_load_desktop_theme(&app->color, &app->type);
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

static float label_size(imui_app_t *app, imui_type_role_t role) {
    switch (role) {
    case IMUI_HEADLINE:    return app->type.headline_small;
    case IMUI_TITLE:       return app->type.title_medium;
    case IMUI_LABEL_SMALL: return app->type.label_small;
    case IMUI_BODY:
    default:               return app->type.body_medium;
    }
}

/* M3 component heights: a button and an icon button are 40px, a filled
 * text field is 56px, and a line of body text needs its own size plus the
 * scale's leading. */
static int leaf_main(imui_app_t *app, imui_widget_t *w, bool horizontal) {
    switch (w->kind) {
    case IMUI_LABEL: {
        float px = label_size(app, w->type_role);
        return horizontal ? text_w(w->text, px) + 4 : (int)px + 10;
    }
    case IMUI_BUTTON:
        /* M3 asks for 24px of padding either side of a text button's
         * label, and room for a 18px leading icon plus its gap. */
        return horizontal ? text_w(w->text, app->type.label_large) +
                            (w->icon ? 66 : 48)
                          : 40;
    case IMUI_ICONBUTTON: return 40;
    case IMUI_DIVIDER: return 1;
    case IMUI_TEXTBOX: return horizontal ? 160 : 56;
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

    /* A non-growing child is worth leaf_main() *or* its min_h, whichever is
     * larger: a container reports 0 from leaf_main() and asks for its height
     * through min_h alone. Counting only the leaf size left the fixed total
     * short by every bar in the tree, so the growing sibling took the whole
     * axis and pushed the last bar -- a status bar -- past the edge. */
    int fixed = 0, grow_sum = 0;
    for (int i = 0; i < n; ++i) {
        imui_widget_t *c = w->children[i];
        if (c->grow > 0) grow_sum += (int)c->grow;
        else {
            int m = leaf_main(app, c, horiz);
            int cm = horiz ? (int)c->min_w : (int)c->min_h;
            if (m < cm) m = cm;
            fixed += m;
        }
    }

    /* Shrink to fit. A window narrowed past what its fixed children want
     * used to let them keep their full size and run off the end, which took
     * whatever was last in the row -- a status label, a text field -- off
     * the screen entirely. Scaling them down instead keeps everything
     * inside the container, tighter rather than missing. */
    int shrink_num = 1, shrink_den = 1;
    if (fixed > main_avail && fixed > 0) {
        shrink_num = main_avail > 0 ? main_avail : 0;
        shrink_den = fixed;
    }
    int flex = main_avail - (fixed * shrink_num) / shrink_den;
    if (flex < 0) flex = 0;

    int cur = horiz ? px : py;
    for (int i = 0; i < n; ++i) {
        imui_widget_t *c = w->children[i];
        int m;
        if (c->grow > 0 && grow_sum > 0) {
            m = flex * (int)c->grow / grow_sum;
            /* A minimum is a request, not a guarantee: honouring it in a
             * container too small for it is what pushes a sibling out. */
            int cm = horiz ? (int)c->min_w : (int)c->min_h;
            if (m < cm && cm <= main_avail) m = cm;
        } else {
            m = leaf_main(app, c, horiz);
            int cm = horiz ? (int)c->min_w : (int)c->min_h;
            if (m < cm) m = cm;
            m = (m * shrink_num) / shrink_den;
        }
        if (m < 0) m = 0;
        if (horiz)
            layout(app, c, cur, py, m, inner_h);
        else
            layout(app, c, px, cur, inner_w, m);
        cur += m + w->gap;
    }
}

/* ------------------------------------------------------------- paint */

/*
 * Every widget below is a Material Design 3 component: a shape from the M3
 * scale, a container from the scheme's roles, a state layer for what the
 * pointer is doing to it, and text from the M3 type scale. Nothing here
 * names a colour.
 */

/* The container a button sits on, so a state layer over it resolves
 * against the right tone. */
static uint32_t widget_backdrop(imui_app_t *app, imui_widget_t *w) {
    for (imui_widget_t *p = w->parent; p; p = p->parent) {
        if (p->kind != IMUI_ROW && p->kind != IMUI_COL) continue;
        if (!p->fill_bg) continue;
        return p->variant == IMUI_FILLED ? app->color.surface_container
                                         : app->color.surface;
    }
    return app->color.surface;
}

/* An M3 button's container and label, given its emphasis and what the
 * pointer is doing. */
static void button_roles(imui_app_t *app, imui_widget_t *w, uint32_t backdrop,
                         uint32_t *container, uint32_t *on_container,
                         bool *outlined) {
    const m3_scheme_t *m3 = &app->color;
    uint32_t state = w->pressed ? M3_STATE_PRESSED
                   : w->hover   ? M3_STATE_HOVER : 0u;
    *outlined = false;
    switch (w->variant) {
    case IMUI_FILLED:
        *container = m3_state_layer(m3->primary, m3->on_primary, state);
        *on_container = m3->on_primary;
        break;
    case IMUI_TONAL:
        *container = m3_state_layer(m3->secondary_container,
                                    m3->on_secondary_container, state);
        *on_container = m3->on_secondary_container;
        break;
    case IMUI_OUTLINED:
        *container = state ? m3_state_layer(backdrop, m3->primary, state)
                           : backdrop;
        *on_container = m3->primary;
        *outlined = true;
        break;
    case IMUI_TEXT:
    default:
        /* A text button has no container until it is touched; then the
         * state layer is the only thing that appears. */
        *container = state ? m3_state_layer(backdrop, m3->primary, state) : 0u;
        *on_container = m3->primary;
        break;
    }
}

static void paint(imui_app_t *app, surf_t *s, imui_widget_t *w) {
    const m3_scheme_t *m3 = &app->color;
    int x = w->rx, y = w->ry, cw = (int)w->rw, ch = (int)w->rh;

    switch (w->kind) {
    case IMUI_ROW: case IMUI_COL:
        /* A filled container is an M3 bar -- a toolbar or a status bar,
         * edge to edge on a surface-container tone. Anything else with a
         * background is an outlined card. */
        if (w->fill_bg && w->variant == IMUI_FILLED) {
            sfill(s, x, y, cw, ch, m3->surface_container);
            sfill(s, x, y + ch - 1, cw, 1, m3->outline_variant);
        } else if (w->fill_bg) {
            soutlined(s, x, y, cw, ch, M3_SHAPE_MEDIUM,
                      m3->surface, m3->outline_variant, 1);
        }
        for (uint32_t i = 0; i < w->child_count; ++i) paint(app, s, w->children[i]);
        break;
    case IMUI_LABEL: {
        float px = label_size(app, w->type_role);
        uint32_t color = w->type_role == IMUI_LABEL_SMALL ? m3->on_surface_variant
                                                          : m3->on_surface;
        draw_text(s, x + 2, y + (ch - (int)px - 3) / 2, w->text, px, color, cw - 4);
        break;
    }
    case IMUI_BUTTON: {
        uint32_t container, on_container;
        bool outlined;
        button_roles(app, w, widget_backdrop(app, w), &container, &on_container,
                     &outlined);
        int r = sfull(cw, ch);
        if (outlined)
            soutlined(s, x, y, cw, ch, r, container, m3->outline, 1);
        else if (container)
            sround(s, x, y, cw, ch, r, container);
        int tx = x + 16;
        if (w->icon) {
            imui_icon(s, x + 14, y + (ch - 18) / 2, 18, w->icon, on_container);
            tx = x + 40;
        }
        draw_text(s, tx, y + (ch - (int)app->type.label_large - 3) / 2, w->text,
                  app->type.label_large, on_container, cw - (tx - x) - 10);
        break;
    }
    case IMUI_ICONBUTTON: {
        /* M3 icon button: a full-shape state layer under the glyph, and
         * nothing at all when the pointer is elsewhere. */
        uint32_t state = w->pressed ? M3_STATE_PRESSED
                       : w->hover   ? M3_STATE_HOVER : 0u;
        if (state)
            sround(s, x, y, cw, ch, sfull(cw, ch),
                   m3_state_layer(widget_backdrop(app, w), m3->on_surface, state));
        imui_icon(s, x + (cw - 20) / 2, y + (ch - 20) / 2, 20, w->icon,
                  m3->on_surface_variant);
        break;
    }
    case IMUI_DIVIDER:
        if (cw > ch) sfill(s, x, y + ch / 2, cw, 1, m3->outline_variant);
        else sfill(s, x + cw / 2, y, 1, ch, m3->outline_variant);
        break;
    case IMUI_SPACER: break;
    case IMUI_TEXTBOX: {
        /* M3 filled text field: rounded at the top, square at the bottom,
         * with an indicator line that turns primary when it has focus. */
        bool focus = (app->focus == w);
        sround4(s, x, y, cw, ch, M3_SHAPE_EXTRA_SMALL, M3_SHAPE_EXTRA_SMALL,
                0, 0, m3->surface_container_highest);
        int indicator = focus ? 2 : 1;
        sfill(s, x, y + ch - indicator, cw, indicator,
              focus ? m3->primary : m3->on_surface_variant);
        float px = app->type.body_large;
        int ty = y + (ch - (int)px - 3) / 2;
        draw_text(s, x + 16, ty, w->buf, px,
                  w->buf_len ? m3->on_surface : m3->on_surface_variant, cw - 32);
        if (focus) {
            int caret_x = x + 16 + text_w_prefix(w->buf, w->caret, px);
            sfill(s, caret_x, ty - 2, 2, (int)px + 6, m3->primary);
        }
        break;
    }
    case IMUI_LIST: {
        soutlined(s, x, y, cw, ch, M3_SHAPE_MEDIUM,
                  m3->surface, m3->outline_variant, 1);
        int rh = IMUI_LIST_ROW_H;
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
            uint32_t on_row = m3->on_surface;
            uint32_t on_row_dim = m3->on_surface_variant;
            if (sel) {
                /* A selected list item is a filled container, the way M3
                 * marks one in a navigation drawer -- not a tinted wash
                 * with a bar stuck on the side. */
                sround(s, x + 4, ry + 2, cw - 8, rh - 4, sfull(cw - 8, rh - 4),
                       m3->secondary_container);
                on_row = on_row_dim = m3->on_secondary_container;
            }
            if (r->icon) imui_icon(s, x + 16, ry + (rh - 24) / 2, 24, r->icon, on_row);
            int tx = x + (r->icon ? 56 : 20);
            int avail = cw - (tx - x) - 16;
            if (r->subtext[0]) {
                draw_text(s, tx, ry + 10, r->text, app->type.body_large,
                          on_row, avail);
                draw_text(s, tx, ry + 10 + (int)app->type.body_large + 5,
                          r->subtext, app->type.body_small, on_row_dim, avail);
            } else {
                draw_text(s, tx, ry + (rh - (int)app->type.body_large - 3) / 2,
                          r->text, app->type.body_large, on_row, avail);
            }
        }
        if ((int)w->row_count > vis) {
            int track_h = ch - 8;
            int th = track_h * vis / (int)w->row_count; if (th < 24) th = 24;
            int ty = w->row_count > (uint32_t)vis
                   ? (track_h - th) * (int)w->scroll / ((int)w->row_count - vis) : 0;
            sround(s, x + cw - 8, y + 4 + ty, 4, th, 2, m3->outline);
        }
        break;
    }
    case IMUI_TEXTAREA: {
        bool focus = (app->focus == w);
        soutlined(s, x, y, cw, ch, M3_SHAPE_MEDIUM,
                  m3->surface_container_lowest,
                  focus ? m3->primary : m3->outline_variant, focus ? 2 : 1);
        float px = app->type.body_medium;
        int lh = (int)px + 6, pad = 12;
        int vis = (ch - 2 * pad) / lh;
        if (vis < 1) vis = 1;
        w->ta_vis = (uint32_t)vis;   /* page keys need a screenful; see handle_key */
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
                          lbuf, px, m3->on_surface, cw - 2 * pad);
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
            int caret_x = x + pad + text_w(cbuf, px);
            sfill(s, caret_x, y + pad + (int)(cl - w->ta_scroll) * lh, 2, lh, m3->primary);
        }
        break;
    }
    }
}

/* ------------------------------------------------------------- events */

/* How many list rows fit in `w` as it is laid out now. The painter works
 * this out too; both have to agree or the scroll limit is wrong. */
static int list_visible_rows(const imui_widget_t *w) {
    int vis = ((int)w->rh - 8) / IMUI_LIST_ROW_H;
    return vis > 0 ? vis : 1;
}

static void list_scroll_by(imui_widget_t *w, int delta) {
    int vis = list_visible_rows(w);
    int max_scroll = (int)w->row_count - vis;
    if (max_scroll < 0) max_scroll = 0;
    int next = (int)w->scroll + delta;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    w->scroll = (uint32_t)next;
}

/* Bring `sel` into view, moving the least that does it -- so arrowing off
 * the bottom of a list scrolls instead of losing the selection. */
static void list_scroll_to_selection(imui_widget_t *w) {
    if (w->sel < 0) return;
    int vis = list_visible_rows(w);
    if (w->sel < (int)w->scroll) w->scroll = (uint32_t)w->sel;
    else if (w->sel >= (int)w->scroll + vis)
        w->scroll = (uint32_t)(w->sel - vis + 1);
}

static void textarea_scroll_by(imui_widget_t *w, int delta) {
    uint32_t lines = 1u;
    for (uint32_t i = 0; i < w->buf_len; ++i) if (w->buf[i] == '\n') ++lines;
    int vis = (int)w->ta_vis > 0 ? (int)w->ta_vis : 1;
    int max_scroll = (int)lines - vis;
    if (max_scroll < 0) max_scroll = 0;
    int next = (int)w->ta_scroll + delta;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    w->ta_scroll = (uint32_t)next;
}

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
        } else if (!f->editable) {
            /* read-only: the caret still moves, nothing types */
            if (ev->keycode == KEY_LEFT && f->caret > 0) f->caret--;
            else if (ev->keycode == KEY_RIGHT && f->caret < f->buf_len) f->caret++;
        } else if (ev->ascii == 8 || ev->ascii == 127) {
            if (f->caret > 0) {
                memmove(f->buf + f->caret - 1, f->buf + f->caret, f->buf_len - f->caret + 1);
                f->caret--; f->buf_len--;
                if (f->on_change) f->on_change(app, f, f->user);
            }
        } else if (ev->keycode == KEY_LEFT && f->caret > 0) { f->caret--; }       /* left */
        else if (ev->keycode == KEY_RIGHT && f->caret < f->buf_len) { f->caret++; } /* right */
        else if (ev->ascii >= 32 && ev->ascii < 127 && f->buf_len + 1 < f->buf_cap) {
            memmove(f->buf + f->caret + 1, f->buf + f->caret, f->buf_len - f->caret + 1);
            f->buf[f->caret++] = (char)ev->ascii; f->buf_len++;
            if (f->on_change) f->on_change(app, f, f->user);
        }
        app->needs_paint = true;
    } else if (f->kind == IMUI_TEXTAREA) {
        char c = (char)ev->ascii;
        bool ed = f->editable;
        /* on_change means the buffer changed, not that a key was pressed:
         * without this the arrow keys counted as an edit and every app
         * using on_change (the editor's dirty flag above all) reported a
         * modification the moment the caret moved. */
        uint32_t before = f->buf_len;
        if (ed && (c == '\r' || c == '\n')) ta_insert(f, "\n", 1);
        else if (ed && (c == 8 || c == 127)) ta_backspace(f);
        else if (ev->keycode == KEY_LEFT) { if (f->caret > 0) f->caret--; }      /* left */
        else if (ev->keycode == KEY_RIGHT) { if (f->caret < f->buf_len) f->caret++; } /* right */
        else if (ev->keycode == KEY_UP) {                                       /* up */
            uint32_t bol = f->caret; while (bol > 0 && f->buf[bol - 1] != '\n') bol--;
            uint32_t col = f->caret - bol;
            if (bol > 0) {
                uint32_t pbol = bol - 1; while (pbol > 0 && f->buf[pbol - 1] != '\n') pbol--;
                uint32_t plen = bol - 1 - pbol;
                f->caret = pbol + (col < plen ? col : plen);
            }
        } else if (ev->keycode == KEY_PAGEUP || ev->keycode == KEY_PAGEDOWN) {   /* page up/down */
            /* A screenful is however many lines fit, which only the paint pass
             * knows (it has the pane height); it leaves the count in ta_vis.
             * Before the first paint there is none, so fall back to a
             * plausible one. */
            uint32_t step = f->ta_vis ? f->ta_vis : 16u;
            for (uint32_t i = 0; i < step; ++i) {
                if (ev->keycode == KEY_PAGEUP) {
                    if (f->caret == 0) break;
                    uint32_t b = f->caret; while (b > 0 && f->buf[b - 1] != '\n') b--;
                    f->caret = (b > 0) ? b - 1 : 0;
                } else {
                    uint32_t e2 = f->caret;
                    while (e2 < f->buf_len && f->buf[e2] != '\n') e2++;
                    if (e2 >= f->buf_len) { f->caret = f->buf_len; break; }
                    f->caret = e2 + 1;
                }
            }
        } else if (ev->keycode == KEY_DOWN) {                                    /* down */
            uint32_t eol = f->caret; while (eol < f->buf_len && f->buf[eol] != '\n') eol++;
            uint32_t bol = f->caret; while (bol > 0 && f->buf[bol - 1] != '\n') bol--;
            uint32_t col = f->caret - bol;
            if (eol < f->buf_len) {
                uint32_t nbol = eol + 1, nEol = nbol;
                while (nEol < f->buf_len && f->buf[nEol] != '\n') nEol++;
                uint32_t nlen = nEol - nbol;
                f->caret = nbol + (col < nlen ? col : nlen);
            }
        } else if (ed && c >= 32 && c < 127) { char cc = c; ta_insert(f, &cc, 1); }
        else if (ed && c == '\t') ta_insert(f, "    ", 4);
        if (ed && f->buf_len != before && f->on_change) f->on_change(app, f, f->user);
        app->needs_paint = true;
    } else if (f->kind == IMUI_LIST) {
        if (ev->keycode == KEY_UP && f->sel > 0) f->sel--;                      /* up */
        else if (ev->keycode == KEY_DOWN && f->sel + 1 < (int)f->row_count) f->sel++; /* down */
        else if (ev->keycode == KEY_PAGEUP || ev->keycode == KEY_PAGEDOWN) {
            /* Page must move the selection, not just the viewport: a bare
             * scroll is undone straight afterwards by the
             * scroll-to-selection pass below, which is why PgUp/PgDn used
             * to leave the list exactly where it was. */
            int page = list_visible_rows(f);
            int next = (int)f->sel;
            if (next < 0) next = (ev->keycode == KEY_PAGEUP) ? 0 : page - 1;
            else next += (ev->keycode == KEY_PAGEUP) ? -page : page;
            if (next < 0) next = 0;
            if (next > (int)f->row_count - 1) next = (int)f->row_count - 1;
            f->sel = f->row_count ? next : -1;
        }
        else if (ev->keycode == KEY_HOME) { f->sel = f->row_count ? 0 : -1; }       /* Home */
        else if (ev->keycode == KEY_END && f->row_count)
            f->sel = (int)f->row_count - 1;                                     /* End */
        else if ((ev->ascii == '\r' || ev->ascii == '\n') && f->sel >= 0 && f->on_activate)
            f->on_activate(app, f, f->user);
        list_scroll_to_selection(f);
        app->needs_paint = true;
    }
}

int imui_run(imui_app_t *app) {
    if (!app || !app->fb) return -1;
    int prev_mx = -1, prev_my = -1, prev_btn = 0;

    while (app->running) {
        if (app->on_tick) app->on_tick(app, app->root, app->user);

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
                        int idx = (int)h->scroll +
                                  (my - (h->ry + 4)) / IMUI_LIST_ROW_H;
                        if (idx >= 0 && idx < (int)h->row_count) {
                            uint64_t now = get_uptime_ms();
                            bool dbl = (idx == h->dbl_idx &&
                                        now - h->dbl_ms < 350u);
                            h->sel = idx;
                            h->dbl_ms = now; h->dbl_idx = idx;
                            /* A list of files opens on a double click; a
                             * list of settings acts on the first one. */
                            if ((dbl || h->single_click) && h->on_activate)
                                h->on_activate(app, h, h->user);
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
            /* The wheel scrolls whatever it is over. Without this the only
             * way down a list was its scrollbar, which is not draggable
             * either -- so anything past the first screenful of a list was
             * simply unreachable. */
            if (me.wheel != 0) {
                imui_widget_t *target = h;
                while (target && target->kind != IMUI_LIST &&
                       target->kind != IMUI_TEXTAREA)
                    target = target->parent;
                if (target) {
                    int step = me.wheel > 0 ? -3 : 3;
                    if (target->kind == IMUI_LIST)
                        list_scroll_by(target, step);
                    else
                        textarea_scroll_by(target, step);
                    app->needs_paint = true;
                }
            }
            prev_btn = btn;
            if (mx != prev_mx || my != prev_my) mouse_moved = true;
            prev_mx = mx; prev_my = my;
        }
        if (mouse_moved) app->needs_paint = true;

        input_keyboard_event_t ke;
        while (window_input_keyboard_poll(&ke) > 0) {
            if (!ke.pressed) continue;
            /* App shortcuts first: they have to work with the focus
             * anywhere, including on a widget that would otherwise swallow
             * the key (Ctrl+S while the caret sits in the text). */
            if (app->on_key && app->on_key(app, &ke)) {
                app->needs_paint = true;
                continue;
            }
            handle_key(app, &ke);
        }

        if (app->needs_layout) {
            layout(app, app->root, 0, 0, (int)app->fb_w, (int)app->fb_h);
            app->needs_layout = false;
            app->needs_paint = true;
        }
        if (app->needs_paint) {
            surf_t s = { app->fb, (int)app->fb_w, (int)app->fb_h };
            sfill(&s, 0, 0, s.w, s.h, app->color.surface);
            paint(app, &s, app->root);
            window_damage(app->win, 0, 0, app->fb_w, app->fb_h);
            app->needs_paint = false;
        }
        sleep_ms(16);
    }
    return 0;
}

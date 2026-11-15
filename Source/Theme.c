/* ImplusOS desktop theme. See Theme.h. */

#include "Theme.h"

#include "File.h"

#include <stdlib.h>
#include <string.h>

/* Applied in this order, each over the last -- the same as the window
 * manager (Core/WM_Main.c): the shipped theme, then the user's choice. */
static const char *const k_theme_paths[] = {
    "/Userland/com.ImplusOS.windowmanager/Resource/Themes/Default.theme",
    "/var/System/shell.theme",
};

uint32_t color_mix(uint32_t a, uint32_t b, uint32_t percent)
{
    if (percent > 100u) percent = 100u;
    uint32_t out = 0xFF000000u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca * (100u - percent) + cb * percent + 50u) / 100u) << shift;
    }
    return out;
}

uint32_t color_alpha(uint32_t argb, uint32_t percent)
{
    if (percent > 100u) percent = 100u;
    return (argb & 0x00FFFFFFu) | (((255u * percent + 50u) / 100u) << 24);
}

/* Rec. 601 luma, 0..255. */
static uint32_t luma(uint32_t c)
{
    return (((c >> 16) & 0xFFu) * 299u + ((c >> 8) & 0xFFu) * 587u +
            (c & 0xFFu) * 114u) / 1000u;
}

void theme_init(theme_t *t, uint32_t accent, bool dark)
{
    if (!t) return;
    accent |= 0xFF000000u;
    memset(t, 0, sizeof(*t));
    t->dark = dark;
    if (dark) {
        t->bg          = 0xFF17171Au;
        t->surface     = 0xFF212125u;
        t->surface_alt = 0xFF2C2C31u;
        t->border      = 0xFF3F3F46u;
        t->text        = 0xFFECECEFu;
        t->text_dim    = 0xFFA5A5AFu;
        t->danger      = 0xFFF2706Bu;
        t->on_danger   = 0xFF2A0705u;
        /* A deep accent disappears on a dark surface; lift it. */
        if (luma(accent) < 110u) accent = color_mix(accent, 0xFFFFFFFFu, 40u);
    } else {
        t->bg          = 0xFFF4F4F6u;
        t->surface     = 0xFFFFFFFFu;
        t->surface_alt = 0xFFEBEBEFu;
        t->border      = 0xFFD3D3DAu;
        t->text        = 0xFF1C1C20u;
        t->text_dim    = 0xFF5E5E68u;
        t->danger      = 0xFFC62828u;
        t->on_danger   = 0xFFFFFFFFu;
    }
    t->accent      = accent;
    t->on_accent   = luma(accent) > 150u ? 0xFF111114u : 0xFFFFFFFFu;
    t->accent_soft = color_mix(t->surface, accent, dark ? 30u : 16u);
}

uint32_t theme_parse_color(const char *text, uint32_t fallback)
{
    if (!text) return fallback;
    if (text[0] == '#') ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;

    uint32_t value = 0u, digits = 0u;
    for (; *text; ++text) {
        uint32_t d;
        if (*text >= '0' && *text <= '9') d = (uint32_t)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') d = (uint32_t)(*text - 'a') + 10u;
        else if (*text >= 'A' && *text <= 'F') d = (uint32_t)(*text - 'A') + 10u;
        else break;
        if (++digits > 8u) return fallback;
        value = (value << 4) | d;
    }
    if (digits == 6u) return value | 0xFF000000u;
    return digits == 8u ? value : fallback;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n > 0u && (s[n - 1u] == ' ' || s[n - 1u] == '\t' || s[n - 1u] == '\r'))
        s[--n] = '\0';
    return s;
}

bool theme_read_file(const char *path, uint32_t *accent, bool *dark)
{
    file_stat_t st;
    if (!path || file_stat(path, &st) < 0 || !st.exists || st.is_dir ||
        st.size == 0u || st.size > 65536u)
        return false;
    int32_t fd = file_open(path, 0);
    if (fd < 0) return false;
    char *buf = (char *)malloc((size_t)st.size + 1u);
    if (!buf) { file_close(fd); return false; }
    int64_t got = file_read(fd, buf, st.size);
    file_close(fd);
    if (got <= 0) { free(buf); return false; }
    buf[(size_t)got] = '\0';

    for (char *line = buf; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        char *eq = strchr(line, '=');
        if (eq && *line != ';' && *line != '#' && *line != '[') {
            *eq = '\0';
            const char *key = trim(line), *value = trim(eq + 1);
            if (strcmp(key, "accent") == 0 && accent)
                *accent = theme_parse_color(value, *accent) | 0xFF000000u;
            else if (strcmp(key, "mode") == 0 && dark)
                *dark = strcmp(value, "dark") == 0;
        }
        line = next;
    }
    free(buf);
    return true;
}

void theme_get_desktop(uint32_t *accent, bool *dark)
{
    uint32_t a = THEME_ACCENT_DEFAULT;
    bool d = false;
    for (uint32_t i = 0u; i < sizeof(k_theme_paths) / sizeof(k_theme_paths[0]); ++i)
        (void)theme_read_file(k_theme_paths[i], &a, &d);
    if (accent) *accent = a;
    if (dark) *dark = d;
}

void theme_load_desktop(theme_t *theme)
{
    uint32_t accent;
    bool dark;
    theme_get_desktop(&accent, &dark);
    theme_init(theme, accent, dark);
}

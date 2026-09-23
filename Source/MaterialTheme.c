/*
 * The filesystem half of the Material Design 3 token module: reading which
 * scheme the desktop is currently themed with, so an application matches
 * the shell it is running in.
 *
 * Kept apart from Material.c on purpose. Everything there is arithmetic
 * with no dependencies, which is what lets the palette generator be
 * unit-tested on the host against M3's published baseline; this file is the
 * only part that needs the File API.
 */

#include "Material.h"

#include "File.h"

#include <stdlib.h>
#include <string.h>

/* The same files, in the same order, the window manager reads
 * (com.ImplusOS.windowmanager/Core/WM_Main.c). An application therefore
 * follows a retinted desktop without being told about it. */
static const char *const k_theme_paths[] = {
    "/var/System/shell.theme",
    "/Userland/com.ImplusOS.windowmanager/Resource/Themes/Material.theme",
};

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r') ++text;
    size_t len = strlen(text);
    while (len > 0u &&
           (text[len - 1u] == ' ' || text[len - 1u] == '\t' ||
            text[len - 1u] == '\r' || text[len - 1u] == '\n')) {
        text[--len] = '\0';
    }
    return text;
}

static uint32_t parse_color(const char *text, uint32_t fallback)
{
    if (!text) return fallback;
    if (text[0] == '#') ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;

    uint32_t value = 0u;
    uint32_t digits = 0u;
    while (*text) {
        uint32_t digit;
        if (*text >= '0' && *text <= '9') digit = (uint32_t)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') digit = (uint32_t)(*text - 'a') + 10u;
        else if (*text >= 'A' && *text <= 'F') digit = (uint32_t)(*text - 'A') + 10u;
        else break;
        if (digits >= 8u) return fallback;
        value = (value << 4u) | digit;
        ++digits;
        ++text;
    }
    if (digits == 6u) value |= 0xFF000000u;
    return (digits == 6u || digits == 8u) ? value : fallback;
}

static float parse_scale(const char *text, float fallback)
{
    if (!text || !*text) return fallback;
    uint32_t whole = 0u, fraction = 0u, divisor = 1u;
    bool any = false;
    while (*text >= '0' && *text <= '9') {
        any = true;
        whole = whole * 10u + (uint32_t)(*text - '0');
        ++text;
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9' && divisor < 10000u) {
            fraction = fraction * 10u + (uint32_t)(*text - '0');
            divisor *= 10u;
            ++text;
        }
    }
    return any ? (float)whole + (float)fraction / (float)divisor : fallback;
}

/* Only the three keys the scheme is generated from. Per-role overrides in a
 * theme file are the shell's business; an application that reproduced them
 * would still not know what the shell did with them. */
static bool read_generator(const char *path, uint32_t *seed, bool *dark,
                           float *scale)
{
    file_stat_t stat;
    if (file_stat(path, &stat) < 0 || !stat.exists || stat.is_dir ||
        stat.size == 0u || stat.size > 65536u) return false;
    int32_t fd = file_open(path, 0);
    if (fd < 0) return false;

    char *buffer = (char *)malloc((size_t)stat.size + 1u);
    if (!buffer) { file_close(fd); return false; }
    int64_t read_bytes = file_read(fd, buffer, stat.size);
    file_close(fd);
    if (read_bytes <= 0) { free(buffer); return false; }
    buffer[(size_t)read_bytes] = '\0';

    char *line = buffer;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        char *clean = trim(line);
        if (*clean && *clean != ';' && *clean != '#' && *clean != '[') {
            char *equals = strchr(clean, '=');
            if (equals) {
                *equals = '\0';
                const char *key = trim(clean);
                const char *value = trim(equals + 1);
                if (strcmp(key, "seed") == 0)
                    *seed = parse_color(value, *seed) | 0xFF000000u;
                else if (strcmp(key, "mode") == 0)
                    *dark = strcmp(value, "dark") == 0 || strcmp(value, "Dark") == 0;
                else if (strcmp(key, "type_scale") == 0)
                    *scale = parse_scale(value, *scale);
            }
        }
        if (!next) break;
        line = next;
    }
    free(buffer);
    return true;
}

void m3_get_desktop_theme(uint32_t *seed, bool *dark)
{
    uint32_t resolved_seed = M3_SEED_BASELINE;
    bool resolved_dark = false;
    float scale = 1.0f;

    for (uint32_t i = 0u; i < sizeof(k_theme_paths) / sizeof(k_theme_paths[0]); ++i) {
        if (read_generator(k_theme_paths[i], &resolved_seed, &resolved_dark, &scale))
            break;
    }
    if (seed) *seed = resolved_seed;
    if (dark) *dark = resolved_dark;
}

void m3_load_desktop_theme(m3_scheme_t *scheme, m3_typescale_t *type)
{
    uint32_t seed = M3_SEED_BASELINE;
    bool dark = false;
    float scale = 1.0f;

    for (uint32_t i = 0u; i < sizeof(k_theme_paths) / sizeof(k_theme_paths[0]); ++i) {
        if (read_generator(k_theme_paths[i], &seed, &dark, &scale)) break;
    }

    if (scheme) m3_scheme_init(scheme, seed, dark);
    if (type) m3_typescale_init(type, scale);
}

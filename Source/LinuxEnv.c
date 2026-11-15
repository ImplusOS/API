#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "LinuxEnv.h"
#include "File.h"
#include "SystemInfo.h"

/*
 * See LinuxEnv.h. This file is a reader for environment files and nothing
 * else: the variables themselves, and the reasons each one exists, live in
 * the .env files the build stages into LINUX_ENV_DIR.
 */

/* Largest environment file read, and how deep @include may nest. */
#define LINUX_ENV_FILE_MAX   16384u
#define LINUX_ENV_INCLUDE_MAX 4u
/* How deep [if] blocks may nest within one file. */
#define LINUX_ENV_IF_MAX     8u
#define LINUX_ENV_PATH_MAX   256u

/* Length of the "NAME" part of a "NAME=VALUE" entry, or (uint32_t)-1 when
 * there is no '=' (or nothing before it) and so no key to speak of. */
static uint32_t linux_env_key_length(const char *entry)
{
    uint32_t i = 0;
    while (entry[i] != '\0' && entry[i] != '=') {
        ++i;
    }
    return (entry[i] == '=' && i > 0u) ? i : (uint32_t)-1;
}

static int32_t linux_env_same_key(const char *a, const char *b)
{
    uint32_t la = linux_env_key_length(a);
    uint32_t lb = linux_env_key_length(b);
    if (la == (uint32_t)-1 || la != lb) {
        return 0;
    }
    return (memcmp(a, b, (size_t)la) == 0) ? 1 : 0;
}

void linux_env_init(linux_env_t *env)
{
    if (env == NULL) {
        return;
    }
    env->count = 0u;
    env->entries[0] = NULL;
    env->text_used = 0u;
    env->accel3d = -1;
}

int32_t linux_env_add(linux_env_t *env, const char *entry)
{
    if (env == NULL || entry == NULL || env->count >= LINUX_ENV_MAX ||
        linux_env_key_length(entry) == (uint32_t)-1) {
        return 0;
    }
    for (uint32_t i = 0; i < env->count; ++i) {
        if (linux_env_same_key(env->entries[i], entry)) {
            return 0;
        }
    }
    env->entries[env->count++] = entry;
    env->entries[env->count] = NULL;
    return 1;
}

void linux_env_add_entries(linux_env_t *env, const char *const *entries)
{
    for (uint32_t i = 0; entries != NULL && entries[i] != NULL; ++i) {
        (void)linux_env_add(env, entries[i]);
    }
}

const char *const *linux_env_entries(const linux_env_t *env)
{
    return (env != NULL) ? env->entries : NULL;
}

/* ------------------------------------------------------------------ */
/* conditions                                                           */
/* ------------------------------------------------------------------ */

/* A ready VirtIO render node means Mesa can run the virgl Gallium driver
 * instead of llvmpipe. The kernel reports this through the graphics-info
 * syscall because scanout and rendering are independent roles: a plain
 * framebuffer can still own the panel while VirtIO serves
 * /dev/dri/renderD128. Asked once per linux_env_t. */
static int32_t linux_env_accel3d(linux_env_t *env)
{
    if (env->accel3d < 0) {
        system_graphics_info_t info;
        memset(&info, 0, sizeof(info));
        env->accel3d =
            (os_get_graphics_info(&info) >= 0 &&
             (info.acceleration_flags & SYSTEM_GRAPHICS_ACCEL_3D) != 0u) ? 1 : 0;
    }
    return env->accel3d;
}

/* Value of the condition named by [name, name+len). An unknown condition is
 * false, so a block guarded by it is skipped rather than half-applied. */
static int32_t linux_env_condition(linux_env_t *env, const char *name, uint32_t len)
{
    if (len == 7u && memcmp(name, "accel3d", 7u) == 0) {
        return linux_env_accel3d(env);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* parsing                                                              */
/* ------------------------------------------------------------------ */

static int32_t linux_env_load_file_depth(linux_env_t *env, const char *path,
                                         uint32_t depth);

static int32_t linux_env_load_profile_depth(linux_env_t *env, const char *name,
                                            uint32_t name_len, uint32_t depth)
{
    static const char prefix[] = LINUX_ENV_DIR "/";
    static const char suffix[] = ".env";
    char path[LINUX_ENV_PATH_MAX];
    uint32_t plen = (uint32_t)(sizeof prefix - 1u);
    uint32_t slen = (uint32_t)(sizeof suffix - 1u);

    if (name_len == 0u || plen + name_len + slen >= sizeof path) {
        return -1;
    }
    /* A profile name is a file name, never a path. */
    for (uint32_t i = 0; i < name_len; ++i) {
        if (name[i] == '/') {
            return -1;
        }
    }
    memcpy(path, prefix, plen);
    memcpy(path + plen, name, name_len);
    memcpy(path + plen + name_len, suffix, slen + 1u);
    return linux_env_load_file_depth(env, path, depth);
}

/* Copy one "NAME=VALUE" line into the text store and add it. */
static int32_t linux_env_add_line(linux_env_t *env, const char *line, uint32_t len)
{
    if (env->text_used + len + 1u > LINUX_ENV_TEXT_MAX) {
        return 0;
    }
    char *copy = &env->text[env->text_used];
    memcpy(copy, line, len);
    copy[len] = '\0';
    if (!linux_env_add(env, copy)) {
        return 0;     /* duplicate or full: the copy's space is reused */
    }
    env->text_used += len + 1u;
    return 1;
}

static int32_t linux_env_is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r') ? 1 : 0;
}

static int32_t linux_env_parse(linux_env_t *env, const char *text, uint32_t size,
                               uint32_t depth)
{
    /* active[i]: whether the lines of the i-th open [if] block are kept;
     * a block inside a skipped block is skipped whatever its condition. */
    int32_t active[LINUX_ENV_IF_MAX + 1u];
    uint32_t level = 0u;
    int32_t added = 0;
    active[0] = 1;

    uint32_t pos = 0u;
    while (pos < size) {
        uint32_t start = pos;
        while (pos < size && text[pos] != '\n') {
            ++pos;
        }
        uint32_t end = pos;
        ++pos;                                     /* past the '\n' */

        while (start < end && linux_env_is_space(text[start])) ++start;
        while (end > start && linux_env_is_space(text[end - 1u])) --end;
        if (start == end || text[start] == '#') {
            continue;
        }
        const char *line = &text[start];
        uint32_t len = end - start;

        if (line[0] == '[') {
            if (len == 7u && memcmp(line, "[endif]", 7u) == 0) {
                if (level > 0u) --level;
            } else if (len > 5u && memcmp(line, "[if ", 4u) == 0 &&
                       line[len - 1u] == ']' && level < LINUX_ENV_IF_MAX) {
                const char *cond = line + 4u;
                uint32_t clen = len - 5u;
                int32_t negate = 0;
                if (clen > 0u && cond[0] == '!') {
                    negate = 1;
                    ++cond;
                    --clen;
                }
                int32_t holds = linux_env_condition(env, cond, clen);
                if (negate) holds = !holds;
                active[level + 1u] = active[level] && holds;
                ++level;
            }
            continue;
        }
        if (!active[level]) {
            continue;
        }
        if (len > 9u && memcmp(line, "@include ", 9u) == 0) {
            const char *name = line + 9u;
            uint32_t nlen = len - 9u;
            while (nlen > 0u && linux_env_is_space(*name)) {
                ++name;
                --nlen;
            }
            int32_t n = linux_env_load_profile_depth(env, name, nlen, depth + 1u);
            if (n > 0) added += n;
            continue;
        }
        added += linux_env_add_line(env, line, len);
    }
    return added;
}

static int32_t linux_env_load_file_depth(linux_env_t *env, const char *path,
                                         uint32_t depth)
{
    if (env == NULL || path == NULL || depth > LINUX_ENV_INCLUDE_MAX) {
        return -1;
    }
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        return -1;
    }
    char buffer[LINUX_ENV_FILE_MAX];
    uint32_t size = 0u;
    while (size < LINUX_ENV_FILE_MAX) {
        int64_t n = file_read(fd, buffer + size, LINUX_ENV_FILE_MAX - size);
        if (n <= 0) break;
        size += (uint32_t)n;
    }
    (void)file_close(fd);
    return linux_env_parse(env, buffer, size, depth);
}

int32_t linux_env_load_file(linux_env_t *env, const char *path)
{
    return linux_env_load_file_depth(env, path, 0u);
}

int32_t linux_env_load_profile(linux_env_t *env, const char *name)
{
    if (env == NULL || name == NULL) {
        return -1;
    }
    return linux_env_load_profile_depth(env, name, (uint32_t)strlen(name), 0u);
}

int32_t linux_env_load_for_program(linux_env_t *env, const char *program_path)
{
    if (env == NULL || program_path == NULL) {
        return 0;
    }
    const char *base = strrchr(program_path, '/');
    base = (base != NULL) ? base + 1 : program_path;
    int32_t n = linux_env_load_profile(env, base);
    return (n > 0) ? n : 0;
}

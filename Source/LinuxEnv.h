#pragma once

#include <stdint.h>

/*
 * LinuxEnv -- the per-program half of a foreign program's environment.
 *
 * The kernel starts a Linux-ABI process with generic defaults only: identity,
 * locale, timezone, the XDG base directories, and the staged Linux runtime's
 * library search path (Kernel/Source/Core/process/ProcessManager_Create.c).
 * Everything that belongs to a particular program, toolkit or service is not
 * code at all: it is data, in environment files on the image, and this module
 * only reads them. Nothing here knows any program by name.
 *
 * Where the files are
 * -------------------
 * All of them sit in LINUX_ENV_DIR, named "<name>.env". Two kinds of name:
 *
 *   - a program profile is named after the basename of the ELF it is for
 *     ("/usr/bin/Xorg" -> "Xorg.env"); linux_env_load_for_program() finds it
 *     from the path being spawned, so a launcher never names it;
 *   - a role profile is named after what the caller is doing ("x11-client",
 *     "desktop"); linux_env_load_profile() loads it by that name.
 *
 * The build collects every Environment/<name>.env under Userland/API,
 * Userland/Application/<app> and Userland/Service/<service> into that one
 * directory (STAGE_LINUX_ENV in the top-level Makefile): an application ships
 * the profile for the program it launches beside its own sources, and adding
 * one is dropping a file there.
 *
 * File format
 * -----------
 *     # comment (whole line)
 *     NAME=VALUE              one variable; the line is taken verbatim
 *     @include <name>         load LINUX_ENV_DIR/<name>.env here
 *     [if <cond>] / [if !<cond>] ... [endif]
 *                             keep the lines in between only when <cond>
 *                             holds (or does not); blocks nest
 *
 * Conditions are facts about the machine, never about a program:
 *     accel3d    the kernel reports a usable 3D render node
 *
 * Whatever order things are loaded in, the first value given for a name wins
 * (see linux_env_add()), so a caller loads the most specific source first:
 * explicit entries, then the program profile, then role profiles.
 *
 *     linux_env_t env;
 *     linux_env_init(&env);
 *     linux_env_load_for_program(&env, path);
 *     linux_env_load_profile(&env, "x11-client");
 *     process_spawn_with_env(path, args, linux_env_entries(&env));
 */

#define LINUX_ENV_DIR "/etc/ImplusOS/linux-env"

/* Room for the entries plus the NULL terminator the syscall wants. */
#define LINUX_ENV_MAX 64
/* Storage for the text of entries read from files. */
#define LINUX_ENV_TEXT_MAX 8192u

typedef struct {
    const char *entries[LINUX_ENV_MAX + 1u];
    uint32_t count;
    uint32_t text_used;
    int32_t accel3d;          /* -1 until first asked */
    char text[LINUX_ENV_TEXT_MAX];
} linux_env_t;

/* Empty the set (it is always NULL-terminated, so an initialised linux_env_t
 * with nothing added is a valid "no extra environment"). */
void linux_env_init(linux_env_t *env);

/* Append "NAME=VALUE". The string is referenced, not copied, so it must
 * outlive the spawn. An entry whose key is already present is ignored, so
 * the child never sees a variable twice (getenv() would take the first,
 * glibc's ld.so the last, and a duplicate would be a coin flip). Returns 1
 * when the entry was added, 0 when it was a duplicate or the set is full. */
int32_t linux_env_add(linux_env_t *env, const char *entry);

/* Append a NULL-terminated "NAME=VALUE" array (NULL is "nothing") with the
 * same first-one-wins rule. */
void linux_env_add_entries(linux_env_t *env, const char *const *entries);

/* The entries as the NULL-terminated array SYSCALL_PROCESS_SPAWN_ELF_ENV
 * expects. */
const char *const *linux_env_entries(const linux_env_t *env);

/* Read one environment file (format above). Returns the number of entries
 * it added, or a negative value when the file could not be read. */
int32_t linux_env_load_file(linux_env_t *env, const char *path);

/* linux_env_load_file(LINUX_ENV_DIR "/<name>.env"). */
int32_t linux_env_load_profile(linux_env_t *env, const char *name);

/* The program profile for the ELF at `program_path`, i.e. the profile named
 * after its basename. A program without one is not an error: returns 0. */
int32_t linux_env_load_for_program(linux_env_t *env, const char *program_path);

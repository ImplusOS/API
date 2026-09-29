#pragma once

#include <stdint.h>

/*
 * LinuxEnv -- the application- and service-specific half of a foreign
 * program's environment.
 *
 * The kernel starts a Linux-ABI process with generic defaults only: identity,
 * locale, timezone, the XDG base directories, and the staged Linux runtime's
 * library search path (Kernel/Source/Core/process/ProcessManager_Create.c,
 * which says why each of those is its business and not ours). Everything that
 * names an application, a toolkit or a service -- DISPLAY, LD_PRELOAD,
 * DOOMWADDIR, GDK_*, FONTCONFIG_*, ... -- is built here instead and handed to
 * the kernel with process_spawn_with_env(), i.e.
 * SYSCALL_PROCESS_SPAWN_ELF_ENV.
 *
 * That split is what keeps the kernel free of Doom's data directory, of Xorg's
 * libgbm workaround and of the dynmain service's library layout. A launcher
 * composes the groups it needs:
 *
 *     linux_env_t env;
 *     linux_env_init(&env);
 *     linux_env_add_x11(&env);          // DISPLAY, Mesa software rasteriser
 *     linux_env_add_doom(&env);         // plus the IWAD/soundfont/audio backend
 *     process_spawn_with_env(path, args, linux_env_entries(&env));
 *
 * Nothing here is read at run time by the child unless the launcher passes it
 * in: an entry only affects the one spawn it was added to.
 */

/* Room for the entries plus the NULL terminator the syscall wants. */
#define LINUX_ENV_MAX 64

typedef struct {
    const char *entries[LINUX_ENV_MAX + 1u];
    uint32_t count;
} linux_env_t;

/* Empty the set (it is always NULL-terminated, so an untouched linux_env_t is
 * a valid "no extra environment"). */
void linux_env_init(linux_env_t *env);

/* Append "NAME=VALUE". An entry whose key is already present is ignored, so
 * adding a group twice -- or adding two groups that overlap -- is harmless
 * and the child never sees a variable twice (getenv() would take the first,
 * glibc's ld.so the last, and a duplicate would be a coin flip). Returns 1
 * when the entry was added, 0 when it was a duplicate or the set is full. */
int32_t linux_env_add(linux_env_t *env, const char *entry);

/* The entries as the NULL-terminated array SYSCALL_PROCESS_SPAWN_ELF_ENV
 * expects. Never NULL: it is &env->entries[0], which init() has already
 * terminated. */
const char *const *linux_env_entries(const linux_env_t *env);

/* An X11 client on the display XSession brings up, drawing through Mesa's
 * software rasteriser. DISPLAY is not optional -- Xlib does not fall back to
 * :0 -- and LIBGL/GALLIUM/LP_NUM_THREADS are what keeps Mesa on llvmpipe with
 * one raster thread until there is a real GPU and the thread-handoff bug
 * behind M24 is found. XFILESEARCHPATH is where Xt (xterm) looks for the
 * app-defaults files. */
void linux_env_add_x11(linux_env_t *env);

/* The desktop stack a GTK/GLib/gdk-pixbuf program assumes: which backends to
 * try, where GSettings schemas and the gdk-pixbuf loader cache live, where
 * fontconfig's config is, and a D-Bus address that refuses immediately so
 * GDBus does not try to autolaunch a bus this OS does not have. Harmless to
 * anything that is not GTK (Chromium and xterm both ignore it). */
void linux_env_add_desktop(linux_env_t *env);

/* The X *server* only: preload libgbm before modesetting_drv.so is dlopen()ed
 * (it needs gbm_* but does not list it in DT_NEEDED), and bind eagerly so a
 * broken lazy PLT fixup in libglx cannot fault mid-message. */
void linux_env_add_xorg_server(linux_env_t *env);

/* Doom: where its IWAD and soundfont are, and the null/dummy audio backends
 * that keep libopenal from aborting on a device this OS does not have. */
void linux_env_add_doom(linux_env_t *env);

/* The in-tree test ld.so (com.ImplusOS.dynmain / com.ImplusOS.ldso): where it
 * finds its libraries and the preload shim it runs the test app under. Only a
 * launcher of an ELF whose PT_INTERP is that ld.so should add this -- glibc's
 * loader rejects both variables' values as written here. */
void linux_env_add_implus_ldso(linux_env_t *env);

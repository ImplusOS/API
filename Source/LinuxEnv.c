#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "LinuxEnv.h"

/*
 * See LinuxEnv.h. Every explanation that used to sit next to an entry in the
 * kernel's glibc_envp table moved here with the entry it belongs to; the
 * kernel keeps only the generic defaults (ProcessManager_Create.c).
 */

/* Length of the "NAME" part of a "NAME=VALUE" entry, or (uint32_t)-1 when
 * there is no '=' and so no key to speak of. */
static uint32_t linux_env_key_length(const char *entry)
{
    uint32_t i = 0;
    while (entry[i] != '\0' && entry[i] != '=') {
        ++i;
    }
    return (entry[i] == '=') ? i : (uint32_t)-1;
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
}

int32_t linux_env_add(linux_env_t *env, const char *entry)
{
    if (env == NULL || entry == NULL || env->count >= LINUX_ENV_MAX) {
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

const char *const *linux_env_entries(const linux_env_t *env)
{
    return (env != NULL) ? env->entries : NULL;
}

static void linux_env_add_list(linux_env_t *env, const char *const *list)
{
    for (uint32_t i = 0; list[i] != NULL; ++i) {
        (void)linux_env_add(env, list[i]);
    }
}

void linux_env_add_x11(linux_env_t *env)
{
    static const char *const group[] = {
        /* Keep the runtime path explicit in every foreign GUI launch.  The
         * kernel also supplies this for the glibc PT_INTERP case, but the
         * loader must receive it in the initial envp: Xorg/GTK invoke the
         * dynamic linker before their own code can repair a missing path. */
        "LD_LIBRARY_PATH=/lib64:/usr/lib/x86_64-linux-gnu:/usr/lib",
        /* The server XSession brought up (or joined) listens on the Unix
         * socket /tmp/.X11-unix/X0. Xlib does not fall back to ":0": with no
         * DISPLAY in the environment, XOpenDisplay(NULL) hands a NULL display
         * name to xcb_connect(), which returns a connection already in the
         * error state, so every X client dies at startup with "Couldn't
         * connect to display!". */
        "DISPLAY=:0",
        /* Mesa must rasterise in software: the Xorg we spawn runs modesetting
         * on /dev/dri/card0 (the kernel's KMS shim) with no GPU behind it, so
         * llvmpipe is the only driver that can draw at all. */
        "LIBGL_ALWAYS_SOFTWARE=1",
        "GALLIUM_DRIVER=llvmpipe",
        /* ...and which DRI driver Mesa binds is decided before any of that:
         * loader_get_driver_for_fd() returns MESA_LOADER_DRIVER_OVERRIDE
         * unconditionally (it is checked first, ahead of LIBGL_ALWAYS_SOFTWARE
         * and ahead of the PCI-id lookup), and "kms_swrast" is the one that
         * combines dumb-buffer KMS with software rasterisation -- exactly what
         * the kernel's DRM shim implements. This sits in the *X-client* group
         * rather than the X-server one because the PCI-id fallback would
         * otherwise pick "virtio_gpu" (the display adapter's real vendor/device
         * id) for any Mesa process launched without the server's environment,
         * and virtio_gpu_dri.so has no counterpart in the shim. Every client
         * path -- xsession_run_env(), the gtk3 demo, Chromium -- goes through
         * here. */
        "MESA_LOADER_DRIVER_OVERRIDE=kms_swrast",
        /* llvmpipe's scene/command-handoff worker threads race the main
         * thread's JIT'd raster kernel under our SMP scheduler; the torn read
         * showed up as a #GP on a user-mode deref of garbage rax at the first
         * frame (TODO_Doom_Xorg_MethodA.md M24). Force one raster thread for
         * now. TODO: find the real thread-handoff bug.
         *
         * NOTE: a second "GALLIUM_DRIVER=softpipe" used to sit below this and
         * never took effect -- getenv() returns the first match -- so it read
         * as if softpipe had been tried and ruled out. linux_env_add() now
         * refuses a duplicate key outright. */
        "LP_NUM_THREADS=1",
        /* The X core keyboard needs xkb-data's rules root; xkbcomp is run
         * against it by the server at activation time. */
        "XKB_CONFIG_ROOT=/usr/share/X11/xkb",
        /* Where Xt (xterm) looks for app-defaults resource files.
         * stage-xterm.sh installs them. */
        "XFILESEARCHPATH=/etc/X11/app-defaults/%N:/usr/share/X11/app-defaults/%N",
        NULL,
    };
    linux_env_add_list(env, group);
}

void linux_env_add_desktop(linux_env_t *env)
{
    static const char *const group[] = {
        /* GTK3: try the Wayland backend, then X11. Whether either exists is
         * the launcher's problem; naming the order is not the kernel's.
         * Chromium's Ozone ignores GDK_BACKEND entirely. */
        "GDK_BACKEND=wayland,x11",
        /* GSettings has no writable dconf backend here, so look the schemas
         * up in the image and answer every read from memory. Without a named
         * schema directory gtk_init() has no schemas at all. */
        "GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas",
        "GSETTINGS_BACKEND=memory",
        /* There is no D-Bus on ImplusOS, and saying so is not optional.
         * Unset, GDBus treats a session bus as merely "not started yet" and
         * autolaunches one: it reads /etc/machine-id and posix_spawn()s
         * dbus-launch, which is not on the image. GTK3's startup reaches that
         * path twice (GApplication registration, then the atk-bridge a11y
         * module) and a GTK3 client got as far as binding every Wayland
         * global and then stopped dead, never creating an window. An address
         * that cannot be connected to makes the same call fail immediately
         * instead, which both callers handle. NO_AT_BRIDGE keeps atk-bridge
         * out of the process altogether.
         * Docs/Others/TODO_GTK3_Wayland_LinuxABI.md G5 (W2'). */
        "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent",
        "NO_AT_BRIDGE=1",
        "FONTCONFIG_PATH=/etc/fonts",
        "FONTCONFIG_FILE=/etc/fonts/fonts.conf",
        /* gdk-pixbuf finds its loaders through a cache file whose path is
         * compiled into the library. Name it (and the loader directory)
         * explicitly so the image's layout is what is used rather than
         * whatever the library was built against. Without a usable cache GTK3
         * has no image loaders at all and aborts on the first icon it draws:
         *   Gtk:ERROR:gtkiconhelper.c:495: Failed to load
         *   .../image-missing.png: Unrecognized image file format */
        "GDK_PIXBUF_MODULE_FILE=/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache",
        "GDK_PIXBUF_MODULEDIR=/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders",
        NULL,
    };
    linux_env_add_list(env, group);
}

void linux_env_add_xorg_server(linux_env_t *env)
{
    static const char *const group[] = {
        /* Xorg's modesetting_drv.so has 12 undefined gbm_* symbols and does
         * NOT list libgbm.so.1 in DT_NEEDED -- on a stock distro it only
         * resolves them because AccelMethod "glamor" pulls in
         * libglamoregl.so (-> libgbm). Our xorg.conf forces AccelMethod
         * "none" (no GPU), so nothing loads libgbm and the driver fails
         * relocation ("undefined symbol: gbm_bo_get_plane_count", "No
         * drivers available"). Preload libgbm.so.1 so its symbols sit in the
         * global scope before modesetting_drv.so is dlopen()ed.
         * TODO_Doom_Xorg_MethodA.md M6 (7th boot).
         *
         * NOTE: glibc's ld.so keeps only the LAST LD_PRELOAD it sees, so a
         * second line here would silently drop libgbm and Xorg would die
         * with "undefined symbol: gbm_bo_get_plane_count". linux_env_add()
         * refuses a duplicate key for the same reason. */
        "LD_PRELOAD=libgbm.so.1",
        /* Mesa's loader checks MESA_LOADER_DRIVER_OVERRIDE before
         * LIBGL_ALWAYS_SOFTWARE.  Without this override LIBGL_ALWAYS_SOFTWARE=1
         * (set by linux_env_add_x11) makes loader_get_driver() return "swrast",
         * which is the pure software renderer with no KMS support — so
         * gbm_create_device() fails to create a screen and glamor reports
         * "couldn't get display device".  "kms_swrast" is the DRI driver that
         * combines dumb-buffer KMS with software rasterisation, which is
         * exactly what our DRM shim provides.  GALLIUM_DRIVER=llvmpipe (from
         * the x11 group) still selects llvmpipe for the actual rasteriser.
         * NOTE: linux_env_add_x11() now sets this too (for X clients, which
         * never get this group); the duplicate add here is refused and the
         * first -- identical -- value stands. */
        "MESA_LOADER_DRIVER_OVERRIDE=kms_swrast",
        /* Explicit GBM backend search path: Mesa's compile-time default may
         * differ from ImplusOS's layout, and gbm_create_device() dlopen()s
         * <path>/dri_gbm.so (or gbm_dri.so) from here. */
        "GBM_BACKENDS_PATH=/usr/lib/x86_64-linux-gnu/gbm",
        /* Bind every relocation at load time. Two reasons:
         * (1) boot 7 proved libglx.so resolves cleanly under eager binding
         *     but a *lazy* PLT fixup for one of its symbols dies at
         *     GlxExtensionInit() time with an un-printable name (the process
         *     faults mid-message) -- eager binding sidesteps the broken lazy
         *     path;
         * (2) with LD_WARN=1 a load-time relocation miss is reported by name
         *     and made non-fatal. LD_WARN itself was dropped: it demoted the
         *     unresolved R_X86_64_GLOB_DAT for `glxServer` to a warning and
         *     left the GOT slot 0, so libglx.so later did `call *0x38(NULL)`
         *     -> SIGSEGV at 0x38. Without it the miss is fatal AND named.
         * TODO_Doom_Xorg_MethodA.md M6/M7. */
        "LD_BIND_NOW=1",
        NULL,
    };
    linux_env_add_list(env, group);
}

void linux_env_add_doom(linux_env_t *env)
{
    static const char *const group[] = {
        /* Doom pulls libopenal (+ SDL2 via libfluidsynth). ImplusOS has no
         * PipeWire/PulseAudio/ALSA device; letting OpenAL-soft probe them
         * makes libpulse's pa_make_fd_cloexec() abort the process. Force the
         * null / dummy backends -- audio is silent, Doom runs. */
        "ALSOFT_DRIVERS=null",
        "SDL_AUDIODRIVER=dummy",
        "PULSE_SERVER=none",
        /* Doom locates its IWAD relative to DOOMWADDIR (else "."); its
         * soundfont from SOUNDFONT. Both data files live beside the binary
         * in /Userland/Doom/Resource -- which is also why spawn gives the
         * child the executable's own directory as its cwd. */
        "DOOMWADDIR=/Userland/Doom/Resource",
        "SOUNDFONT=/Userland/Doom/Resource/soundfont.sf2",
        NULL,
    };
    linux_env_add_list(env, group);
}

void linux_env_add_implus_ldso(linux_env_t *env)
{
    static const char *const group[] = {
        /* Where the in-tree ld.so looks for the libraries of the test ELF it
         * runs, and the preload shim that test goes through. The kernel used
         * to set both for every process whose PT_INTERP named an "ld-..."
         * interpreter -- which put a service's directory in the kernel -- so
         * it is now the launcher of that test ELF which says so, by adding
         * this group. glibc's loader must never see these values. */
        "LD_LIBRARY_PATH=/Userland/Service/com.ImplusOS.dynmain/lib",
        "LD_PRELOAD=libpreload.so",
        NULL,
    };
    linux_env_add_list(env, group);
}

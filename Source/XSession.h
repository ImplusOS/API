#pragma once

#include <stdint.h>

#include "Window.h"

/*
 * XSession -- shared bring-up for the launchers that run a foreign X11 client
 * (Userland/Application/{Chromium,Doom,Terminal,Wine,...}).
 *
 * ImplusOS has no X server of its own. What it has is Debian's unmodified
 * Xorg, staged by Vendor/LinuxRuntime, scanning out through the kernel's KMS
 * shim. Each launcher starts an X server of its own (display :N, a DRM
 * session of its own -- Kernel/Source/Core/drm/DRM_Kms.c) whose scanout is
 * redirected into an off-screen buffer, and is that server's window manager:
 *
 *   - every top-level X window becomes an ordinary ImplusOS window, with the
 *     compositor's title bar, taskbar entry, focus and close button; its
 *     pixels are cut out of the redirected screen. Menus, tooltips and other
 *     override-redirect windows become undecorated ImplusOS windows at the
 *     matching place. There is no "X desktop" window and no window inside a
 *     window.
 *   - the X screen is twice the display's width; the window manager places
 *     each top-level where it does not overlap another, so cutting one out
 *     never shows another's pixels.
 *   - input from those ImplusOS windows is fed to the server through XTEST
 *     (no evdev: servers do not share devices), and the X input focus follows
 *     the ImplusOS focus.
 *
 * Because the server is the launcher's own, a second launcher never joins
 * the first one's server, and closing one program cannot take another with
 * it. Programs the client starts itself (an xterm opened from an xterm) run
 * on the same server and get windows the same way; the launcher stays until
 * its client has exited and no window of the server is left.
 */

typedef struct {
    int32_t  xorg_pid;   /* > 0 while this session's server runs */
    uint32_t display;    /* N of ":N" */
    uint32_t width;      /* the X screen */
    uint32_t height;
    int      ready;      /* the server is up and managed */
} xsession_t;

/* Bring up this launcher's X server. `title` names windows whose program
 * gives them no name; width/height are only a hint for the screen size.
 * Returns 0, or a negative value when no server could be started. */
int32_t xsession_open(xsession_t *session, const char *title,
                      uint32_t width, uint32_t height);

/* Spawn `path` as an X client (`args` may be NULL) and run its windows until
 * it has exited and no window of the server is left. Returns the client's
 * exit status, or a negative value if it could not be started.
 *
 * The child gets an environment assembled here, not in the kernel: the
 * display and Mesa variables every X client needs, plus the desktop-stack
 * variables GTK/GLib/fontconfig programs read, plus anything `extra_envp`
 * adds (a NULL-terminated "NAME=VALUE" array; see API/Source/LinuxEnv.c). */
int32_t xsession_run_env(xsession_t *session, const char *path, const char *args,
                         const char *const *extra_envp);

/* xsession_run_env() with nothing extra. */
int32_t xsession_run(xsession_t *session, const char *path, const char *args);

/* Close every window and stop the server. */
void xsession_close(xsession_t *session);

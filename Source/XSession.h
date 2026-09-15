#pragma once

#include <stdint.h>

#include "Window.h"

/*
 * XSession -- shared bring-up for the launchers that run a foreign X11 client
 * (Userland/Application/{Doom,Terminal}).
 *
 * ImplusOS has no X server of its own. What it has is Debian's unmodified
 * Xorg, staged by Vendor/LinuxRuntime, scanning out through the kernel's KMS
 * shim at /dev/dri/card0 -- which is the same framebuffer the window manager
 * owns. Left alone the two fight over the panel and whichever presented last
 * wins, so a launcher instead creates an ordinary WM window and registers its
 * backing store as the KMS scanout target (display_kms_set_mirror). X then
 * draws into that surface and the compositor composites it like any other
 * window.
 *
 * There is exactly one display (":0"), so the SECOND launcher to start must
 * join the server that is already running rather than start its own -- two
 * Xorg processes on the same socket, both trying to own the panel, is the
 * failure this type exists to prevent. A joined session creates no window and
 * takes no mirror: its client draws inside whichever window already hosts the
 * server, and tearing it down leaves that server running for its owner.
 */

typedef struct {
    int32_t     xorg_pid;   /* > 0 only when this session started the server */
    window_id_t window;     /* 0 when hosted elsewhere or X owns the panel */
    uint32_t    width;
    uint32_t    height;
    int         mirrored;   /* X is redirected into `window` */
    int         joined;     /* a server was already running; we did not start it */
    uint8_t     buttons;    /* mouse buttons last forwarded to the X server */
} xsession_t;

/* Bring up the X server, or join the running one, and give it somewhere to
 * draw. `title` names the hosting window. Returns 0, or a negative value when
 * no server could be reached. */
int32_t xsession_open(xsession_t *session, const char *title,
                      uint32_t width, uint32_t height);

/* Spawn `path` as an X client (`args` may be NULL) and pump the mirror until
 * it exits. Returns the client's exit status, or a negative value if it could
 * not be started. */
int32_t xsession_run(xsession_t *session, const char *path, const char *args);

/* Release the window and the mirror, and stop the server if this session was
 * the one that started it. */
void xsession_close(xsession_t *session);

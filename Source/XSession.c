#include "XSession.h"

#include <stdio.h>

#include "Graphics.h"
#include "Process.h"
#include "Serial.h"
#include "Socket.h"
#include "Window.h"

#define XORG_PATH "/usr/bin/Xorg"

/* The server's arguments, unchanged from the set the Doom bring-up settled on
 * (Docs/Others/TODO_Doom_Xorg_MethodA.md):
 *   +iglx      indirect GLX -- there is no direct-rendering path here, so the
 *              server has to provide GL itself for a GLX client.
 *   -ac        no host-based access control: there is no .Xauthority and no
 *              xhost here, so every client arrives with no credentials.
 *   -keeptty / -novtswitch   there are no VTs to switch away from.
 *   -logfile /dev/tty        the X log goes to COM1, the only channel a
 *              bring-up failure can be read from.
 * Deliberately not -verbose: COM1 is driven a character at a time from inside
 * the syscall path, and raising the level costs seconds before the first
 * frame. */
#define XORG_ARGS ":0 -config /etc/X11/xorg.conf -nolisten tcp -novtswitch " \
                  "-keeptty +iglx -dumbSched -ac " \
                  "-logfile /dev/tty"

/* The socket Xorg binds once it is ready to accept clients. */
#define X_SOCKET_PATH "/tmp/.X11-unix/X0"

/* A timeout, not a delay: the wait ends the moment the socket is listening.
 * Xorg dynamic-links ~40 shared objects and runs xkbcomp twice, so how long
 * that takes is wildly load-dependent under TCG. */
#define XORG_READY_TIMEOUT_MS 120000u
#define XORG_POLL_INTERVAL_MS 50u

/* Frame pacing while a client is running. The mirror is repainted only when
 * the server actually produced a frame, so this is a poll interval, not a
 * refresh rate. */
#define XSESSION_FRAME_MS 16u
#define XSESSION_IDLE_MS  200u

static int server_is_listening(void)
{
    return unix_socket_is_listening(X_SOCKET_PATH) > 0;
}

static int wait_for_server(void)
{
    uint64_t started = get_uptime_ms();
    uint32_t waited = 0u;
    int ready = 0;
    while (waited < XORG_READY_TIMEOUT_MS) {
        if (server_is_listening()) {
            ready = 1;
            break;
        }
        sleep_ms(XORG_POLL_INTERVAL_MS);
        waited += XORG_POLL_INTERVAL_MS;
    }
    char message[96];
    /* Worth logging on every boot: essentially all of the time before the
     * first frame is spent here, so it is the number to watch when anything
     * touching foreign-process startup changes. */
    snprintf(message, sizeof message, "[xsession] Xorg %s after %lu ms\n",
             ready ? "ready" : "TIMED OUT",
             (unsigned long)(get_uptime_ms() - started));
    serial_write_string(message);
    return ready;
}

int32_t xsession_open(xsession_t *session, const char *title,
                      uint32_t width, uint32_t height)
{
    if (session == NULL) {
        return -1;
    }
    session->xorg_pid = -1;
    session->window = 0u;
    session->width = 0u;
    session->height = 0u;
    session->mirrored = 0;
    session->joined = 0;

    /* Someone got here first. Join their server: the display, the panel and
     * the KMS mirror all belong to them. */
    if (server_is_listening()) {
        session->joined = 1;
        serial_write_string("[xsession] joined the running X server\n");
        return 0;
    }

    /* Host X inside a window when there is a compositor to host it. Without
     * one (early bring-up, or a WM that failed to start) fall back to letting
     * X own the panel, which is the only way anything is visible at all. */
    if (window_get_wm_pid() >= 0) {
        session->window = window_create(width, height, title);
        if (session->window != 0u) {
            uint32_t w = 0u, h = 0u;
            uint32_t *pixels = window_get_backing_store(session->window, &w, &h);
            if (pixels != NULL && w != 0u && h != 0u &&
                display_kms_set_mirror(pixels, w, h) == 0) {
                session->width = w;
                session->height = h;
                session->mirrored = 1;
                /* The backing store starts fully transparent and X only ever
                 * writes RGB -- it never sets an alpha byte -- so without this
                 * the compositor blends every frame against the wallpaper and
                 * the window reads as an empty pane. */
                (void)window_set_surface_opaque(session->window, true);
                window_show(session->window);
                window_raise(session->window);
                serial_write_string("[xsession] X redirected into a window\n");
            } else {
                /* Leave the window up: it is where any error text goes, and
                 * destroying it would flash the desktop. */
                serial_write_string("[xsession] window mirror unavailable, "
                                    "X will scan out to the panel\n");
            }
        }
    }

    session->xorg_pid = process_spawn_with_arg(XORG_PATH, XORG_ARGS);
    if (session->xorg_pid <= 0) {
        serial_write_string("[xsession] failed to spawn Xorg\n");
        xsession_close(session);
        return -1;
    }
    if (!wait_for_server()) {
        xsession_close(session);
        return -1;
    }
    return 0;
}

int32_t xsession_run(xsession_t *session, const char *path, const char *args)
{
    if (session == NULL || path == NULL) {
        return -1;
    }

    int32_t pid = (args != NULL) ? process_spawn_with_arg(path, args)
                                 : process_spawn(path);
    if (pid < 0) {
        return -1;
    }

    /* The native process_waitpid() never blocks: it reports 0 while the child
     * still runs and only returns the pid once it has exited. Polling it is
     * what keeps the mirror being repainted in the meantime. */
    int32_t status = 0;
    for (;;) {
        int32_t reaped = process_waitpid(pid, &status, 0);
        if (reaped == pid) break;   /* exited, status valid */
        if (reaped < 0) break;      /* no such child */
        if (session->mirrored && display_kms_mirror_take_dirty()) {
            window_damage(session->window, 0u, 0u,
                          session->width, session->height);
        }
        sleep_ms(session->mirrored ? XSESSION_FRAME_MS : XSESSION_IDLE_MS);
    }
    return status;
}

void xsession_close(xsession_t *session)
{
    if (session == NULL || session->joined) {
        return; /* someone else's server: leave every part of it alone */
    }

    if (session->xorg_pid > 0) {
        (void)process_kill(session->xorg_pid);
        session->xorg_pid = -1;
    }
    if (session->mirrored) {
        /* Hand the panel back before leaving, or a flip from a lingering
         * server would write into a surface this process no longer owns. */
        (void)display_kms_set_mirror(0, 0u, 0u);
        session->mirrored = 0;
    }
    if (session->window != 0u) {
        window_destroy(session->window);
        session->window = 0u;
    }
}

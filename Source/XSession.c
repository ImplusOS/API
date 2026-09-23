#include "XSession.h"

#include <stdio.h>

#include "Graphics.h"
#include "Input.h"
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

static int wait_for_server(xsession_t *session)
{
    uint64_t started = get_uptime_ms();
    uint32_t waited = 0u;
    int ready = 0;
    int exited = 0;
    int32_t status = 0;
    while (waited < XORG_READY_TIMEOUT_MS) {
        if (server_is_listening()) {
            ready = 1;
            break;
        }
        /* The server can also die before it ever listens -- a missing symbol
         * in one of the ~40 shared objects it dynamic-links is the usual way,
         * and ld.so exits 127 for that. process_waitpid() does not block (it
         * reports 0 while the child still runs), so asking every pass turns
         * what used to be a silent two-minute stall into an immediate,
         * legible failure: the old loop sat here for the full timeout after
         * Xorg had already exited, and the log said "TIMED OUT" when the real
         * event was an exit a minute earlier. */
        if (session->xorg_pid > 0 &&
            process_waitpid(session->xorg_pid, &status, 0) == session->xorg_pid) {
            session->xorg_pid = -1;  /* reaped: nothing left to kill */
            exited = 1;
            break;
        }
        sleep_ms(XORG_POLL_INTERVAL_MS);
        waited += XORG_POLL_INTERVAL_MS;
    }
    char message[128];
    /* Worth logging on every boot: essentially all of the time before the
     * first frame is spent here, so it is the number to watch when anything
     * touching foreign-process startup changes. */
    if (exited) {
        snprintf(message, sizeof message,
                 "[xsession] Xorg exited (status %ld) after %lu ms, never listened\n",
                 (long)status, (unsigned long)(get_uptime_ms() - started));
    } else {
        snprintf(message, sizeof message, "[xsession] Xorg %s after %lu ms\n",
                 ready ? "ready" : "TIMED OUT",
                 (unsigned long)(get_uptime_ms() - started));
    }
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
    session->surface_w = 0u;
    session->surface_h = 0u;
    session->mirrored = 0;
    session->joined = 0;
    session->buttons = 0u;

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
                session->surface_w = w;
                session->surface_h = h;
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
    if (!wait_for_server(session)) {
        xsession_close(session);
        return -1;
    }
    return 0;
}

/* Linux input-event codes (linux/input-event-codes.h) used below. */
#define LX_EV_SYN      0x00u
#define LX_EV_KEY      0x01u
#define LX_EV_REL      0x02u
#define LX_EV_ABS      0x03u
#define LX_SYN_REPORT  0u
#define LX_REL_WHEEL   8u
#define LX_ABS_X       0u
#define LX_ABS_Y       1u
#define LX_BTN_LEFT    0x110u
#define LX_BTN_RIGHT   0x111u
#define LX_BTN_MIDDLE  0x112u
#define LX_ABS_MAX     65535u

/* The kernel's keycodes are PS/2 scan code set 1, with 0xE0-prefixed keys
 * as 0xE0xx (Kernel/Source/include/kernel/keycodes.h). Linux's KEY_* values
 * 1..88 are set 1 unchanged; the extended and Japanese keys are not. */
static uint16_t xsession_linux_keycode(uint16_t keycode)
{
    if (keycode >= 1u && keycode <= 0x58u) {
        return keycode;
    }
    switch (keycode) {
    case 0x70u:   return 93u;   /* KEY_KATAKANAHIRAGANA */
    case 0x73u:   return 89u;   /* KEY_RO */
    case 0x79u:   return 92u;   /* KEY_HENKAN */
    case 0x7Bu:   return 94u;   /* KEY_MUHENKAN */
    case 0x7Du:   return 124u;  /* KEY_YEN */
    case 0xE01Cu: return 96u;   /* KEY_KPENTER */
    case 0xE01Du: return 97u;   /* KEY_RIGHTCTRL */
    case 0xE035u: return 98u;   /* KEY_KPSLASH */
    case 0xE037u: return 99u;   /* KEY_SYSRQ */
    case 0xE038u: return 100u;  /* KEY_RIGHTALT */
    case 0xE047u: return 102u;  /* KEY_HOME */
    case 0xE048u: return 103u;  /* KEY_UP */
    case 0xE049u: return 104u;  /* KEY_PAGEUP */
    case 0xE04Bu: return 105u;  /* KEY_LEFT */
    case 0xE04Du: return 106u;  /* KEY_RIGHT */
    case 0xE04Fu: return 107u;  /* KEY_END */
    case 0xE050u: return 108u;  /* KEY_DOWN */
    case 0xE051u: return 109u;  /* KEY_PAGEDOWN */
    case 0xE052u: return 110u;  /* KEY_INSERT */
    case 0xE053u: return 111u;  /* KEY_DELETE */
    case 0xE05Bu: return 125u;  /* KEY_LEFTMETA */
    case 0xE05Cu: return 126u;  /* KEY_RIGHTMETA */
    case 0xE05Du: return 127u;  /* KEY_COMPOSE */
    default:      return 0u;
    }
}

static int32_t xsession_scale_axis(uint16_t pos, uint32_t extent)
{
    if (extent <= 1u) {
        return 0;
    }
    uint32_t p = pos >= extent ? extent - 1u : pos;
    return (int32_t)((p * LX_ABS_MAX) / (extent - 1u));
}

/* Hand the window's keyboard and mouse to the X server. X reads
 * /dev/input/event0 (keyboard) and event1 (absolute pointer) through
 * xf86-input-evdev; the compositor delivers this window's input to us as IPC
 * events, in window-content coordinates. Before this, nothing ever fed those
 * devices, so a client like Chromium could be seen but not used. */
static void xsession_forward_input(xsession_t *session)
{
    input_keyboard_event_t key;
    while (window_input_keyboard_poll(&key) > 0) {
        uint16_t code = xsession_linux_keycode(key.keycode);
        if (code == 0u) {
            continue;
        }
        (void)input_evdev_inject(0u, LX_EV_KEY, code, key.pressed ? 1 : 0);
        (void)input_evdev_inject(0u, LX_EV_SYN, LX_SYN_REPORT, 0);
    }

    input_mouse_event_t mouse;
    while (window_input_mouse_poll(&mouse) > 0) {
        (void)input_evdev_inject(1u, LX_EV_ABS, LX_ABS_X,
                                 xsession_scale_axis(mouse.x, session->width));
        (void)input_evdev_inject(1u, LX_EV_ABS, LX_ABS_Y,
                                 xsession_scale_axis(mouse.y, session->height));
        uint8_t changed = (uint8_t)(mouse.buttons ^ session->buttons);
        static const struct { uint8_t mask; uint16_t code; } buttons[] = {
            { INPUT_MOUSE_BTN_LEFT,   LX_BTN_LEFT },
            { INPUT_MOUSE_BTN_RIGHT,  LX_BTN_RIGHT },
            { INPUT_MOUSE_BTN_MIDDLE, LX_BTN_MIDDLE },
        };
        for (uint32_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
            if ((changed & buttons[i].mask) != 0u) {
                (void)input_evdev_inject(1u, LX_EV_KEY, buttons[i].code,
                                         (mouse.buttons & buttons[i].mask) ? 1 : 0);
            }
        }
        session->buttons = mouse.buttons;
        if (mouse.wheel != 0) {
            (void)input_evdev_inject(1u, LX_EV_REL, LX_REL_WHEEL, mouse.wheel);
        }
        (void)input_evdev_inject(1u, LX_EV_SYN, LX_SYN_REPORT, 0);
    }
}

/* Startup timing probe (-DXSESSION_TIMING=1). Logs, on the same uptime clock
 * the kernel stamps "[app] spawn ... at=" with, when the client's first frame
 * reaches the mirror, when the middle of the surface first shows something
 * other than black (a dialog or page being painted) and when the left edge
 * turns light (a page filling the whole window). Two pixel reads per mirrored
 * frame; nothing at all when compiled out. */
#ifndef XSESSION_TIMING
#define XSESSION_TIMING 0
#endif

#if XSESSION_TIMING
#include <time.h>

/* The kernel's monotonic clock (what "[app] ... at=" is stamped with), not
 * get_uptime_ms(): that one counts timer ticks and falls behind whenever
 * interrupts are held off. */
static uint64_t xsession_timing_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return get_uptime_ms();
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void xsession_timing_note(xsession_t *session, uint64_t spawned_ms)
{
    static int stage;
    uint32_t w = 0u, h = 0u;
    uint32_t *px = window_get_backing_store(session->window, &w, &h);
    if (px == NULL || w < 64u || h < 64u) {
        return;
    }
    uint32_t center = px[(h / 2u) * w + w / 2u] & 0x00FFFFFFu;
    uint32_t edge = px[(h / 2u) * w + 16u] & 0x00FFFFFFu;
    const char *what = NULL;
    if (stage == 0) {
        what = "first-frame";
        stage = 1;
    } else if (stage == 1 && center != 0u) {
        what = "ui-painted";
        stage = 2;
    } else if (stage == 2 && ((edge >> 16) & 0xFFu) >= 0xE0u &&
               (edge & 0xFFu) >= 0xE0u) {
        what = "page-painted";
        stage = 3;
    }
    if (what != NULL) {
        char line[96];
        uint64_t now = xsession_timing_now_ms();
        snprintf(line, sizeof(line),
                 "[xsession] %s at=%llums (+%llums) c=%06x e=%06x\n", what,
                 (unsigned long long)now,
                 (unsigned long long)(now - spawned_ms), center, edge);
        serial_write_string(line);
    }
}
#endif

/* Point the mirror at the window's current backing store.
 *
 * A resize replaces that surface: the window manager allocates a new
 * shared-memory object, hands the owner the new handle and closes the old
 * one. The mirror still names the pages of the surface X was drawing into, so
 * without this every frame after the first resize lands somewhere the
 * compositor no longer reads and the window sits on its last pre-resize frame.
 *
 * The mirror is released before the old mapping is dropped, not after: the
 * pages go back to the allocator with the last mapping, and a flip arriving in
 * between would memcpy into memory that is no longer ours. Flips are dropped
 * while no mirror is registered, which costs at most one frame.
 *
 * X reads the connector's mode once at startup, so it keeps rendering at the
 * size the window had then; the blit clips to whichever of the two is smaller.
 * Growing a window leaves the new margin at the background colour until the
 * server can be told to change modes (RandR is not wired up yet). */
static bool xsession_rebind_mirror(xsession_t *session)
{
    (void)display_kms_set_mirror(0, 0u, 0u);
    session->mirrored = 0;

    uint32_t w = 0u, h = 0u;
    uint32_t *pixels = window_get_backing_store(session->window, &w, &h);
    if (pixels == NULL || w == 0u || h == 0u ||
        display_kms_set_mirror(pixels, w, h) != 0) {
        return false;
    }

    session->surface_w = w;
    session->surface_h = h;
    session->mirrored = 1;
    (void)window_set_surface_opaque(session->window, true);
    window_damage(session->window, 0u, 0u, w, h);
    return true;
}

int32_t xsession_run(xsession_t *session, const char *path, const char *args)
{
    if (session == NULL || path == NULL) {
        return -1;
    }

#if XSESSION_TIMING
    uint64_t spawned_ms = xsession_timing_now_ms();
#endif
    int32_t pid = (args != NULL) ? process_spawn_with_arg(path, args)
                                 : process_spawn(path);
    if (pid < 0) {
        return -1;
    }

    /* The native process_waitpid() never blocks: it reports 0 while the child
     * still runs and only returns the pid once it has exited. Polling it is
     * what keeps the mirror being repainted in the meantime. */
    if (session->window != 0u) {
        (void)window_subscribe_keyboard(session->window);
        (void)window_subscribe_mouse(session->window);
    }

    int32_t status = 0;
    /* Only a session that redirected scanout has a mirror to re-point; when X
     * drives the panel there is no surface tied to the window's size. */
    const bool mirror_owned = session->mirrored != 0;
    bool rebind_mirror = false;
    for (;;) {
        int32_t reaped = process_waitpid(pid, &status, 0);
        if (reaped == pid) break;   /* exited, status valid */
        if (reaped < 0) break;      /* no such child */
        if (session->window != 0u) {
            xsession_forward_input(session);
            uint32_t new_w = 0u, new_h = 0u;
            if (window_poll_resize(session->window, &new_w, &new_h) > 0 &&
                mirror_owned &&
                (new_w != session->surface_w || new_h != session->surface_h)) {
                rebind_mirror = true;
            }
            /* Stays set until a rebind succeeds: dropping the window here
             * would strand X drawing into a surface nothing composites. */
            if (rebind_mirror && xsession_rebind_mirror(session)) {
                rebind_mirror = false;
            }
        }
        if (session->mirrored && display_kms_mirror_take_dirty()) {
            window_damage(session->window, 0u, 0u,
                          session->surface_w, session->surface_h);
#if XSESSION_TIMING
            xsession_timing_note(session, spawned_ms);
#endif
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

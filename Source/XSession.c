#include "XSession.h"

#include <stdio.h>
#include <string.h>

#include "Graphics.h"
#include "Input.h"
#include "LinuxEnv.h"
#include "Memory.h"
#include "Process.h"
#include "Serial.h"
#include "Socket.h"
#include "Window.h"
#include "X11Wire.h"

#define XORG_PATH "/usr/bin/Xorg"

/* The server's arguments, after the display number:
 *   +iglx      indirect GLX -- the server provides GL itself for a GLX client
 *              that cannot render directly.
 *   -ac        no host-based access control: there is no .Xauthority here,
 *              so every client arrives with no credentials.
 *   -keeptty / -novtswitch   there are no VTs to switch away from.
 *   -logfile /dev/tty        the X log goes to COM1, the only channel a
 *              bring-up failure can be read from.
 * Deliberately not -verbose: COM1 is driven a character at a time from inside
 * the syscall path, and raising the level costs seconds before the first
 * frame. */
#define XORG_ARGS_FMT ":%u -config /etc/X11/xorg.conf -nolisten tcp " \
                      "-novtswitch -keeptty +iglx -dumbSched -ac "  \
                      "-logfile /dev/tty"

/* Displays tried, in order, for this launcher's server. One per running X
 * program, so this also bounds how many can run at once (the kernel's DRM
 * shim has DRM_MAX_SESSIONS of them). */
#define XSESSION_MAX_DISPLAYS 8u

/* A timeout, not a delay: the wait ends the moment the socket is listening.
 * Xorg dynamic-links ~40 shared objects and runs xkbcomp twice, so how long
 * that takes is wildly load-dependent under TCG. */
#define XORG_READY_TIMEOUT_MS 120000u
#define XORG_POLL_INTERVAL_MS 10u

/* Frame pacing while a client is running. Windows are repainted only when
 * the server actually produced a frame, so this is a poll interval, not a
 * refresh rate -- but it is also the interval at which input reaches the
 * server, so it starts at the scheduler-tick floor and only backs off while
 * nothing at all happens. */
#define XSESSION_FRAME_MS 1u
#define XSESSION_ACTIVE_BACKOFF_MAX_MS 8u

/* Gap kept between top-level windows on the X screen, so a menu that hangs
 * a little past its window's edge does not land on the neighbour's pixels. */
#define XS_SLOT_GAP 32

#define XS_MAX_WINDOWS 64u
#define XS_TITLE_MAX   128u

/* ------------------------------------------------------------------ */
/* session state (one session per launcher process)                     */
/* ------------------------------------------------------------------ */

typedef struct {
    bool        used;
    uint32_t    xid;          /* the X window */
    window_id_t wid;          /* its ImplusOS window */
    bool        managed;      /* false: override-redirect (menu, tooltip) */
    int32_t     x, y;         /* position on the X screen */
    uint32_t    w, h;
    uint32_t   *pixels;       /* the ImplusOS window's backing store */
    uint32_t    pw, ph;
    uint32_t    owner;        /* popups: index + 1 of the window they belong to */
    bool        delete_protocol;
} xs_win_t;

static x11_conn_t g_x;
static bool       g_x_connected;
static xs_win_t   g_wins[XS_MAX_WINDOWS];
static uint32_t  *g_screen;           /* the redirected X screen */
static int32_t    g_screen_shm = -1;
static uint32_t   g_sw, g_sh;
static uint32_t   g_focus_xid;
static uint8_t    g_buttons;
static char       g_fallback_title[XS_TITLE_MAX];
static char       g_display_env[24];
#define XS_MAX_CLIENTS 8u
static int32_t    g_clients[XS_MAX_CLIENTS]; /* each leads its own session */
static uint32_t   g_client_count;

static uint32_t A_WM_PROTOCOLS, A_WM_DELETE_WINDOW, A_WM_STATE,
                A_NET_WM_NAME, A_UTF8_STRING;

static void xs_log_u32(const char *what, uint32_t value)
{
    char line[96];
    snprintf(line, sizeof line, "[xsession] %s%u\n", what, value);
    serial_write_string(line);
}

/* ------------------------------------------------------------------ */
/* server start-up                                                      */
/* ------------------------------------------------------------------ */

static int server_is_listening(uint32_t display)
{
    char path[48];
    snprintf(path, sizeof path, "/tmp/.X11-unix/X%u", display);
    return unix_socket_is_listening(path) > 0;
}

/* 1 listening, 0 the server exited first (reaped), -1 timed out. */
static int wait_for_server(xsession_t *session)
{
    uint64_t started = get_uptime_ms();
    int result = -1;
    int32_t status = 0;
    while (get_uptime_ms() - started < XORG_READY_TIMEOUT_MS) {
        if (server_is_listening(session->display)) {
            result = 1;
            break;
        }
        /* The server can also die before it ever listens -- a display some
         * other launcher took a moment earlier, or a missing symbol in one
         * of the ~40 shared objects it dynamic-links. process_waitpid() does
         * not block, so asking every pass turns that into an immediate,
         * legible failure rather than a two-minute stall. */
        if (process_waitpid(session->xorg_pid, &status, 0) == session->xorg_pid) {
            session->xorg_pid = -1;
            result = 0;
            break;
        }
        sleep_ms(XORG_POLL_INTERVAL_MS);
    }
    char message[128];
    if (result == 0) {
        snprintf(message, sizeof message,
                 "[xsession] Xorg :%u exited (status %ld) after %lu ms, "
                 "never listened\n", session->display, (long)status,
                 (unsigned long)(get_uptime_ms() - started));
    } else {
        snprintf(message, sizeof message, "[xsession] Xorg :%u %s after %lu ms\n",
                 session->display, result > 0 ? "ready" : "TIMED OUT",
                 (unsigned long)(get_uptime_ms() - started));
    }
    serial_write_string(message);
    return result;
}

/* Stop a process this session started and collect it.
 *
 * SIGTERM first: Xorg shuts down cleanly on it -- it closes and unlinks its
 * listening sockets and exits through exit_group(), which takes its input,
 * disk and llvmpipe threads with it. A bare SIGKILL left a zombie Xorg whose
 * socket went on reporting "listening". SIGKILL stays as the fallback for a
 * server that will not go. Reaped here as well: the launcher is about to
 * exit, and a child it has not collected stays a zombie for good. */
#define XSESSION_TERM_TIMEOUT_MS 3000u
#define XSESSION_KILL_TIMEOUT_MS 3000u
#define XS_SIGTERM 15

static bool xsession_reap_within(int32_t pid, uint32_t timeout_ms)
{
    int32_t status = 0;
    for (uint32_t waited = 0u; waited < timeout_ms;
         waited += XORG_POLL_INTERVAL_MS) {
        int32_t reaped = process_waitpid(pid, &status, 0);
        if (reaped == pid || reaped < 0) {
            return true;
        }
        sleep_ms(XORG_POLL_INTERVAL_MS);
    }
    return false;
}

static void xsession_stop_and_reap(int32_t pid)
{
    (void)process_send_signal(pid, XS_SIGTERM);
    if (xsession_reap_within(pid, XSESSION_TERM_TIMEOUT_MS)) {
        return;
    }
    (void)process_kill(pid);
    if (!xsession_reap_within(pid, XSESSION_KILL_TIMEOUT_MS)) {
        serial_write_string("[xsession] a stopped server was not reaped in time\n");
    }
}

/* The X screen: twice the display's width, so a program's dialogs can sit
 * beside its main window instead of on top of it; capped by what the
 * kernel will mirror (DRM_MIRROR_MAX_PAGES, 32 MiB). */
#define XS_MIRROR_MAX_BYTES (32u * 1024u * 1024u)

static bool xsession_alloc_screen(uint32_t hint_w, uint32_t hint_h)
{
    uint32_t dw = get_display_width();
    uint32_t dh = get_display_height();
    if (dw == 0u || dh == 0u) { dw = 1280u; dh = 800u; }
    if (hint_w > dw) dw = hint_w;
    if (hint_h > dh) dh = hint_h;
    uint32_t w = dw * 2u;
    uint32_t h = dh;
    if (w > 8192u) w = 8192u;
    if (h > 8192u) h = 8192u;
    while ((uint64_t)w * h * 4u > XS_MIRROR_MAX_BYTES && w > dw) {
        w -= 64u;
    }
    uint32_t bytes = w * h * 4u;
    g_screen_shm = os_shared_memory_create(bytes);
    if (g_screen_shm < 0) return false;
    g_screen = (uint32_t *)os_shared_memory_map(g_screen_shm);
    if (g_screen == NULL) {
        (void)os_shared_memory_close(g_screen_shm);
        g_screen_shm = -1;
        return false;
    }
    /* Touch every page: the kernel resolves them once, at registration. */
    memset(g_screen, 0, bytes);
    g_sw = w;
    g_sh = h;
    return true;
}

static void xsession_free_screen(void)
{
    (void)display_kms_set_mirror(0, 0u, 0u);
    if (g_screen != NULL) {
        (void)os_shared_memory_unmap(g_screen_shm, g_screen);
        g_screen = NULL;
    }
    if (g_screen_shm >= 0) {
        (void)os_shared_memory_close(g_screen_shm);
        g_screen_shm = -1;
    }
}

int32_t xsession_open(xsession_t *session, const char *title,
                      uint32_t width, uint32_t height)
{
    if (session == NULL) {
        return -1;
    }
    memset(session, 0, sizeof(*session));
    session->xorg_pid = -1;
    memset(g_wins, 0, sizeof g_wins);
    g_focus_xid = 0u;
    g_buttons = 0u;
    snprintf(g_fallback_title, sizeof g_fallback_title, "%s",
             title != NULL ? title : "X11");

    if (!xsession_alloc_screen(width, height)) {
        serial_write_string("[xsession] no memory for the X screen\n");
        return -1;
    }
    /* Registered before the server starts: the kernel binds this DRM
     * session to the first DRM client descended from this process, and
     * advertises the buffer's size as the connector's mode -- which is how
     * the server comes up at exactly g_sw x g_sh. */
    if (display_kms_set_mirror(g_screen, g_sw, g_sh) != 0) {
        serial_write_string("[xsession] the kernel refused the X screen mirror\n");
        xsession_free_screen();
        return -1;
    }
    session->width = g_sw;
    session->height = g_sh;

    /* A display of our own. Another launcher may take the same free one at
     * the same moment; its server or ours then fails to lock it and exits
     * before listening, and the loser moves on to the next. */
    for (uint32_t display = 0u; display < XSESSION_MAX_DISPLAYS; ++display) {
        if (server_is_listening(display)) continue;
        session->display = display;
        char args[192];
        snprintf(args, sizeof args, XORG_ARGS_FMT, display);
        linux_env_t xorg_env;
        linux_env_init(&xorg_env);
        linux_env_add_x11(&xorg_env);
        linux_env_add_desktop(&xorg_env);
        linux_env_add_xorg_server(&xorg_env);
        session->xorg_pid = process_spawn_with_env(XORG_PATH, args,
                                                   linux_env_entries(&xorg_env));
        if (session->xorg_pid <= 0) {
            serial_write_string("[xsession] failed to spawn Xorg\n");
            xsession_close(session);
            return -1;
        }
        int ready = wait_for_server(session);
        if (ready > 0) break;
        if (ready < 0) {
            xsession_close(session);
            return -1;
        }
        session->xorg_pid = -1; /* lost the display: try the next */
    }
    if (session->xorg_pid <= 0) {
        serial_write_string("[xsession] no free X display\n");
        xsession_close(session);
        return -1;
    }


    if (x11_connect(&g_x, session->display, 10000u) != 0) {
        serial_write_string("[xsession] could not connect to the X server\n");
        xsession_close(session);
        return -1;
    }
    g_x_connected = true;
    /* x11_connect() returns once the server has answered the connection
     * setup -- i.e. once it has finished starting and runs Dispatch(). The
     * program is spawned only after that, so it never races this connection
     * through the server's start-up.
     *
     * Become the window manager: top-level windows are mapped and moved by
     * us, and we hear about every one that appears or goes. */
    x11_select_input(&g_x, g_x.root,
                     X11_SubstructureRedirectMask | X11_SubstructureNotifyMask);
    if (x11_sync(&g_x) != 0) {
        serial_write_string("[xsession] another window manager owns the server\n");
    }
    A_WM_PROTOCOLS     = x11_intern_atom(&g_x, "WM_PROTOCOLS", false);
    A_WM_DELETE_WINDOW = x11_intern_atom(&g_x, "WM_DELETE_WINDOW", false);
    A_WM_STATE         = x11_intern_atom(&g_x, "WM_STATE", false);
    A_NET_WM_NAME      = x11_intern_atom(&g_x, "_NET_WM_NAME", false);
    A_UTF8_STRING      = x11_intern_atom(&g_x, "UTF8_STRING", false);
    snprintf(g_display_env, sizeof g_display_env, "DISPLAY=:%u", session->display);
    session->ready = 1;

    char message[96];
    snprintf(message, sizeof message, "[xsession] managing :%u (%ux%u screen)\n",
             session->display, g_sw, g_sh);
    serial_write_string(message);
    return 0;
}

/* ------------------------------------------------------------------ */
/* windows                                                              */
/* ------------------------------------------------------------------ */

static xs_win_t *win_by_xid(uint32_t xid)
{
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (g_wins[i].used && g_wins[i].xid == xid) return &g_wins[i];
    }
    return NULL;
}

static xs_win_t *win_by_wid(window_id_t wid)
{
    if (wid == 0u) return NULL;
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (g_wins[i].used && g_wins[i].wid == wid) return &g_wins[i];
    }
    return NULL;
}

static xs_win_t *win_alloc(void)
{
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (!g_wins[i].used) {
            memset(&g_wins[i], 0, sizeof g_wins[i]);
            g_wins[i].used = true;
            return &g_wins[i];
        }
    }
    return NULL;
}

static uint32_t win_index(const xs_win_t *w) { return (uint32_t)(w - g_wins); }

static uint32_t managed_count(void)
{
    uint32_t n = 0u;
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (g_wins[i].used && g_wins[i].managed) ++n;
    }
    return n;
}

static void win_forget(xs_win_t *w)
{
    if (w->wid != 0u) {
        window_destroy(w->wid);
    }
    uint32_t idx = win_index(w) + 1u;
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (g_wins[i].used && g_wins[i].owner == idx) g_wins[i].owner = 0u;
    }
    if (g_focus_xid == w->xid) g_focus_xid = 0u;
    memset(w, 0, sizeof(*w));
}

static void win_refresh_backing(xs_win_t *w)
{
    uint32_t bw = 0u, bh = 0u;
    uint32_t *px = window_get_backing_store(w->wid, &bw, &bh);
    w->pixels = (px != NULL && bw != 0u && bh != 0u) ? px : NULL;
    w->pw = w->pixels ? bw : 0u;
    w->ph = w->pixels ? bh : 0u;
    if (w->pixels) (void)window_set_surface_opaque(w->wid, true);
}

/* Copy the window's rectangle of the X screen into its backing store. */
static void win_paint(xs_win_t *w)
{
    if (w->pixels == NULL || g_screen == NULL) return;
    int32_t x0 = w->x, y0 = w->y;
    uint32_t cw = w->pw, ch = w->ph;
    for (uint32_t row = 0; row < ch; ++row) {
        int32_t sy = y0 + (int32_t)row;
        uint32_t *dst = w->pixels + (size_t)row * w->pw;
        if (sy < 0 || (uint32_t)sy >= g_sh) {
            memset(dst, 0, cw * 4u);
            continue;
        }
        int32_t sx = x0;
        uint32_t skip = 0u;
        if (sx < 0) { skip = (uint32_t)(-sx); sx = 0; }
        if (skip >= cw) { memset(dst, 0, cw * 4u); continue; }
        uint32_t n = cw - skip;
        if ((uint32_t)sx + n > g_sw) n = (uint32_t)sx >= g_sw ? 0u : g_sw - (uint32_t)sx;
        if (skip) memset(dst, 0, skip * 4u);
        memcpy(dst + skip, g_screen + (size_t)sy * g_sw + (uint32_t)sx, n * 4u);
        if (skip + n < cw) memset(dst + skip + n, 0, (cw - skip - n) * 4u);
    }
    window_damage(w->wid, 0u, 0u, w->pw, w->ph);
}

/* Latin-1 (STRING / COMPOUND_TEXT's ASCII part) to UTF-8. */
static void latin1_to_utf8(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    uint32_t o = 0u;
    for (uint32_t i = 0; i < n && in[i] != 0u && o + 3u < cap; ++i) {
        if (in[i] < 0x80u) {
            out[o++] = (char)in[i];
        } else {
            out[o++] = (char)(0xC0u | (in[i] >> 6));
            out[o++] = (char)(0x80u | (in[i] & 0x3Fu));
        }
    }
    out[o] = '\0';
}

static void win_fetch_title(const xs_win_t *w, char *out, uint32_t cap)
{
    uint8_t buf[XS_TITLE_MAX];
    uint32_t type = 0u;
    uint8_t format = 0u;
    int32_t n = x11_get_property(&g_x, w->xid, A_NET_WM_NAME, A_UTF8_STRING,
                                 buf, sizeof buf - 1u, &type, &format);
    if (n > 0 && type == A_UTF8_STRING) {
        if ((uint32_t)n >= cap) n = (int32_t)cap - 1;
        memcpy(out, buf, (uint32_t)n);
        out[n] = '\0';
        return;
    }
    n = x11_get_property(&g_x, w->xid, X11_ATOM_WM_NAME, 0u, buf,
                         sizeof buf - 1u, &type, &format);
    if (n > 0) {
        if (type == A_UTF8_STRING) {
            if ((uint32_t)n >= cap) n = (int32_t)cap - 1;
            memcpy(out, buf, (uint32_t)n);
            out[n] = '\0';
        } else {
            latin1_to_utf8(buf, (uint32_t)n, out, cap);
        }
        if (out[0] != '\0') return;
    }
    snprintf(out, cap, "%s", g_fallback_title);
}

static bool win_supports_delete(uint32_t xid)
{
    uint32_t atoms[16];
    uint32_t type = 0u;
    uint8_t format = 0u;
    int32_t n = x11_get_property(&g_x, xid, A_WM_PROTOCOLS, X11_ATOM_ATOM,
                                 atoms, sizeof atoms, &type, &format);
    for (int32_t i = 0; n > 0 && i < n / 4; ++i) {
        if (atoms[i] == A_WM_DELETE_WINDOW) return true;
    }
    return false;
}

static bool rects_overlap(int32_t ax, int32_t ay, uint32_t aw, uint32_t ah,
                          int32_t bx, int32_t by, uint32_t bw, uint32_t bh)
{
    return ax < bx + (int32_t)bw + XS_SLOT_GAP && bx < ax + (int32_t)aw + XS_SLOT_GAP &&
           ay < by + (int32_t)bh + XS_SLOT_GAP && by < ay + (int32_t)ah + XS_SLOT_GAP;
}

static bool slot_free(int32_t x, int32_t y, uint32_t w, uint32_t h,
                      const xs_win_t *self)
{
    if (x < 0 || y < 0 || (uint32_t)x + w > g_sw || (uint32_t)y + h > g_sh) {
        return false;
    }
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        const xs_win_t *o = &g_wins[i];
        if (!o->used || !o->managed || o == self) continue;
        if (rects_overlap(x, y, w, h, o->x, o->y, o->w, o->h)) return false;
    }
    return true;
}

/* A place on the X screen where a w x h window overlaps no other top-level:
 * the origin, or just right of / below one already placed. Falls back to
 * the origin (overlapping) when the screen is full. */
static void find_slot(uint32_t w, uint32_t h, const xs_win_t *self,
                      int32_t *out_x, int32_t *out_y)
{
    if (slot_free(0, 0, w, h, self)) { *out_x = 0; *out_y = 0; return; }
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        const xs_win_t *o = &g_wins[i];
        if (!o->used || !o->managed || o == self) continue;
        int32_t cand[2][2] = {
            { o->x + (int32_t)o->w + XS_SLOT_GAP, o->y },
            { o->x, o->y + (int32_t)o->h + XS_SLOT_GAP },
        };
        for (uint32_t k = 0; k < 2u; ++k) {
            if (slot_free(cand[k][0], cand[k][1], w, h, self)) {
                *out_x = cand[k][0];
                *out_y = cand[k][1];
                return;
            }
        }
    }
    *out_x = 0;
    *out_y = 0;
}

static void clamp_size(uint32_t *w, uint32_t *h)
{
    if (*w < 1u) *w = 1u;
    if (*h < 1u) *h = 1u;
    if (*w > g_sw) *w = g_sw;
    if (*h > g_sh) *h = g_sh;
}

static void win_configure(xs_win_t *w)
{
    uint32_t values[5] = { (uint32_t)w->x, (uint32_t)w->y, w->w, w->h, 0u };
    x11_configure_window(&g_x, w->xid,
                         X11_CWX | X11_CWY | X11_CWWidth | X11_CWHeight |
                         X11_CWBorderWidth, values, 5u);
}

static void x_focus(xs_win_t *w)
{
    if (w == NULL || !w->managed || g_focus_xid == w->xid) return;
    x11_set_input_focus(&g_x, w->xid);
    g_focus_xid = w->xid;
}

/* MapRequest: a program wants a top-level window shown. */
static void manage(uint32_t xid)
{
    xs_win_t *w = win_by_xid(xid);
    if (w != NULL) {
        x11_map_window(&g_x, xid);
        return;
    }
    bool override_redirect = false;
    uint8_t map_state = 0u;
    uint16_t klass = 0u;
    if (x11_get_window_attributes(&g_x, xid, &override_redirect, &map_state,
                                  &klass) != 0) {
        return; /* already gone */
    }
    int16_t gx = 0, gy = 0;
    uint16_t gw = 0, gh = 0, border = 0;
    if (x11_get_geometry(&g_x, xid, &gx, &gy, &gw, &gh, &border) != 0) return;

    w = win_alloc();
    if (w == NULL) {
        x11_map_window(&g_x, xid); /* no room to track it: map it unmanaged */
        return;
    }
    w->xid = xid;
    w->managed = true;
    w->w = gw;
    w->h = gh;
    clamp_size(&w->w, &w->h);
    find_slot(w->w, w->h, w, &w->x, &w->y);
    w->delete_protocol = win_supports_delete(xid);

    uint32_t transient_for = 0u;
    uint32_t type = 0u;
    uint8_t format = 0u;
    (void)x11_get_property(&g_x, xid, X11_ATOM_WM_TRANSIENT_FOR,
                           X11_ATOM_WINDOW, &transient_for,
                           sizeof transient_for, &type, &format);

    char title[XS_TITLE_MAX];
    win_fetch_title(w, title, sizeof title);

    win_configure(w);
    x11_select_input(&g_x, xid, X11_PropertyChangeMask);
    uint32_t wm_state[2] = { 1u /* NormalState */, 0u };
    x11_change_property32(&g_x, xid, A_WM_STATE, A_WM_STATE, wm_state, 2u);
    x11_map_window(&g_x, xid);

    w->wid = window_create(w->w, w->h, title);
    if (w->wid == 0u) {
        serial_write_string("[xsession] window_create failed\n");
        return; /* left mapped on the X screen, unseen */
    }
    (void)window_set_close_policy(w->wid, true);
    (void)window_subscribe_keyboard(w->wid);
    (void)window_subscribe_mouse(w->wid);
    win_refresh_backing(w);
    /* The compositor may have clamped the size to the work area. */
    if (w->pixels != NULL && (w->pw != w->w || w->ph != w->h)) {
        w->w = w->pw;
        w->h = w->ph;
        win_configure(w);
    }
    /* A dialog opens over the window it belongs to. */
    xs_win_t *parent = transient_for != 0u ? win_by_xid(transient_for) : NULL;
    if (parent != NULL && parent->wid != 0u) {
        uint32_t px = 0, py = 0, pw = 0, ph = 0;
        if (window_get_rect(parent->wid, &px, &py, &pw, &ph) == 0) {
            int32_t nx = (int32_t)px + ((int32_t)pw - (int32_t)w->w) / 2;
            int32_t ny = (int32_t)py + ((int32_t)ph - (int32_t)w->h) / 2;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            window_set_rect(w->wid, (uint32_t)nx, (uint32_t)ny, w->w, w->h);
        }
    }
    win_paint(w);
    window_show(w->wid);
    window_raise(w->wid);
    window_set_focus(w->wid);
    x_focus(w);

    char line[XS_TITLE_MAX + 64];
    snprintf(line, sizeof line, "[xsession] window 0x%x \"%s\" %ux%u at %d,%d\n",
             xid, title, w->w, w->h, w->x, w->y);
    serial_write_string(line);
}

/* The managed window a popup at (x, y) belongs to. */
static xs_win_t *popup_owner(int32_t x, int32_t y)
{
    xs_win_t *focused = NULL, *any = NULL;
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        xs_win_t *o = &g_wins[i];
        if (!o->used || !o->managed || o->wid == 0u) continue;
        if (x >= o->x && y >= o->y && x < o->x + (int32_t)o->w &&
            y < o->y + (int32_t)o->h) {
            return o;
        }
        if (o->xid == g_focus_xid) focused = o;
        if (any == NULL) any = o;
    }
    return focused != NULL ? focused : any;
}

/* Where on the ImplusOS screen a popup at X position (x, y) goes. */
static bool popup_place(xs_win_t *p, int32_t *nx, int32_t *ny)
{
    xs_win_t *owner = p->owner ? &g_wins[p->owner - 1u] : NULL;
    if (owner == NULL || !owner->used || owner->wid == 0u) {
        owner = popup_owner(p->x, p->y);
        p->owner = owner ? win_index(owner) + 1u : 0u;
    }
    if (owner == NULL) return false;
    uint32_t cx = 0, cy = 0, cw = 0, ch = 0;
    if (window_get_content_rect(owner->wid, &cx, &cy, &cw, &ch) != 0) return false;
    *nx = (int32_t)cx + (p->x - owner->x);
    *ny = (int32_t)cy + (p->y - owner->y);
    if (*nx < 0) *nx = 0;
    if (*ny < 0) *ny = 0;
    return true;
}

/* MapNotify of an override-redirect window: a menu, a tooltip, a combo-box
 * list. It positions itself; show it undecorated at the same spot relative
 * to the window it belongs to. */
static void track_popup(uint32_t xid)
{
    if (win_by_xid(xid) != NULL) return;
    int16_t gx = 0, gy = 0;
    uint16_t gw = 0, gh = 0, border = 0;
    if (x11_get_geometry(&g_x, xid, &gx, &gy, &gw, &gh, &border) != 0) return;
    if (gw <= 1u && gh <= 1u) return; /* 1x1 helper windows: nothing to see */
    xs_win_t *p = win_alloc();
    if (p == NULL) return;
    p->xid = xid;
    p->managed = false;
    p->x = gx;
    p->y = gy;
    p->w = (uint32_t)gw + 2u * border;
    p->h = (uint32_t)gh + 2u * border;
    clamp_size(&p->w, &p->h);
    int32_t nx = 0, ny = 0;
    if (!popup_place(p, &nx, &ny)) {
        memset(p, 0, sizeof(*p));
        return;
    }
    p->wid = window_create_ex((uint32_t)nx, (uint32_t)ny, p->w, p->h, 0u, "");
    if (p->wid == 0u) {
        memset(p, 0, sizeof(*p));
        return;
    }
    window_set_system(p->wid, true);
    window_set_rect(p->wid, (uint32_t)nx, (uint32_t)ny, p->w, p->h);
    (void)window_subscribe_keyboard(p->wid);
    (void)window_subscribe_mouse(p->wid);
    win_refresh_backing(p);
    win_paint(p);
    window_show(p->wid);
    window_raise(p->wid);
}

static void popup_moved(xs_win_t *p, int16_t x, int16_t y, uint16_t w,
                        uint16_t h, uint16_t border)
{
    uint32_t nw = (uint32_t)w + 2u * border, nh = (uint32_t)h + 2u * border;
    clamp_size(&nw, &nh);
    if (p->x == x && p->y == y && p->w == nw && p->h == nh) return;
    p->x = x;
    p->y = y;
    p->w = nw;
    p->h = nh;
    int32_t nx = 0, ny = 0;
    if (popup_place(p, &nx, &ny)) {
        window_set_rect(p->wid, (uint32_t)nx, (uint32_t)ny, p->w, p->h);
    }
}

static void handle_configure_request(const uint8_t *ev)
{
    uint32_t xid = (uint32_t)ev[8] | ((uint32_t)ev[9] << 8) |
                   ((uint32_t)ev[10] << 16) | ((uint32_t)ev[11] << 24);
    int16_t rx = (int16_t)(ev[16] | (ev[17] << 8));
    int16_t ry = (int16_t)(ev[18] | (ev[19] << 8));
    uint16_t rw = (uint16_t)(ev[20] | (ev[21] << 8));
    uint16_t rh = (uint16_t)(ev[22] | (ev[23] << 8));
    uint16_t mask = (uint16_t)(ev[26] | (ev[27] << 8));
    xs_win_t *w = win_by_xid(xid);
    if (w == NULL) {
        /* Not mapped yet: give it what it asks for; it is placed on map. */
        uint32_t values[4];
        uint32_t n = 0u;
        uint16_t m = 0u;
        if (mask & X11_CWX) { values[n++] = (uint32_t)(int32_t)rx; m |= X11_CWX; }
        if (mask & X11_CWY) { values[n++] = (uint32_t)(int32_t)ry; m |= X11_CWY; }
        if (mask & X11_CWWidth) { values[n++] = rw; m |= X11_CWWidth; }
        if (mask & X11_CWHeight) { values[n++] = rh; m |= X11_CWHeight; }
        if (m != 0u) x11_configure_window(&g_x, xid, m, values, n);
        return;
    }
    uint32_t nw = (mask & X11_CWWidth) ? rw : w->w;
    uint32_t nh = (mask & X11_CWHeight) ? rh : w->h;
    clamp_size(&nw, &nh);
    if (nw != w->w || nh != w->h) {
        w->w = nw;
        w->h = nh;
        if (!slot_free(w->x, w->y, w->w, w->h, w)) {
            find_slot(w->w, w->h, w, &w->x, &w->y);
        }
        win_configure(w);
        if (w->wid != 0u) {
            uint32_t fx = 0, fy = 0, fw = 0, fh = 0;
            if (window_get_rect(w->wid, &fx, &fy, &fw, &fh) == 0) {
                window_set_rect(w->wid, fx, fy, w->w, w->h);
            }
        }
    } else {
        /* Position is ours to choose (the program's idea of where it sits
         * on the X screen means nothing on the ImplusOS desktop), and the
         * size did not change: say so, as ICCCM requires. */
        x11_send_configure_notify(&g_x, xid, (int16_t)w->x, (int16_t)w->y,
                                  (uint16_t)w->w, (uint16_t)w->h);
    }
}

static uint32_t ev32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void handle_x_event(const uint8_t *ev)
{
    switch (ev[0] & 0x7Fu) {
    case X11_MapRequest:
        manage(ev32(ev + 8));
        break;
    case X11_ConfigureRequest:
        handle_configure_request(ev);
        break;
    case X11_MapNotify: {
        uint32_t xid = ev32(ev + 8);
        if (ev[12] != 0u) track_popup(xid); /* override-redirect */
        break;
    }
    case X11_UnmapNotify:
    case X11_DestroyNotify: {
        xs_win_t *w = win_by_xid(ev32(ev + 8));
        if (w != NULL) win_forget(w);
        break;
    }
    case X11_ConfigureNotify: {
        xs_win_t *w = win_by_xid(ev32(ev + 8));
        if (w != NULL && !w->managed) {
            popup_moved(w, (int16_t)(ev[16] | (ev[17] << 8)),
                        (int16_t)(ev[18] | (ev[19] << 8)),
                        (uint16_t)(ev[20] | (ev[21] << 8)),
                        (uint16_t)(ev[22] | (ev[23] << 8)),
                        (uint16_t)(ev[24] | (ev[25] << 8)));
        }
        break;
    }
    case X11_PropertyNotify: {
        xs_win_t *w = win_by_xid(ev32(ev + 4));
        uint32_t atom = ev32(ev + 8);
        if (w != NULL && w->managed && w->wid != 0u) {
            if (atom == X11_ATOM_WM_NAME || atom == A_NET_WM_NAME) {
                char title[XS_TITLE_MAX];
                win_fetch_title(w, title, sizeof title);
                (void)window_set_title(w->wid, title);
            } else if (atom == A_WM_PROTOCOLS) {
                w->delete_protocol = win_supports_delete(w->xid);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* input                                                                */
/* ------------------------------------------------------------------ */

/* The kernel's keycodes are PS/2 scan code set 1, with 0xE0-prefixed keys
 * as 0xE0xx (Kernel/Source/include/kernel/keycodes.h). Linux's KEY_* values
 * 1..88 are set 1 unchanged; the extended and Japanese keys are not. X
 * keycodes are Linux's plus 8 (the evdev keymap). */
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

static bool forward_input(void)
{
    bool forwarded = false;
    input_keyboard_event_t key;
    window_id_t wid = 0u;
    while (window_input_keyboard_poll_from(&key, &wid) > 0) {
        forwarded = true;
        xs_win_t *w = win_by_wid(wid);
        if (w == NULL) continue;
        x_focus(w); /* keys go to the window they were typed into */
        uint16_t code = xsession_linux_keycode(key.keycode);
        if (code == 0u || code + 8u > 255u) continue;
        x11_fake_input(&g_x, key.pressed ? X11_KeyPress : X11_KeyRelease,
                       (uint8_t)(code + 8u), 0, 0);
    }

    input_mouse_event_t mouse;
    while (window_input_mouse_poll_from(&mouse, &wid) > 0) {
        forwarded = true;
        xs_win_t *w = win_by_wid(wid);
        if (w == NULL) continue;
        int32_t ax = w->x + (int32_t)mouse.x;
        int32_t ay = w->y + (int32_t)mouse.y;
        x11_fake_input(&g_x, X11_MotionNotify, 0u, (int16_t)ax, (int16_t)ay);
        uint8_t changed = (uint8_t)(mouse.buttons ^ g_buttons);
        static const struct { uint8_t mask; uint8_t button; } buttons[] = {
            { INPUT_MOUSE_BTN_LEFT,   1u },
            { INPUT_MOUSE_BTN_MIDDLE, 2u },
            { INPUT_MOUSE_BTN_RIGHT,  3u },
        };
        for (uint32_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
            if ((changed & buttons[i].mask) == 0u) continue;
            bool down = (mouse.buttons & buttons[i].mask) != 0u;
            if (down) x_focus(w);
            x11_fake_input(&g_x, down ? X11_ButtonPress : X11_ButtonRelease,
                           buttons[i].button, 0, 0);
        }
        g_buttons = mouse.buttons;
        if (mouse.wheel != 0) {
            uint8_t b = mouse.wheel > 0 ? 4u : 5u;
            int32_t steps = mouse.wheel > 0 ? mouse.wheel : -mouse.wheel;
            for (int32_t s = 0; s < steps && s < 8; ++s) {
                x11_fake_input(&g_x, X11_ButtonPress, b, 0, 0);
                x11_fake_input(&g_x, X11_ButtonRelease, b, 0, 0);
            }
        }
    }
    return forwarded;
}

/* The user resized or closed one of the ImplusOS windows. */
static bool handle_window_requests(void)
{
    bool did = false;
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        xs_win_t *w = &g_wins[i];
        if (!w->used || w->wid == 0u) continue;
        uint32_t nw = 0u, nh = 0u;
        if (window_poll_resize(w->wid, &nw, &nh) <= 0) continue;
        did = true;
        win_refresh_backing(w);
        if (w->managed && w->pixels != NULL && (w->pw != w->w || w->ph != w->h)) {
            w->w = w->pw;
            w->h = w->ph;
            clamp_size(&w->w, &w->h);
            if (!slot_free(w->x, w->y, w->w, w->h, w)) {
                find_slot(w->w, w->h, w, &w->x, &w->y);
            }
            win_configure(w);
        }
        win_paint(w);
    }
    window_id_t closing = 0u;
    while (window_poll_close(&closing) > 0) {
        did = true;
        xs_win_t *w = win_by_wid(closing);
        if (w == NULL || !w->managed) continue;
        char line[96];
        snprintf(line, sizeof line, "[xsession] close 0x%x (%s)\n", w->xid,
                 w->delete_protocol ? "WM_DELETE_WINDOW" : "KillClient");
        serial_write_string(line);
        if (w->delete_protocol) {
            uint32_t data[5] = { A_WM_DELETE_WINDOW, 0u, 0u, 0u, 0u };
            x11_send_client_message(&g_x, w->xid, A_WM_PROTOCOLS, data);
        } else {
            x11_kill_client(&g_x, w->xid);
        }
    }
    return did;
}

/* ------------------------------------------------------------------ */
/* running a client                                                     */
/* ------------------------------------------------------------------ */

int32_t xsession_run(xsession_t *session, const char *path, const char *args)
{
    return xsession_run_env(session, path, args, NULL);
}

int32_t xsession_run_env(xsession_t *session, const char *path, const char *args,
                         const char *const *extra_envp)
{
    if (session == NULL || path == NULL || !session->ready) {
        return -1;
    }

    /* This server's display first: linux_env_add() keeps the first value it
     * is given for a name, and linux_env_add_x11() carries the default. */
    linux_env_t env;
    linux_env_init(&env);
    (void)linux_env_add(&env, g_display_env);
    linux_env_add_x11(&env);
    linux_env_add_desktop(&env);
    for (uint32_t i = 0; extra_envp != NULL && extra_envp[i] != NULL; ++i) {
        (void)linux_env_add(&env, extra_envp[i]);
    }

    int32_t pid = process_spawn_with_env(path, (args != NULL) ? args : "",
                                         linux_env_entries(&env));
    if (pid < 0) {
        return -1;
    }
    if (g_client_count < XS_MAX_CLIENTS) g_clients[g_client_count++] = pid;
    /* Priority 3 is the one the login/desktop launch path uses; it only
     * changes the time slice. Children (Chromium's renderers) inherit it. */
    (void)process_set_priority(pid, 3u);

    int32_t status = 0;
    bool client_done = false;
    uint32_t poll_interval_ms = XSESSION_FRAME_MS;
    for (;;) {
        if (!client_done) {
            int32_t reaped = process_waitpid(pid, &status, 0);
            if (reaped == pid || reaped < 0) {
                client_done = true;
                xs_log_u32("client exited; windows left: ", managed_count());
            }
        }
        /* Done once the program is gone and nothing it (or a program it
         * started on this display) opened is still on screen. */
        if (client_done && managed_count() == 0u) break;
        if (g_x.broken) {
            serial_write_string("[xsession] lost the X server\n");
            break;
        }

        bool did_work = false;
        uint8_t ev[X11_EVENT_SIZE];
        while (x11_next_event(&g_x, ev) > 0) {
            handle_x_event(ev);
            did_work = true;
        }
        if (forward_input()) did_work = true;
        if (handle_window_requests()) did_work = true;
        if (display_kms_mirror_take_dirty()) {
            did_work = true;
            for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
                if (g_wins[i].used && g_wins[i].wid != 0u) win_paint(&g_wins[i]);
            }
        }
        if (did_work) {
            poll_interval_ms = XSESSION_FRAME_MS;
        } else if (poll_interval_ms < XSESSION_ACTIVE_BACKOFF_MAX_MS) {
            poll_interval_ms *= 2u;
            if (poll_interval_ms > XSESSION_ACTIVE_BACKOFF_MAX_MS) {
                poll_interval_ms = XSESSION_ACTIVE_BACKOFF_MAX_MS;
            }
        }
        sleep_ms(poll_interval_ms);
    }
    return status;
}

/* The program is gone and so are its windows; whatever it forked and left
 * running (Chromium's zygote and renderers, a crash handler) goes too, the
 * way a desktop ends an application. A spawned program leads a session of
 * its own, and its children inherit it. */
#define XSESSION_SESSION_GRACE_MS 2000u

static bool clients_alive(void)
{
    for (uint32_t i = 0; i < g_client_count; ++i) {
        if (process_send_signal(-g_clients[i], 0) == 0) return true;
    }
    return false;
}

static void xsession_end_client_sessions(void)
{
    if (clients_alive()) {
        serial_write_string("[xsession] ending what the program left running\n");
        for (uint32_t i = 0; i < g_client_count; ++i) {
            (void)process_send_signal(-g_clients[i], XS_SIGTERM);
        }
        for (uint32_t waited = 0u; waited < XSESSION_SESSION_GRACE_MS &&
                                   clients_alive();
             waited += XORG_POLL_INTERVAL_MS) {
            sleep_ms(XORG_POLL_INTERVAL_MS);
        }
        for (uint32_t i = 0; i < g_client_count; ++i) {
            (void)process_send_signal(-g_clients[i], 9 /* SIGKILL */);
        }
    }
    /* Collect the clients themselves if nobody has (they are our children). */
    for (uint32_t i = 0; i < g_client_count; ++i) {
        int32_t status = 0;
        (void)process_waitpid(g_clients[i], &status, 0);
    }
    g_client_count = 0u;
}

void xsession_close(xsession_t *session)
{
    for (uint32_t i = 0; i < XS_MAX_WINDOWS; ++i) {
        if (g_wins[i].used) win_forget(&g_wins[i]);
    }
    xsession_end_client_sessions();
    if (g_x_connected) {
        x11_disconnect(&g_x);
        g_x_connected = false;
    }
    if (session != NULL && session->xorg_pid > 0) {
        xsession_stop_and_reap(session->xorg_pid);
        session->xorg_pid = -1;
    }
    xsession_free_screen();
    if (session != NULL) session->ready = 0;
}

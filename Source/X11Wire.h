#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * X11Wire -- a minimal native X11 protocol client.
 *
 * ImplusOS userland cannot link libX11/libxcb (they are glibc programs), and
 * XSession.c needs to be an X client of the server it starts: it is that
 * server's window manager (each X top-level becomes an ImplusOS window) and
 * it feeds the server its input through XTEST. This speaks just the slice of
 * the core protocol that takes, over the native AF_UNIX socket, little-endian
 * only. All requests that have a reply are synchronous.
 */

#define X11_EVENT_SIZE 32u

/* Core event codes (X.h). */
#define X11_KeyPress          2
#define X11_KeyRelease        3
#define X11_ButtonPress       4
#define X11_ButtonRelease     5
#define X11_MotionNotify      6
#define X11_FocusIn           9
#define X11_CreateNotify     16
#define X11_DestroyNotify    17
#define X11_UnmapNotify      18
#define X11_MapNotify        19
#define X11_MapRequest       20
#define X11_ConfigureNotify  22
#define X11_ConfigureRequest 23
#define X11_PropertyNotify   28
#define X11_ClientMessage    33

/* Event masks. */
#define X11_StructureNotifyMask    (1u << 17)
#define X11_SubstructureNotifyMask (1u << 19)
#define X11_SubstructureRedirectMask (1u << 20)
#define X11_FocusChangeMask        (1u << 21)
#define X11_PropertyChangeMask     (1u << 22)

/* ConfigureWindow value-mask bits. */
#define X11_CWX           (1u << 0)
#define X11_CWY           (1u << 1)
#define X11_CWWidth       (1u << 2)
#define X11_CWHeight      (1u << 3)
#define X11_CWBorderWidth (1u << 4)
#define X11_CWStackMode   (1u << 6)

#define X11_IsViewable 2u
#define X11_InputOnly  2u

#define X11_ATOM_ATOM     4u
#define X11_ATOM_CARDINAL 6u
#define X11_ATOM_STRING  31u
#define X11_ATOM_WINDOW  33u
#define X11_ATOM_WM_NAME 39u
#define X11_ATOM_WM_NORMAL_HINTS 40u
#define X11_ATOM_WM_TRANSIENT_FOR 68u

#define X11_RBUF_SIZE (256u * 1024u)
#define X11_EVQ_SIZE  256u

typedef struct {
    int32_t  fd;
    bool     broken;
    uint32_t root;
    uint16_t screen_w;
    uint16_t screen_h;
    uint32_t id_base;
    uint32_t id_mask;
    uint32_t id_next;
    uint16_t seq;            /* sequence number of the last request sent */
    uint8_t  xtest_opcode;   /* 0 when XTEST is missing */
    uint8_t  rbuf[X11_RBUF_SIZE];
    uint32_t rlen;
    uint8_t  evq[X11_EVQ_SIZE][X11_EVENT_SIZE];
    uint32_t ev_head;
    uint32_t ev_tail;
    uint32_t ev_count;
    /* Most recent X error, for the caller's log. */
    uint8_t  last_error;
    uint8_t  last_error_major;
    uint16_t last_error_seq;
} x11_conn_t;

/* Connect to display :`display` and run the connection setup. 0 or < 0. */
int32_t x11_connect(x11_conn_t *c, uint32_t display, uint32_t timeout_ms);
void    x11_disconnect(x11_conn_t *c);

/* Read whatever the server has sent and queue its events. Returns 1 when an
 * event is available afterwards. Never blocks. */
int32_t x11_pump(x11_conn_t *c);
/* Pop one queued event (32 bytes). 1 when one was returned. */
int32_t x11_next_event(x11_conn_t *c, uint8_t out[X11_EVENT_SIZE]);

/* A round trip: every request sent before it has been processed when it
 * returns. Returns 0, or -1 when the request it covered produced an error
 * (the caller's way to check e.g. "another window manager is running"). */
int32_t x11_sync(x11_conn_t *c);

uint32_t x11_intern_atom(x11_conn_t *c, const char *name, bool only_if_exists);

/* GetProperty. Copies up to `cap` bytes of the value into `out` and returns
 * the number of bytes copied (-1 on error, 0 when absent). */
int32_t x11_get_property(x11_conn_t *c, uint32_t window, uint32_t property,
                         uint32_t type, void *out, uint32_t cap,
                         uint32_t *out_type, uint8_t *out_format);

int32_t x11_get_geometry(x11_conn_t *c, uint32_t window, int16_t *x, int16_t *y,
                         uint16_t *w, uint16_t *h, uint16_t *border);
int32_t x11_get_window_attributes(x11_conn_t *c, uint32_t window,
                                  bool *override_redirect, uint8_t *map_state,
                                  uint16_t *window_class);
/* QueryTree: up to `cap` children of `window`, bottom to top. */
int32_t x11_query_tree(x11_conn_t *c, uint32_t window, uint32_t *children,
                       uint32_t cap);

void x11_select_input(x11_conn_t *c, uint32_t window, uint32_t event_mask);
void x11_configure_window(x11_conn_t *c, uint32_t window, uint16_t mask,
                          const uint32_t *values, uint32_t count);
void x11_map_window(x11_conn_t *c, uint32_t window);
void x11_set_input_focus(x11_conn_t *c, uint32_t window);
void x11_kill_client(x11_conn_t *c, uint32_t resource);
void x11_change_property32(x11_conn_t *c, uint32_t window, uint32_t property,
                           uint32_t type, const uint32_t *values,
                           uint32_t count);
/* ClientMessage of `type` carrying five 32-bit values, sent to `window`. */
void x11_send_client_message(x11_conn_t *c, uint32_t window, uint32_t type,
                             const uint32_t data[5]);
/* The synthetic ConfigureNotify ICCCM 4.1.5 owes a client whose
 * ConfigureRequest was not honoured as asked. */
void x11_send_configure_notify(x11_conn_t *c, uint32_t window, int16_t x,
                               int16_t y, uint16_t w, uint16_t h);

/* XTEST FakeInput. `type` is X11_KeyPress..X11_MotionNotify; `detail` the
 * keycode or button; x/y the root position for MotionNotify. */
void x11_fake_input(x11_conn_t *c, uint8_t type, uint8_t detail, int16_t x,
                    int16_t y);

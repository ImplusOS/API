#include "X11Wire.h"

#include <stdio.h>
#include <string.h>

#include "Process.h"
#include "Serial.h"

extern uint64_t syscall1(uint64_t, uint64_t);
extern uint64_t syscall2(uint64_t, uint64_t, uint64_t);
extern uint64_t syscall3(uint64_t, uint64_t, uint64_t, uint64_t);

/* Native AF_UNIX (Kernel/Source/Core/syscall/Syscall_Main.h). recv never
 * blocks: -11 when nothing is queued, 0 at end of stream. send takes at
 * most 64 KiB per call and returns -11 while the peer's ring is full. */
#define SYS_UNIX_SOCKET  220
#define SYS_UNIX_CONNECT 224
#define SYS_UNIX_SEND    225
#define SYS_UNIX_RECV    226
#define SYS_UNIX_CLOSE   229

#define X11_EAGAIN (-11)
#define X11_REPLY_TIMEOUT_MS 5000u

/* Core request opcodes. */
#define OP_ChangeWindowAttributes 2
#define OP_GetWindowAttributes    3
#define OP_MapWindow              8
#define OP_ConfigureWindow       12
#define OP_GetGeometry           14
#define OP_QueryTree             15
#define OP_InternAtom            16
#define OP_ChangeProperty        18
#define OP_GetProperty           20
#define OP_SendEvent             25
#define OP_SetInputFocus         42
#define OP_GetInputFocus         43
#define OP_QueryExtension        98
#define OP_KillClient           113

#define XTEST_FakeInput 2

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint32_t pad4(uint32_t n) { return (n + 3u) & ~3u; }

static int32_t raw_send_all(x11_conn_t *c, const uint8_t *data, uint32_t len)
{
    uint32_t waited = 0u;
    while (len > 0u && !c->broken) {
        uint32_t chunk = len > 32768u ? 32768u : len;
        int64_t n = (int64_t)syscall3(SYS_UNIX_SEND, (uint64_t)c->fd,
                                      (uint64_t)(uintptr_t)data, chunk);
        if (n > 0) {
            data += n;
            len -= (uint32_t)n;
            waited = 0u;
            continue;
        }
        if (n == X11_EAGAIN || n == 0) {
            /* The server is busy; let it drain its end. Its replies to us
             * may be what fills the pipe, so keep reading meanwhile. */
            (void)x11_pump(c);
            if (++waited > X11_REPLY_TIMEOUT_MS) {
                c->broken = true;
                return -1;
            }
            sleep_ms(1u);
            continue;
        }
        c->broken = true;
        return -1;
    }
    return c->broken ? -1 : 0;
}

/* Send one request; returns its sequence number. */
static uint16_t send_request(x11_conn_t *c, uint8_t *req, uint32_t len)
{
    /* Requests are padded to 4 bytes; the length field counts words. */
    put16(req + 2, len / 4u);
    c->seq = (uint16_t)(c->seq + 1u);
    (void)raw_send_all(c, req, len);
    return c->seq;
}

static void queue_event(x11_conn_t *c, const uint8_t *ev)
{
    if (c->ev_count >= X11_EVQ_SIZE) {
        return; /* overrun: the oldest are what the caller has seen least */
    }
    memcpy(c->evq[c->ev_head], ev, X11_EVENT_SIZE);
    c->ev_head = (c->ev_head + 1u) % X11_EVQ_SIZE;
    ++c->ev_count;
}

/* Length of the complete message at the front of rbuf, or 0 if more bytes
 * are needed. */
static uint32_t front_message_len(const x11_conn_t *c)
{
    if (c->rlen < 32u) return 0u;
    uint8_t kind = c->rbuf[0];
    uint32_t len = 32u;
    if (kind == 1u || (kind & 0x7Fu) == 35u) { /* reply / GenericEvent */
        len += get32(c->rbuf + 4) * 4u;
    }
    return c->rlen >= len ? len : 0u;
}

static void consume_front(x11_conn_t *c, uint32_t len)
{
    memmove(c->rbuf, c->rbuf + len, c->rlen - len);
    c->rlen -= len;
}

static void read_socket(x11_conn_t *c)
{
    while (!c->broken && c->rlen < X11_RBUF_SIZE) {
        uint32_t room = X11_RBUF_SIZE - c->rlen;
        if (room > 32768u) room = 32768u;
        int64_t n = (int64_t)syscall3(SYS_UNIX_RECV, (uint64_t)c->fd,
                                      (uint64_t)(uintptr_t)(c->rbuf + c->rlen),
                                      room);
        if (n > 0) {
            c->rlen += (uint32_t)n;
            continue;
        }
        if (n == 0) {
            c->broken = true; /* the server went away */
        }
        break;
    }
}

int32_t x11_pump(x11_conn_t *c)
{
    if (c == NULL || c->fd < 0) return 0;
    read_socket(c);
    for (;;) {
        uint32_t len = front_message_len(c);
        if (len == 0u) break;
        uint8_t kind = c->rbuf[0];
        if (kind == 0u) {
            c->last_error = c->rbuf[1];
            c->last_error_seq = get16(c->rbuf + 2);
            c->last_error_major = c->rbuf[10];
        } else if (kind != 1u) {
            queue_event(c, c->rbuf);
        }
        /* A reply nobody is waiting for: dropped. Every request with a
         * reply is waited for synchronously. */
        consume_front(c, len);
    }
    return c->ev_count > 0u ? 1 : 0;
}

int32_t x11_next_event(x11_conn_t *c, uint8_t out[X11_EVENT_SIZE])
{
    if (c == NULL) return 0;
    if (c->ev_count == 0u) (void)x11_pump(c);
    if (c->ev_count == 0u) return 0;
    memcpy(out, c->evq[c->ev_tail], X11_EVENT_SIZE);
    c->ev_tail = (c->ev_tail + 1u) % X11_EVQ_SIZE;
    --c->ev_count;
    return 1;
}

/* Wait for the reply to `seq`. Copies up to `cap` bytes of it into `out` and
 * returns its full length, or -1 for an error reply / a dead connection. */
static int32_t wait_reply(x11_conn_t *c, uint16_t seq, uint8_t *out,
                          uint32_t cap)
{
    uint64_t deadline = get_uptime_ms() + X11_REPLY_TIMEOUT_MS;
    while (!c->broken) {
        read_socket(c);
        for (;;) {
            uint32_t len = front_message_len(c);
            if (len == 0u) break;
            uint8_t kind = c->rbuf[0];
            uint16_t mseq = get16(c->rbuf + 2);
            if (kind == 1u && mseq == seq) {
                uint32_t n = len < cap ? len : cap;
                if (out != NULL) memcpy(out, c->rbuf, n);
                consume_front(c, len);
                return (int32_t)len;
            }
            if (kind == 0u) {
                c->last_error = c->rbuf[1];
                c->last_error_seq = mseq;
                c->last_error_major = c->rbuf[10];
                consume_front(c, len);
                if (mseq == seq) return -1;
                continue;
            }
            if (kind != 1u) queue_event(c, c->rbuf);
            consume_front(c, len);
        }
        if (get_uptime_ms() > deadline) {
            serial_write_string("[x11] reply timed out\n");
            c->broken = true;
            break;
        }
        sleep_ms(1u);
    }
    return -1;
}

int32_t x11_connect(x11_conn_t *c, uint32_t display, uint32_t timeout_ms)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    char path[48];
    snprintf(path, sizeof path, "/tmp/.X11-unix/X%u", display);

    int64_t fd = (int64_t)syscall1(SYS_UNIX_SOCKET, 1u);
    if (fd < 0) return -1;
    c->fd = (int32_t)fd;
    uint64_t start = get_uptime_ms();
    for (;;) {
        int64_t rc = (int64_t)syscall2(SYS_UNIX_CONNECT, (uint64_t)c->fd,
                                       (uint64_t)(uintptr_t)path);
        if (rc == 0) break;
        if (get_uptime_ms() - start > timeout_ms) {
            x11_disconnect(c);
            return -1;
        }
        sleep_ms(10u);
    }

    uint8_t setup[12];
    memset(setup, 0, sizeof setup);
    setup[0] = 'l';
    put16(setup + 2, 11u);
    if (raw_send_all(c, setup, sizeof setup) < 0) {
        x11_disconnect(c);
        return -1;
    }

    /* Setup reply: 8-byte header, then `length` words. */
    uint64_t deadline = get_uptime_ms() + timeout_ms;
    for (;;) {
        read_socket(c);
        if (c->rlen >= 8u && c->rlen >= 8u + get16(c->rbuf + 6) * 4u) break;
        if (c->broken || get_uptime_ms() > deadline) {
            x11_disconnect(c);
            return -1;
        }
        sleep_ms(2u);
    }
    if (c->rbuf[0] != 1u) {
        serial_write_string("[x11] connection refused by the server\n");
        x11_disconnect(c);
        return -1;
    }
    const uint8_t *s = c->rbuf + 8;
    uint32_t total = 8u + get16(c->rbuf + 6) * 4u;
    c->id_base = get32(s + 4);
    c->id_mask = get32(s + 8);
    uint16_t vendor_len = get16(s + 16);
    uint8_t formats = s[21];
    const uint8_t *screen = s + 32 + pad4(vendor_len) + formats * 8u;
    c->root = get32(screen + 0);
    c->screen_w = get16(screen + 20);
    c->screen_h = get16(screen + 22);
    consume_front(c, total);

    /* XTEST: how input reaches the server. */
    {
        const char *name = "XTEST";
        uint32_t nlen = (uint32_t)strlen(name);
        uint8_t req[8 + 8];
        memset(req, 0, sizeof req);
        req[0] = OP_QueryExtension;
        put16(req + 4, nlen);
        memcpy(req + 8, name, nlen);
        uint16_t seq = send_request(c, req, 8u + pad4(nlen));
        uint8_t rep[32];
        if (wait_reply(c, seq, rep, sizeof rep) >= 32 && rep[8] != 0u) {
            c->xtest_opcode = rep[9];
        } else {
            serial_write_string("[x11] the server has no XTEST; no input\n");
        }
    }
    return c->broken ? -1 : 0;
}

void x11_disconnect(x11_conn_t *c)
{
    if (c == NULL) return;
    if (c->fd >= 0) {
        (void)syscall1(SYS_UNIX_CLOSE, (uint64_t)c->fd);
    }
    c->fd = -1;
    c->broken = true;
}

int32_t x11_sync(x11_conn_t *c)
{
    uint8_t req[4] = { OP_GetInputFocus, 0, 0, 0 };
    uint16_t first = (uint16_t)(c->seq + 1u);
    uint8_t before = c->last_error;
    uint16_t before_seq = c->last_error_seq;
    uint16_t seq = send_request(c, req, sizeof req);
    if (wait_reply(c, seq, NULL, 0u) < 0) return -1;
    (void)first;
    /* An error for a request sent since the previous sync? */
    if (c->last_error != before || c->last_error_seq != before_seq) {
        return -1;
    }
    return 0;
}

uint32_t x11_intern_atom(x11_conn_t *c, const char *name, bool only_if_exists)
{
    uint32_t nlen = (uint32_t)strlen(name);
    if (nlen > 200u) return 0u;
    uint8_t req[8 + 204];
    memset(req, 0, sizeof req);
    req[0] = OP_InternAtom;
    req[1] = only_if_exists ? 1u : 0u;
    put16(req + 4, nlen);
    memcpy(req + 8, name, nlen);
    uint16_t seq = send_request(c, req, 8u + pad4(nlen));
    uint8_t rep[32];
    if (wait_reply(c, seq, rep, sizeof rep) < 32) return 0u;
    return get32(rep + 8);
}

int32_t x11_get_property(x11_conn_t *c, uint32_t window, uint32_t property,
                         uint32_t type, void *out, uint32_t cap,
                         uint32_t *out_type, uint8_t *out_format)
{
    uint8_t req[24];
    memset(req, 0, sizeof req);
    req[0] = OP_GetProperty;
    put32(req + 4, window);
    put32(req + 8, property);
    put32(req + 12, type);
    put32(req + 16, 0u);
    put32(req + 20, (cap + 3u) / 4u);
    uint16_t seq = send_request(c, req, sizeof req);
    static uint8_t rep[32 + 4096];
    int32_t len = wait_reply(c, seq, rep, sizeof rep);
    if (len < 32) return -1;
    uint8_t format = rep[1];
    uint32_t value_type = get32(rep + 8);
    uint32_t items = get32(rep + 16);
    uint32_t bytes = items * (format / 8u);
    if (out_type) *out_type = value_type;
    if (out_format) *out_format = format;
    if (value_type == 0u) return 0;
    if (bytes > cap) bytes = cap;
    if (bytes > (uint32_t)len - 32u) bytes = (uint32_t)len - 32u;
    if (bytes > sizeof rep - 32u) bytes = sizeof rep - 32u;
    memcpy(out, rep + 32, bytes);
    return (int32_t)bytes;
}

int32_t x11_get_geometry(x11_conn_t *c, uint32_t window, int16_t *x, int16_t *y,
                         uint16_t *w, uint16_t *h, uint16_t *border)
{
    uint8_t req[8];
    memset(req, 0, sizeof req);
    req[0] = OP_GetGeometry;
    put32(req + 4, window);
    uint16_t seq = send_request(c, req, sizeof req);
    uint8_t rep[32];
    if (wait_reply(c, seq, rep, sizeof rep) < 32) return -1;
    if (x) *x = (int16_t)get16(rep + 12);
    if (y) *y = (int16_t)get16(rep + 14);
    if (w) *w = get16(rep + 16);
    if (h) *h = get16(rep + 18);
    if (border) *border = get16(rep + 20);
    return 0;
}

int32_t x11_get_window_attributes(x11_conn_t *c, uint32_t window,
                                  bool *override_redirect, uint8_t *map_state,
                                  uint16_t *window_class)
{
    uint8_t req[8];
    memset(req, 0, sizeof req);
    req[0] = OP_GetWindowAttributes;
    put32(req + 4, window);
    uint16_t seq = send_request(c, req, sizeof req);
    uint8_t rep[44];
    if (wait_reply(c, seq, rep, sizeof rep) < 44) return -1;
    if (window_class) *window_class = get16(rep + 12);
    if (map_state) *map_state = rep[26];
    if (override_redirect) *override_redirect = rep[27] != 0u;
    return 0;
}

int32_t x11_query_tree(x11_conn_t *c, uint32_t window, uint32_t *children,
                       uint32_t cap)
{
    uint8_t req[8];
    memset(req, 0, sizeof req);
    req[0] = OP_QueryTree;
    put32(req + 4, window);
    uint16_t seq = send_request(c, req, sizeof req);
    static uint8_t rep[32 + 4 * 512];
    int32_t len = wait_reply(c, seq, rep, sizeof rep);
    if (len < 32) return -1;
    uint32_t n = get16(rep + 16);
    if (n > cap) n = cap;
    if (n > (sizeof rep - 32u) / 4u) n = (sizeof rep - 32u) / 4u;
    for (uint32_t i = 0; i < n; ++i) children[i] = get32(rep + 32 + i * 4u);
    return (int32_t)n;
}

void x11_select_input(x11_conn_t *c, uint32_t window, uint32_t event_mask)
{
    uint8_t req[16];
    memset(req, 0, sizeof req);
    req[0] = OP_ChangeWindowAttributes;
    put32(req + 4, window);
    put32(req + 8, 1u << 11); /* CWEventMask */
    put32(req + 12, event_mask);
    (void)send_request(c, req, sizeof req);
}

void x11_configure_window(x11_conn_t *c, uint32_t window, uint16_t mask,
                          const uint32_t *values, uint32_t count)
{
    uint8_t req[12 + 7 * 4];
    if (count > 7u) count = 7u;
    memset(req, 0, sizeof req);
    req[0] = OP_ConfigureWindow;
    put32(req + 4, window);
    put16(req + 8, mask);
    for (uint32_t i = 0; i < count; ++i) put32(req + 12 + i * 4u, values[i]);
    (void)send_request(c, req, 12u + count * 4u);
}

void x11_map_window(x11_conn_t *c, uint32_t window)
{
    uint8_t req[8];
    memset(req, 0, sizeof req);
    req[0] = OP_MapWindow;
    put32(req + 4, window);
    (void)send_request(c, req, sizeof req);
}

void x11_set_input_focus(x11_conn_t *c, uint32_t window)
{
    uint8_t req[12];
    memset(req, 0, sizeof req);
    req[0] = OP_SetInputFocus;
    req[1] = 1u; /* RevertToPointerRoot */
    put32(req + 4, window);
    put32(req + 8, 0u); /* CurrentTime */
    (void)send_request(c, req, sizeof req);
}

void x11_kill_client(x11_conn_t *c, uint32_t resource)
{
    uint8_t req[8];
    memset(req, 0, sizeof req);
    req[0] = OP_KillClient;
    put32(req + 4, resource);
    (void)send_request(c, req, sizeof req);
}

void x11_change_property32(x11_conn_t *c, uint32_t window, uint32_t property,
                           uint32_t type, const uint32_t *values,
                           uint32_t count)
{
    uint8_t req[24 + 16 * 4];
    if (count > 16u) count = 16u;
    memset(req, 0, sizeof req);
    req[0] = OP_ChangeProperty;
    req[1] = 0u; /* Replace */
    put32(req + 4, window);
    put32(req + 8, property);
    put32(req + 12, type);
    req[16] = 32u;
    put32(req + 20, count);
    for (uint32_t i = 0; i < count; ++i) put32(req + 24 + i * 4u, values[i]);
    (void)send_request(c, req, 24u + count * 4u);
}

static void send_event(x11_conn_t *c, uint32_t window, uint32_t mask,
                       const uint8_t ev[32])
{
    uint8_t req[44];
    memset(req, 0, sizeof req);
    req[0] = OP_SendEvent;
    req[1] = 0u; /* propagate: false */
    put32(req + 4, window);
    put32(req + 8, mask);
    memcpy(req + 12, ev, 32u);
    (void)send_request(c, req, sizeof req);
}

void x11_send_client_message(x11_conn_t *c, uint32_t window, uint32_t type,
                             const uint32_t data[5])
{
    uint8_t ev[32];
    memset(ev, 0, sizeof ev);
    ev[0] = X11_ClientMessage;
    ev[1] = 32u;
    put32(ev + 4, window);
    put32(ev + 8, type);
    for (uint32_t i = 0; i < 5u; ++i) put32(ev + 12 + i * 4u, data[i]);
    send_event(c, window, 0u, ev);
}

void x11_send_configure_notify(x11_conn_t *c, uint32_t window, int16_t x,
                               int16_t y, uint16_t w, uint16_t h)
{
    uint8_t ev[32];
    memset(ev, 0, sizeof ev);
    ev[0] = X11_ConfigureNotify;
    put32(ev + 4, window);
    put32(ev + 8, window);
    put32(ev + 12, 0u);
    put16(ev + 16, (uint16_t)x);
    put16(ev + 18, (uint16_t)y);
    put16(ev + 20, w);
    put16(ev + 22, h);
    send_event(c, window, X11_StructureNotifyMask, ev);
}

void x11_fake_input(x11_conn_t *c, uint8_t type, uint8_t detail, int16_t x,
                    int16_t y)
{
    if (c->xtest_opcode == 0u) return;
    uint8_t req[36];
    memset(req, 0, sizeof req);
    req[0] = c->xtest_opcode;
    req[1] = XTEST_FakeInput;
    req[4] = type;
    req[5] = detail;
    put32(req + 8, 0u);      /* time: now */
    put32(req + 12, type == X11_MotionNotify ? c->root : 0u);
    put16(req + 24, (uint16_t)x);
    put16(req + 26, (uint16_t)y);
    req[35] = 0u;            /* deviceid: core */
    (void)send_request(c, req, sizeof req);
}

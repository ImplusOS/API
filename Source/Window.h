#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "Input.h"

typedef uint32_t window_id_t;

window_id_t window_create(uint32_t width, uint32_t height, const char *title);
window_id_t window_create_ex(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                             uint32_t bg_color, const char *title);
void window_destroy(window_id_t wid);
void window_set_rect(window_id_t wid, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void window_show(window_id_t wid);
void window_raise(window_id_t wid);
void window_set_focus(window_id_t wid);
int32_t window_get_rect(window_id_t wid, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
/* Screen rectangle of the content area, below the title bar. */
int32_t window_get_content_rect(window_id_t wid, uint32_t *x, uint32_t *y,
                                uint32_t *w, uint32_t *h);
int32_t window_subscribe_keyboard(window_id_t wid);
int32_t window_subscribe_mouse(window_id_t wid);
/* Mark a window as shell chrome rather than an application: the compositor
 * draws it no frame and the taskbar does not list it. It still stacks,
 * focuses and receives input like any other window -- the Wayland session's
 * output is the one surface that wants this. */
void window_set_system(window_id_t wid, bool is_system);
/* Show `title` (UTF-8) in the title bar and the taskbar. */
int32_t window_set_title(window_id_t wid, const char *title);
int32_t window_set_surface_opaque(window_id_t wid, bool opaque);
int32_t window_get_wm_pid(void);
int32_t window_register_service(void);

/* Input. The _from variants also name the window the event was sent to
 * (for clients with several windows). */
int32_t window_input_keyboard_poll(input_keyboard_event_t *out);
int32_t window_input_mouse_poll(input_mouse_event_t *out);
int32_t window_input_keyboard_poll_from(input_keyboard_event_t *out,
                                        window_id_t *out_wid);
int32_t window_input_mouse_poll_from(input_mouse_event_t *out,
                                     window_id_t *out_wid);

/* The window's pixels, shared with the compositor. Draw, then damage. */
uint32_t *window_get_backing_store(window_id_t wid, uint32_t *out_w, uint32_t *out_h);
void window_damage(window_id_t wid, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* 1 when the compositor has resized `wid` (0: any window) since the last call,
 * with the new content size written to out_w and out_h, 0 when it has not. A
 * resize replaces the backing store, so the answer to a 1 is to call
 * window_get_backing_store() again -- the pointer held from before it is no
 * longer what the compositor reads from. Like the input polls, this drains
 * the IPC port, so keep calling it from the same loop that polls input. */
int32_t window_poll_resize(window_id_t wid, uint32_t *out_w, uint32_t *out_h);

/* true: the close button sends this process a close request (read it with
 * window_poll_close()) instead of destroying the window, and the compositor
 * never kills this process when such a window goes away. */
int32_t window_set_close_policy(window_id_t wid, bool owner_handles);
/* 1 and *out_wid when a close was requested for one of those windows. */
int32_t window_poll_close(window_id_t *out_wid);

/* Desktop services. */
void window_show_notification(const char *title, const char *message);
void window_show_error(const char *title, const char *message);
/* Retheme the shell with `accent` in light or dark (see Theme.h).
 * Applications pick it up the next time they start. */
void window_set_theme(uint32_t accent, bool dark);
/* Re-read the wallpaper from disk and repaint the desktop. */
void window_reload_background(void);

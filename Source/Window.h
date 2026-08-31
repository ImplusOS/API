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
void window_hide(window_id_t wid);
void window_raise(window_id_t wid);
void window_lower(window_id_t wid);
void window_set_focus(window_id_t wid);
int32_t window_get_rect(window_id_t wid, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
window_id_t window_get_focus(void);
int32_t window_subscribe_keyboard(window_id_t wid);
int32_t window_subscribe_mouse(window_id_t wid);
int32_t window_unsubscribe_input(window_id_t wid);
void    window_set_system(window_id_t wid, bool is_system);
void window_set_bg_color(window_id_t wid, uint32_t color);
void window_clear(window_id_t wid);
int32_t window_get_wm_pid(void);
int32_t window_register_service(void);
int32_t window_input_keyboard_poll(input_keyboard_event_t *out);
int32_t window_input_mouse_poll(input_mouse_event_t *out);
int32_t window_input_keyboard_wait(input_keyboard_event_t *out);
int32_t window_input_mouse_wait(input_mouse_event_t *out);
int32_t window_input_keyboard_pending(void);
int32_t window_input_mouse_pending(void);

int32_t window_set_layout_xml(window_id_t wid, const char *xml_str, uint32_t xml_len);
int32_t window_load_layout(window_id_t wid, const char *xml_path);
void window_draw_text(window_id_t wid, uint32_t x, uint32_t y, const char *text, uint32_t color, float font_size);
void window_show_notification(const char *title, const char *message);
uint32_t window_get_capabilities(void);
uint32_t *window_get_backing_store(window_id_t wid, uint32_t *out_w, uint32_t *out_h);
void window_release_backing_store(window_id_t wid);
void window_damage(window_id_t wid, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void window_begin_transaction(window_id_t wid);
void window_end_transaction(window_id_t wid);
int32_t window_set_icon_path(window_id_t wid, const char *path);
int32_t window_set_surface_opaque(window_id_t wid, bool opaque);

void window_show_dialog(uint32_t type, const char *title, const char *message);
void window_show_info(const char *title, const char *message);
void window_show_warning(const char *title, const char *message);
void window_show_error(const char *title, const char *message);

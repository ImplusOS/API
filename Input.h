#pragma once

#include <stdint.h>

#define INPUT_KBD_MOD_SHIFT (1u << 0)
#define INPUT_KBD_MOD_CTRL  (1u << 1)
#define INPUT_KBD_MOD_ALT   (1u << 2)
#define INPUT_KBD_MOD_CAPS  (1u << 3)

#define INPUT_MOUSE_BTN_LEFT   (1u << 0)
#define INPUT_MOUSE_BTN_RIGHT  (1u << 1)
#define INPUT_MOUSE_BTN_MIDDLE (1u << 2)

typedef struct __attribute__((packed)) {
    uint16_t keycode;
    uint8_t pressed;
    uint8_t ascii;
    uint8_t modifiers;
    uint8_t reserved[3];
} input_keyboard_event_t;

typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
    int8_t wheel;
    uint8_t reserved[2];
} input_mouse_event_t;

int32_t input_read_keyboard(input_keyboard_event_t *event_out);
int32_t input_read_mouse(input_mouse_event_t *event_out);

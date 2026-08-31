#pragma once

#include <stdint.h>

#define DISPLAY_MAX_MONITORS 16u
#define DISPLAY_MAX_MONITOR_NAME 32u

#define DISPLAY_OUTPUT_UNKNOWN        0u
#define DISPLAY_OUTPUT_FRAMEBUFFER    1u
#define DISPLAY_OUTPUT_VIRTIO_SCANOUT 2u
#define DISPLAY_OUTPUT_HDMI           3u
#define DISPLAY_OUTPUT_DISPLAYPORT    4u
#define DISPLAY_OUTPUT_VGA            5u

#define DISPLAY_MONITOR_FLAG_CONNECTED      (1u << 0)
#define DISPLAY_MONITOR_FLAG_PRIMARY        (1u << 1)
#define DISPLAY_MONITOR_FLAG_MODESET        (1u << 2)
#define DISPLAY_MONITOR_FLAG_HOTPLUG        (1u << 3)
#define DISPLAY_MONITOR_FLAG_SYNTHETIC_MODE (1u << 4)

#define DISPLAY_MODE_FLAG_CURRENT   (1u << 0)
#define DISPLAY_MODE_FLAG_PREFERRED (1u << 1)
#define DISPLAY_MODE_FLAG_SYNTHETIC (1u << 2)

typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint32_t monitor_count;
    uint32_t primary_monitor;
    uint32_t flags;
    int32_t  origin_x;
    int32_t  origin_y;
    uint32_t width;
    uint32_t height;
} display_topology_t;

typedef struct __attribute__((packed)) {
    uint32_t index;
    uint32_t id;
    uint32_t flags;
    uint32_t output_type;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t physical_width_mm;
    uint32_t physical_height_mm;
    uint32_t refresh_millihz;
    uint32_t current_mode;
    uint32_t mode_count;
    uint32_t generation;
    char     name[DISPLAY_MAX_MONITOR_NAME];
    char     output_name[DISPLAY_MAX_MONITOR_NAME];
} display_monitor_info_t;

typedef struct __attribute__((packed)) {
    uint32_t monitor_index;
    uint32_t mode_index;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bits_per_pixel;
    uint32_t refresh_millihz;
    char     name[DISPLAY_MAX_MONITOR_NAME];
} display_mode_info_t;

typedef struct __attribute__((packed)) {
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
} display_rect_t;

int32_t graphics_init(uint32_t window_id);

void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
uint32_t get_pixel(uint32_t x, uint32_t y);
void draw_present(void);
void draw_present_rects(const display_rect_t *rects, uint32_t count);

uint32_t get_display_width(void);
uint32_t get_display_height(void);
void *sys_get_display_framebuffer(void);
int64_t display_get_topology(display_topology_t *out_topology);
int64_t display_get_monitor_info(uint32_t monitor_index,
                                 display_monitor_info_t *out_info);
int64_t display_get_monitor_mode_info(uint32_t monitor_index,
                                      uint32_t mode_index,
                                      display_mode_info_t *out_info);
int64_t display_set_monitor_mode(uint32_t monitor_index, uint32_t mode_index);

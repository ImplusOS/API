#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct ft_font ft_font_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    int32_t  bearing_x;
    int32_t  bearing_y;
    int32_t  advance;
    uint8_t *buffer;
} ft_glyph_bitmap_t;

typedef struct {
    int32_t x;
    int32_t y;
} ft_vec2_t;

int ft_font_init(void);
void ft_font_shutdown(void);

ft_font_t *ft_font_load_from_memory(const uint8_t *data, size_t size,
                                     uint32_t pixel_size);
void ft_font_close(ft_font_t *font);

int ft_font_load_glyph(ft_font_t *font, uint32_t codepoint,
                        ft_glyph_bitmap_t *out_bitmap);
void ft_font_free_bitmap(ft_glyph_bitmap_t *bitmap);

ft_vec2_t ft_font_get_kerning(ft_font_t *font, uint32_t left,
                               uint32_t right);

int ft_font_get_line_height(ft_font_t *font);
int ft_font_get_ascender(ft_font_t *font);
int ft_font_get_descender(ft_font_t *font);

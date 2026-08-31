#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
} jpeg_image_t;

int jpeg_decode_rgba_from_memory(const uint8_t *data, size_t size,
                                 jpeg_image_t *out_image);
void jpeg_free_image(jpeg_image_t *image);


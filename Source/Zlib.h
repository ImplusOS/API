#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    size_t size;
    uint8_t *data;
} zlib_buffer_t;

// Returns 0 on success, negative value on error
int zlib_compress(const uint8_t *in, size_t in_len, zlib_buffer_t *out);
int zlib_decompress(const uint8_t *in, size_t in_len, zlib_buffer_t *out);

// Free the buffer data
void zlib_free_buffer(zlib_buffer_t *buf);

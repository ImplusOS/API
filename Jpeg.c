#include "Jpeg.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
} jpeg_api_error_t;

static void jpeg_api_error_exit(j_common_ptr cinfo)
{
    jpeg_api_error_t *error = (jpeg_api_error_t *)cinfo->err;
    longjmp(error->jump, 1);
}

void jpeg_free_image(jpeg_image_t *image)
{
    if (!image) return;
    free(image->rgba);
    image->rgba = NULL;
    image->width = 0u;
    image->height = 0u;
}

int jpeg_decode_rgba_from_memory(const uint8_t *data, size_t size,
                                 jpeg_image_t *out_image)
{
    if (!data || size == 0u || !out_image) return -1;

    struct jpeg_decompress_struct cinfo;
    jpeg_api_error_t jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_api_error_exit;

    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

#ifdef JCS_ALPHA_EXTENSIONS
    cinfo.out_color_space = JCS_EXT_RGBA;
#else
    cinfo.out_color_space = JCS_RGB;
#endif

    jpeg_start_decompress(&cinfo);
    if (cinfo.output_width == 0u || cinfo.output_height == 0u) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    size_t pixel_count = (size_t)cinfo.output_width * (size_t)cinfo.output_height;
    if (pixel_count > ((size_t)-1) / 4u) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4u);
    if (!rgba) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

#ifdef JCS_ALPHA_EXTENSIONS
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t *row = rgba + ((size_t)cinfo.output_scanline *
                               (size_t)cinfo.output_width * 4u);
        JSAMPROW rows[1] = { row };
        jpeg_read_scanlines(&cinfo, rows, 1u);
    }
#else
    size_t row_rgb_size = (size_t)cinfo.output_width * 3u;
    uint8_t *row_rgb = (uint8_t *)malloc(row_rgb_size);
    if (!row_rgb) {
        free(rgba);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        uint32_t y = cinfo.output_scanline;
        JSAMPROW rows[1] = { row_rgb };
        jpeg_read_scanlines(&cinfo, rows, 1u);
        uint8_t *dst = rgba + ((size_t)y * (size_t)cinfo.output_width * 4u);
        for (uint32_t x = 0u; x < cinfo.output_width; ++x) {
            dst[x * 4u + 0u] = row_rgb[x * 3u + 0u];
            dst[x * 4u + 1u] = row_rgb[x * 3u + 1u];
            dst[x * 4u + 2u] = row_rgb[x * 3u + 2u];
            dst[x * 4u + 3u] = 0xFFu;
        }
    }
    free(row_rgb);
#endif

    jpeg_finish_decompress(&cinfo);

    jpeg_free_image(out_image);
    out_image->width = cinfo.output_width;
    out_image->height = cinfo.output_height;
    out_image->rgba = rgba;

    jpeg_destroy_decompress(&cinfo);
    return 0;
}

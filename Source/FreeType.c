#include "FreeType.h"

#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/ftbitmap.h>

struct ft_font {
    FT_Face face;
    uint32_t pixel_size;
};

static FT_Library g_library = NULL;
static int g_initialized = 0;

int ft_font_init(void)
{
    if (g_initialized) return 0;

    FT_Error err = FT_Init_FreeType(&g_library);
    if (err) return -1;

    g_initialized = 1;
    return 0;
}

void ft_font_shutdown(void)
{
    if (!g_initialized) return;
    FT_Done_FreeType(g_library);
    g_library = NULL;
    g_initialized = 0;
}

ft_font_t *ft_font_load_from_memory(const uint8_t *data, size_t size,
                                     uint32_t pixel_size)
{
    if (!g_initialized || !data || size == 0) return NULL;

    FT_Face face = NULL;
    FT_Error err = FT_New_Memory_Face(g_library,
                                       (const FT_Byte *)data,
                                       (FT_Long)size,
                                       0, &face);
    if (err || !face) return NULL;

    err = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size);
    if (err) {
        FT_Done_Face(face);
        return NULL;
    }

    ft_font_t *font = (ft_font_t *)malloc(sizeof(ft_font_t));
    if (!font) {
        FT_Done_Face(face);
        return NULL;
    }

    font->face = face;
    font->pixel_size = pixel_size;
    return font;
}

void ft_font_close(ft_font_t *font)
{
    if (!font) return;
    if (font->face) FT_Done_Face(font->face);
    free(font);
}

int ft_font_load_glyph(ft_font_t *font, uint32_t codepoint,
                        ft_glyph_bitmap_t *out_bitmap)
{
    if (!font || !font->face || !out_bitmap) return -1;

    FT_UInt glyph_index = FT_Get_Char_Index(font->face, (FT_ULong)codepoint);
    FT_Error err = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT);
    if (err) return -1;

    err = FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL);
    if (err) return -1;

    FT_GlyphSlot slot = font->face->glyph;
    FT_Bitmap *bmp = &slot->bitmap;

    out_bitmap->width = (uint32_t)bmp->width;
    out_bitmap->height = (uint32_t)bmp->rows;
    out_bitmap->bearing_x = (int32_t)slot->bitmap_left;
    out_bitmap->bearing_y = (int32_t)slot->bitmap_top;
    out_bitmap->advance = (int32_t)(slot->advance.x >> 6);
    out_bitmap->buffer = NULL;

    if (bmp->width > 0 && bmp->rows > 0) {
        size_t buf_size = (size_t)bmp->width * (size_t)bmp->rows;
        out_bitmap->buffer = (uint8_t *)malloc(buf_size);
        if (!out_bitmap->buffer) return -1;

        for (uint32_t y = 0; y < (uint32_t)bmp->rows; y++) {
            memcpy(out_bitmap->buffer + y * (size_t)bmp->width,
                   bmp->buffer + (size_t)y * (size_t)bmp->pitch,
                   (size_t)bmp->width);
        }
    }

    return 0;
}

void ft_font_free_bitmap(ft_glyph_bitmap_t *bitmap)
{
    if (!bitmap) return;
    free(bitmap->buffer);
    bitmap->buffer = NULL;
    bitmap->width = 0;
    bitmap->height = 0;
}

ft_vec2_t ft_font_get_kerning(ft_font_t *font, uint32_t left,
                               uint32_t right)
{
    ft_vec2_t zero = { 0, 0 };
    if (!font || !font->face) return zero;

    FT_UInt idx_left = FT_Get_Char_Index(font->face, (FT_ULong)left);
    FT_UInt idx_right = FT_Get_Char_Index(font->face, (FT_ULong)right);

    FT_Vector kern;
    FT_Get_Kerning(font->face, idx_left, idx_right, FT_KERNING_DEFAULT, &kern);

    ft_vec2_t result;
    result.x = (int32_t)(kern.x >> 6);
    result.y = (int32_t)(kern.y >> 6);
    return result;
}

int ft_font_get_line_height(ft_font_t *font)
{
    if (!font || !font->face) return 0;
    return (int)((font->face->size->metrics.height + 63) >> 6);
}

int ft_font_get_ascender(ft_font_t *font)
{
    if (!font || !font->face) return 0;
    return (int)((font->face->size->metrics.ascender + 63) >> 6);
}

int ft_font_get_descender(ft_font_t *font)
{
    if (!font || !font->face) return 0;
    return (int)((font->face->size->metrics.descender + 63) >> 6);
}

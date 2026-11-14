#include "passport_title_font.h"
#include <string.h>

LV_FONT_DECLARE(passport_cjk_16);

extern const uint8_t title_font_start[] asm("_binary_runner_cjk_pfnt_start");
extern const uint8_t title_font_end[] asm("_binary_runner_cjk_pfnt_end");

enum { HEADER_BYTES = 12, WIDTH = 16, HEIGHT = 20, GLYPH_BYTES = WIDTH * HEIGHT / 4 };
static const uint8_t *s_pixels;
static size_t s_count;
static lv_font_t s_fallback;
static lv_font_t s_primary;

static uint16_t glyph_code(size_t index)
{
    const uint8_t *code = title_font_start + HEADER_BYTES + index * sizeof(uint16_t);
    return code[0] | (uint16_t)code[1] << 8;
}

static bool glyph_descriptor(const lv_font_t *font, lv_font_glyph_dsc_t *glyph,
                             uint32_t code, uint32_t next)
{
    (void)font;
    (void)next;
    size_t lo = 0, hi = s_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (glyph_code(mid) < code) lo = mid + 1;
        else hi = mid;
    }
    if (lo == s_count || glyph_code(lo) != code) return false;
    glyph->adv_w = glyph->box_w = WIDTH;
    glyph->box_h = HEIGHT;
    glyph->ofs_y = -5;
    glyph->format = LV_FONT_GLYPH_FORMAT_A2;
    glyph->gid.index = lo;
    return true;
}

static const void *glyph_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *draw_buf)
{
    if (!draw_buf) return NULL;
    const uint8_t *data = s_pixels + glyph->gid.index * GLYPH_BYTES;
    for (size_t y = 0; y < HEIGHT; ++y) {
        uint8_t *row = draw_buf->data + y * draw_buf->header.stride;
        for (size_t x = 0; x < WIDTH; ++x) {
            size_t i = y * WIDTH + x;
            row[x] = ((data[i / 4] >> (6 - (i % 4) * 2)) & 3) * 85;
        }
    }
    return draw_buf;
}

void passport_title_font_init(void)
{
    size_t size = title_font_end - title_font_start;
    if (size < HEADER_BYTES || memcmp(title_font_start, "PCF1", 4)) return;
    uint32_t count;
    memcpy(&count, title_font_start + 4, sizeof(count));
    if (!count || count > (size - HEADER_BYTES) / (sizeof(uint16_t) + GLYPH_BYTES) ||
        size != HEADER_BYTES + count * (sizeof(uint16_t) + GLYPH_BYTES)) return;
    s_count = count;
    s_pixels = title_font_start + HEADER_BYTES + s_count * sizeof(uint16_t);
    s_fallback = (lv_font_t){.get_glyph_dsc = glyph_descriptor, .get_glyph_bitmap = glyph_bitmap,
                             .line_height = passport_cjk_16.line_height, .base_line = passport_cjk_16.base_line};
    s_primary = passport_cjk_16;
    s_primary.fallback = &s_fallback;
}

const lv_font_t *passport_title_font(void)
{
    return s_count ? &s_primary : &passport_cjk_16;
}

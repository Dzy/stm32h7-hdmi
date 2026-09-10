#include "tnc155/upd7220.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GDC_SR_DATA_READY           0x01u
#define GDC_SR_FIFO_FULL            0x02u
#define GDC_SR_FIFO_EMPTY           0x04u
#define GDC_SR_DRAWING_IN_PROGRESS  0x08u
#define GDC_SR_VSYNC_ACTIVE         0x20u
#define GDC_SR_HBLANK_ACTIVE        0x40u

#define GDC_FIFO_CAPACITY           16u
#define GDC_FIFO_TAG_PARAMETER      0u
#define GDC_FIFO_TAG_COMMAND        1u
#define GDC_FIFO_DIRECTION_READ     0u
#define GDC_FIFO_DIRECTION_WRITE    1u

#define L8_OFF_NORMAL               0x00u
#define L8_ON_NORMAL                0xdfu
#define L8_OFF_BRIGHT               0x04u
#define L8_ON_BRIGHT                0x71u

#define ATLAS_BANKS                 6u
#define ATLAS_GLYPHS                64u
#define ATLAS_ROWS                  32u
#define ATLAS_ROW_BYTES             16u

#define DEFAULT_TEXT_HEIGHT         468u
#define DEFAULT_GRAPHICS_HEIGHT     490u

/* The STM32 firmware contains one CLP GDC.  Binding the target surface here
   keeps the public emulator state compact conceptually: the uPD7220 command
   engine owns only FIFO/register semantics, while visible work is performed
   directly on the native L8 display surface. */
static tnc155_upd7220 *s_bound_gdc;
static uint8_t *s_l8;
static const uint8_t *s_atlas;
static unsigned s_width;
static unsigned s_height;
static unsigned s_stride;

typedef struct l8_partition {
    uint32_t start;
    uint16_t length;
    bool graphics;
} l8_partition;

typedef struct l8_location {
    bool valid;
    bool graphics;
    unsigned x_word;
    unsigned y;
    unsigned cell_height;
} l8_location;

static uint8_t decode_display_mode(uint8_t sync_mode)
{
    switch (sync_mode & 0x22u) {
    case 0x00u: return TNC155_GDC_MODE_MIXED;
    case 0x02u: return TNC155_GDC_MODE_GRAPHICS;
    case 0x20u: return TNC155_GDC_MODE_CHARACTER;
    default:    return TNC155_GDC_MODE_CHARACTER;
    }
}

static uint8_t parameter_count(uint8_t command)
{
    switch (command) {
    case 0x00u: return 8u;
    case 0x46u: return 1u;
    case 0x47u: return 1u;
    case 0x49u: return 3u;
    case 0x4au: return 2u;
    case 0x4bu: return 3u;
    case 0x4cu: return 11u;
    default:
        if ((command & 0xfeu) == 0x0eu)
            return 8u;
        if ((command & 0xf0u) == 0x70u)
            return (uint8_t)(16u - (command & 15u));
        return 0u;
    }
}

static unsigned active_height(const tnc155_upd7220 *gdc)
{
    unsigned active = gdc->active_lines;
    if (active == 0u)
        active = gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ?
                 DEFAULT_GRAPHICS_HEIGHT : DEFAULT_TEXT_HEIGHT;
    if (active > s_height)
        active = s_height;
    return active;
}

static l8_partition decode_partition(const tnc155_upd7220 *gdc,
                                     unsigned area)
{
    unsigned base = area * 4u;
    l8_partition part;
    uint8_t b0 = gdc->parameter_ram[base + 0u];
    uint8_t b1 = gdc->parameter_ram[base + 1u];
    uint8_t b2 = gdc->parameter_ram[base + 2u];
    uint8_t b3 = gdc->parameter_ram[base + 3u];

    if (gdc->display_mode == TNC155_GDC_MODE_CHARACTER)
        part.start = (uint32_t)b0 | ((uint32_t)(b1 & 0x1fu) << 8);
    else
        part.start = (uint32_t)b0 | ((uint32_t)b1 << 8) |
                     ((uint32_t)(b2 & 3u) << 16);

    part.length = (uint16_t)((b2 >> 4) | ((uint16_t)(b3 & 0x3fu) << 4));
    part.graphics = gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ||
                    (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
                     (b3 & 0x40u) != 0u);
    return part;
}

static l8_location locate_word(const tnc155_upd7220 *gdc, uint32_t word)
{
    l8_location loc = {0};
    unsigned active;
    unsigned native_y0;
    unsigned line_height;
    unsigned effective_pitch;
    unsigned base_y = 0u;
    unsigned area;

    if (gdc != s_bound_gdc || s_l8 == NULL || s_stride == 0u)
        return loc;

    active = active_height(gdc);
    native_y0 = (s_height - active) / 2u;
    line_height = gdc->lines_per_character != 0u ?
                  gdc->lines_per_character : 1u;
    effective_pitch = gdc->pitch != 0u ? gdc->pitch : 1u;
    word &= 0x7fffu;

    for (area = 0u; area < 4u && base_y < active; ++area) {
        l8_partition part = decode_partition(gdc, area);
        unsigned length = part.length;
        unsigned pitch = effective_pitch;
        uint32_t delta;
        unsigned local;
        unsigned column;

        if (length == 0u && part.graphics)
            length = 0x400u;
        if (length == 0u)
            continue;
        if (length > active - base_y)
            length = active - base_y;

        if (part.graphics && gdc->display_mode == TNC155_GDC_MODE_MIXED)
            pitch >>= 1;
        if (pitch == 0u)
            pitch = 1u;

        delta = (word - (part.start & 0x7fffu)) & 0x7fffu;
        local = (unsigned)(delta / pitch);
        column = (unsigned)(delta % pitch);

        if (part.graphics) {
            if (local < length && column < 32u) {
                loc.valid = true;
                loc.graphics = true;
                loc.x_word = column;
                loc.y = native_y0 + base_y + local;
                loc.cell_height = 1u;
                return loc;
            }
        } else {
            unsigned cell_y = local * line_height;
            if (column < 32u && cell_y < length) {
                unsigned cell_height = line_height;
                if (cell_height > length - cell_y)
                    cell_height = length - cell_y;
                loc.valid = true;
                loc.graphics = false;
                loc.x_word = column;
                loc.y = native_y0 + base_y + cell_y;
                loc.cell_height = cell_height;
                return loc;
            }
        }
        base_y += length;
    }
    return loc;
}

static int p1_small_font_y_offset(unsigned mode, unsigned character)
{
    if (mode == 0u)
        return 0;
    if (mode == 1u)
        return character == 0x3au ? -3 : -6;
    if (mode == 2u)
        return character == 0x1eu ? -4 : -3;
    return 0;
}

static unsigned atlas_bank(unsigned p1_mode)
{
    return p1_mode <= 2u ? 0u : p1_mode - 2u;
}

static void blit_atlas_row(unsigned attr, const uint32_t *src, uint32_t *dst)
{
    unsigned i;

    switch (attr & 3u) {
    case 0u:
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        return;
    case 1u:
        for (i = 0u; i < 4u; ++i)
            dst[i] = src[i] ^ 0xdfdfdfdfu;
        return;
    case 2u:
        for (i = 0u; i < 4u; ++i) {
            uint32_t on = src[i] & 0x01010101u;
            dst[i] = 0x04040404u + on * 0x6du;
        }
        return;
    default:
        for (i = 0u; i < 4u; ++i) {
            uint32_t on = src[i] & 0x01010101u;
            dst[i] = 0x71717171u - on * 0x6du;
        }
        return;
    }
}

static void render_text_cell(uint16_t cell, const l8_location *loc)
{
    static const uint32_t zero_row[4] = {0u, 0u, 0u, 0u};
    uint8_t ascii;
    uint8_t mode_data;
    unsigned p1_mode;
    unsigned character;
    unsigned attr;
    unsigned bank;
    int y_offset;
    unsigned row;

    if (!loc->valid || loc->graphics || s_atlas == NULL)
        return;

    ascii = (uint8_t)cell;
    mode_data = (uint8_t)(cell >> 8);
    p1_mode = (mode_data >> 2) & 7u;
    character = ascii & 0x3fu;
    attr = ((mode_data & 0x02u) != 0u ? 2u : 0u) |
           ((mode_data & 0x01u) != 0u ? 1u : 0u);
    bank = atlas_bank(p1_mode);
    if (bank >= ATLAS_BANKS)
        return;
    y_offset = p1_mode <= 2u ?
               p1_small_font_y_offset(p1_mode, character) : 0;

    for (row = 0u; row < loc->cell_height; ++row) {
        int source_row = (int)row - y_offset;
        const uint32_t *src = zero_row;
        uint32_t *dst;

        if (loc->y + row >= s_height || loc->x_word * 16u + 15u >= s_width)
            continue;
        dst = (uint32_t *)(void *)(s_l8 +
              (size_t)(loc->y + row) * s_stride + loc->x_word * 16u);

        if (source_row >= 0 && source_row < (int)ATLAS_ROWS) {
            src = (const uint32_t *)(const void *)(s_atlas +
                (((size_t)bank * ATLAS_GLYPHS + character) * ATLAS_ROWS +
                 (unsigned)source_row) * ATLAS_ROW_BYTES);
        }
        blit_atlas_row(attr, src, dst);
    }
}

static bool pixel_is_on(uint8_t value)
{
    return value == L8_ON_NORMAL || value == L8_ON_BRIGHT;
}

static void apply_pixel_mod(uint8_t *pixel, uint8_t mod, bool source_on)
{
    switch (mod & 3u) {
    case 0u:
        *pixel = source_on ? L8_ON_NORMAL : L8_OFF_NORMAL;
        break;
    case 1u:
        if (source_on)
            *pixel = pixel_is_on(*pixel) ? L8_OFF_NORMAL : L8_ON_NORMAL;
        break;
    case 2u:
        if (source_on)
            *pixel = L8_OFF_NORMAL;
        break;
    default:
        if (source_on)
            *pixel = L8_ON_NORMAL;
        break;
    }
}

static void render_graphics_word(uint16_t data, uint16_t mask, uint8_t mod,
                                 const l8_location *loc)
{
    unsigned bit;
    uint8_t *dst;

    if (!loc->valid || !loc->graphics || loc->y >= s_height ||
        loc->x_word * 16u + 15u >= s_width)
        return;

    dst = s_l8 + (size_t)loc->y * s_stride + loc->x_word * 16u;
    for (bit = 0u; bit < 16u; ++bit) {
        uint16_t b = (uint16_t)(1u << bit);
        if ((mask & b) != 0u)
            apply_pixel_mod(&dst[bit], mod, (data & b) != 0u);
    }
}

static void direct_write_word(tnc155_upd7220 *gdc, uint8_t type, uint8_t mod,
                              uint16_t data, uint16_t mask)
{
    uint16_t effective_mask = mask;
    uint16_t effective_data = data;
    l8_location loc;

    if (type == 2u) {
        effective_mask &= 0x00ffu;
        effective_data &= 0x00ffu;
    } else if (type == 3u) {
        effective_mask &= 0xff00u;
        effective_data &= 0xff00u;
    } else if (type == 1u) {
        return;
    }

    effective_data &= effective_mask;
    loc = locate_word(gdc, gdc->cursor);
    if (loc.graphics) {
        render_graphics_word(effective_data, effective_mask, mod, &loc);
    } else if (loc.valid && type == 0u && effective_mask == 0xffffu &&
               (mod & 3u) == 0u) {
        /* Character WDAT is visible immediately. There is no private text
           shadow or dirty queue in the STM32 backend. */
        render_text_cell(data, &loc);
    }

    gdc->last_vram_word = gdc->cursor & 0x3ffffu;
    gdc->last_vram_value = data;
    ++gdc->words_written;
}

static uint16_t rotate_left16(uint16_t value)
{
    return (uint16_t)((uint16_t)(value << 1) | (value >> 15));
}

static uint16_t rotate_right16(uint16_t value)
{
    return (uint16_t)((value >> 1) | (uint16_t)(value << 15));
}

static uint16_t drawing_pitch(const tnc155_upd7220 *gdc)
{
    if (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
        gdc->figure_graphics_data)
        return (uint16_t)(gdc->pitch >> 1);
    return gdc->pitch;
}

static uint16_t drawing_pattern(const tnc155_upd7220 *gdc, unsigned cycle)
{
    if (gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ||
        (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
         gdc->figure_graphics_data))
        return ((gdc->pattern >> (cycle & 15u)) & 1u) ? 0xffffu : 0u;
    return gdc->pattern;
}

static void next_pixel(tnc155_upd7220 *gdc, unsigned direction)
{
    uint16_t pitch = drawing_pitch(gdc);
    switch (direction & 7u) {
    case 0u:
        gdc->cursor += pitch;
        break;
    case 1u:
        gdc->cursor += pitch;
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 2u:
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 3u:
        gdc->cursor -= pitch;
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 4u:
        gdc->cursor -= pitch;
        break;
    case 5u:
        gdc->cursor -= pitch;
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    case 6u:
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    default:
        gdc->cursor += pitch;
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    }
    gdc->cursor &= 0x3ffffu;
}

static int sign_extend14(uint16_t value)
{
    value &= 0x3fffu;
    if ((value & 0x2000u) != 0u)
        return (int)value - 0x4000;
    return (int)value;
}

static void reset_figure_parameters(tnc155_upd7220 *gdc)
{
    gdc->figure_count = 0u;
    gdc->figure_d = 8u;
    gdc->figure_d1 = 0xffffu;
    gdc->figure_d2 = 8u;
    gdc->figure_dm = 0xffffu;
    gdc->figure_graphics_data = false;
    gdc->figure_type = 0u;
    gdc->pattern = (uint16_t)(gdc->parameter_ram[8] |
                              ((uint16_t)gdc->parameter_ram[9] << 8));
}

static bool current_pixel_xy(const tnc155_upd7220 *gdc, int *x, int *y)
{
    l8_location loc;
    unsigned bit = 0u;
    uint16_t mask;

    if (gdc->mask == 0u || (gdc->mask & (uint16_t)(gdc->mask - 1u)) != 0u)
        return false;
    loc = locate_word(gdc, gdc->cursor);
    if (!loc.valid || !loc.graphics)
        return false;
    mask = gdc->mask;
    while ((mask & 1u) == 0u) {
        ++bit;
        mask >>= 1;
    }
    *x = (int)(loc.x_word * 16u + bit);
    *y = (int)loc.y;
    return *x >= 0 && *y >= 0 && (unsigned)*x < s_width &&
           (unsigned)*y < s_height;
}

static void native_line(int x0, int y0, int x1, int y1, uint8_t mod,
                        bool source_on)
{
    int dx;
    int sx;
    int dy;
    int sy;
    int err;

    if (y0 == y1 && y0 >= 0 && (unsigned)y0 < s_height) {
        int xa = x0 < x1 ? x0 : x1;
        int xb = x0 < x1 ? x1 : x0;
        if (xa < 0) xa = 0;
        if (xb >= (int)s_width) xb = (int)s_width - 1;
        if (xa <= xb && (mod & 3u) == 0u) {
            memset(s_l8 + (size_t)y0 * s_stride + (unsigned)xa,
                   source_on ? L8_ON_NORMAL : L8_OFF_NORMAL,
                   (size_t)(xb - xa + 1));
            return;
        }
    }

    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    sx = x0 < x1 ? 1 : -1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (unsigned)x0 < s_width &&
            (unsigned)y0 < s_height)
            apply_pixel_mod(s_l8 + (size_t)y0 * s_stride + (unsigned)x0,
                            mod, source_on);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = err << 1;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static void draw_pixel(tnc155_upd7220 *gdc)
{
    unsigned i;
    for (i = 0u; i <= gdc->figure_count; ++i) {
        direct_write_word(gdc, 0u, gdc->bitmap_mod,
                          drawing_pattern(gdc, i), gdc->mask);
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_line_fallback(tnc155_upd7220 *gdc)
{
    int d = sign_extend14(gdc->figure_d);
    int d1 = sign_extend14(gdc->figure_d1);
    int d2 = sign_extend14(gdc->figure_d2);
    uint8_t octant = gdc->figure_direction;
    unsigned i;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        direct_write_word(gdc, 0u, gdc->bitmap_mod,
                          drawing_pattern(gdc, i), gdc->mask);
        if ((octant & 1u) != 0u)
            gdc->figure_direction = (uint8_t)(d < 0 ?
                ((octant + 1u) & 7u) : octant);
        else
            gdc->figure_direction = (uint8_t)(d < 0 ?
                octant : ((octant + 1u) & 7u));
        d += d < 0 ? d1 : d2;
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_line(tnc155_upd7220 *gdc)
{
    uint32_t saved_cursor = gdc->cursor;
    uint16_t saved_mask = gdc->mask;
    uint8_t saved_direction = gdc->figure_direction;
    int x0, y0, x1 = 0, y1 = 0;
    int d;
    int d1;
    int d2;
    uint8_t octant;
    unsigned i;
    bool mapped;

    /* Solid graphics lines are the dominant visualization case.  Simulate
       only the cheap GDC state transitions to obtain the final endpoint,
       then rasterize the visible line once with a tight native L8 Bresenham
       loop.  This removes per-pixel cursor->word mapping and VRAM emulation. */
    if (gdc->pattern != 0xffffu || !current_pixel_xy(gdc, &x0, &y0)) {
        draw_line_fallback(gdc);
        return;
    }

    d = sign_extend14(gdc->figure_d);
    d1 = sign_extend14(gdc->figure_d1);
    d2 = sign_extend14(gdc->figure_d2);
    octant = gdc->figure_direction;
    mapped = false;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        if (i == gdc->figure_count)
            mapped = current_pixel_xy(gdc, &x1, &y1);
        if ((octant & 1u) != 0u)
            gdc->figure_direction = (uint8_t)(d < 0 ?
                ((octant + 1u) & 7u) : octant);
        else
            gdc->figure_direction = (uint8_t)(d < 0 ?
                octant : ((octant + 1u) & 7u));
        d += d < 0 ? d1 : d2;
        next_pixel(gdc, gdc->figure_direction);
    }

    if (!mapped) {
        gdc->cursor = saved_cursor;
        gdc->mask = saved_mask;
        gdc->figure_direction = saved_direction;
        draw_line_fallback(gdc);
        return;
    }

    native_line(x0, y0, x1, y1, gdc->bitmap_mod, true);
    gdc->words_written += (uint32_t)gdc->figure_count + 1u;
    gdc->last_vram_word = saved_cursor & 0x3ffffu;
    gdc->last_vram_value = 0xffffu;
}

static void draw_arc(tnc155_upd7220 *gdc)
{
    int err = -(int)gdc->figure_d;
    int d = (int)gdc->figure_d + 1;
    uint8_t octant = gdc->figure_direction;
    unsigned i;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        if (i >= gdc->figure_dm)
            direct_write_word(gdc, 0u, gdc->bitmap_mod,
                              drawing_pattern(gdc, i & 15u), gdc->mask);
        if (err < 0)
            gdc->figure_direction = (uint8_t)((octant & 1u) != 0u ?
                ((octant + 1u) & 7u) : octant);
        else
            gdc->figure_direction = (uint8_t)((octant & 1u) != 0u ?
                octant : ((octant + 1u) & 7u));
        if (err < 0)
            err += ((int)i + 1) << 1;
        else {
            --d;
            err += ((int)i - d + 1) << 1;
        }
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_rectangle(tnc155_upd7220 *gdc)
{
    unsigned side;
    for (side = 0u; side < 4u; ++side) {
        uint16_t distance = (side & 1u) != 0u ?
                            gdc->figure_d2 : gdc->figure_d;
        uint16_t j;
        for (j = 0u; j < distance; ++j) {
            direct_write_word(gdc, 0u, gdc->bitmap_mod,
                              drawing_pattern(gdc, j & 15u), gdc->mask);
            if (side > 0u && j == 0u)
                gdc->figure_direction =
                    (uint8_t)((gdc->figure_direction + 2u) & 7u);
            next_pixel(gdc, gdc->figure_direction);
        }
    }
}

static void draw_graphics_character(tnc155_upd7220 *gdc)
{
    static const int8_t direction_change[2][4] = {
        { 2, 2, -2, -2 },
        { 1, 3, -3, -1 }
    };
    unsigned type = (gdc->figure_type & 0x10u) >> 4;
    unsigned i;
    unsigned di = 0u;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        unsigned zoom_y;
        uint8_t row = gdc->parameter_ram[15u - (i & 7u)];
        gdc->pattern = (uint16_t)((uint16_t)row << 8 | row);
        for (zoom_y = 0u; zoom_y <= gdc->graphics_character_zoom;
             ++zoom_y, ++di) {
            unsigned j;
            for (j = 0u; j < gdc->figure_d; ++j) {
                unsigned cycle = (di & 1u) != 0u ?
                                 15u - (j & 15u) : (j & 15u);
                unsigned zoom_x;
                uint16_t pattern = drawing_pattern(gdc, cycle);
                for (zoom_x = 0u; zoom_x <= gdc->graphics_character_zoom;
                     ++zoom_x) {
                    direct_write_word(gdc, 0u, gdc->bitmap_mod,
                                      pattern, gdc->mask);
                    if (j + 1u != gdc->figure_d ||
                        zoom_x != gdc->graphics_character_zoom)
                        next_pixel(gdc, gdc->figure_direction);
                }
            }
            gdc->figure_direction = (uint8_t)(
                ((int)gdc->figure_direction +
                 direction_change[type][(di & 1u) << 1]) & 7);
            next_pixel(gdc, gdc->figure_direction);
            gdc->figure_direction = (uint8_t)(
                ((int)gdc->figure_direction +
                 direction_change[type][((di & 1u) << 1) + 1u]) & 7);
        }
    }
}

static void execute_figure_draw(tnc155_upd7220 *gdc)
{
    switch (gdc->figure_type) {
    case 0u: draw_pixel(gdc); break;
    case 1u: draw_line(gdc); break;
    case 4u: draw_arc(gdc); break;
    case 8u: draw_rectangle(gdc); break;
    default: break;
    }
    ++gdc->figure_draws;
    gdc->drawing_in_progress = true;
    reset_figure_parameters(gdc);
}

static void execute_graphics_character_draw(tnc155_upd7220 *gdc)
{
    if ((gdc->figure_type & 0x0fu) == 2u)
        draw_graphics_character(gdc);
    ++gdc->graphics_character_draws;
    gdc->drawing_in_progress = true;
    reset_figure_parameters(gdc);
}

static uint32_t wdat_next_cursor(const tnc155_upd7220 *gdc, uint32_t cursor)
{
    static const int8_t x_dir[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t y_dir[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    unsigned dir = gdc->figure_direction & 7u;
    int32_t next = (int32_t)cursor + x_dir[dir] +
                   y_dir[dir] * (int32_t)drawing_pitch(gdc);
    return (uint32_t)next & 0x3ffffu;
}

static void write_wdat_word(tnc155_upd7220 *gdc, uint16_t supplied)
{
    uint8_t type = (uint8_t)((gdc->command & 0x18u) >> 3);
    uint8_t mod = (uint8_t)(gdc->command & 3u);
    uint32_t repeats = gdc->wdat_first_word ?
                       (uint32_t)gdc->figure_count + 1u : 1u;

    gdc->pattern = supplied;
    while (repeats-- != 0u) {
        direct_write_word(gdc, type, mod, supplied, gdc->mask);
        gdc->cursor = wdat_next_cursor(gdc, gdc->cursor);
    }
    gdc->wdat_first_word = false;
}

static void fifo_clear(tnc155_upd7220 *gdc)
{
    gdc->fifo_head = 0u;
    gdc->fifo_tail = 0u;
    gdc->fifo_count = 0u;
}

static void fifo_set_direction(tnc155_upd7220 *gdc, uint8_t direction)
{
    if (gdc->fifo_direction != direction) {
        fifo_clear(gdc);
        gdc->fifo_direction = direction;
    }
}

static bool fifo_push(tnc155_upd7220 *gdc, uint8_t data, uint8_t tag)
{
    if (gdc->fifo_count >= GDC_FIFO_CAPACITY) {
        ++gdc->fifo_overruns;
        return false;
    }
    gdc->fifo_data[gdc->fifo_tail] = data;
    gdc->fifo_tag[gdc->fifo_tail] = tag;
    gdc->fifo_tail = (uint8_t)((gdc->fifo_tail + 1u) & 15u);
    ++gdc->fifo_count;
    if (gdc->fifo_count > gdc->fifo_high_watermark)
        gdc->fifo_high_watermark = gdc->fifo_count;
    return true;
}

static bool fifo_pop(tnc155_upd7220 *gdc, uint8_t *data, uint8_t *tag)
{
    if (gdc->fifo_count == 0u)
        return false;
    *data = gdc->fifo_data[gdc->fifo_head];
    *tag = gdc->fifo_tag[gdc->fifo_head];
    gdc->fifo_head = (uint8_t)((gdc->fifo_head + 1u) & 15u);
    --gdc->fifo_count;
    return true;
}

void tnc155_upd7220_init(tnc155_upd7220 *gdc)
{
    memset(gdc, 0, sizeof(*gdc));
    gdc->fifo_direction = GDC_FIFO_DIRECTION_WRITE;
    fifo_clear(gdc);
    gdc->status = GDC_SR_FIFO_EMPTY;
    gdc->vsync_active = true;
    gdc->mask = 0xffffu;
    gdc->lines_per_character = 1u;
    reset_figure_parameters(gdc);
}

void tnc155_upd7220_bind_l8(tnc155_upd7220 *gdc,
                            uint8_t *framebuffer,
                            unsigned width,
                            unsigned height,
                            unsigned stride,
                            const uint8_t *font_atlas)
{
    s_bound_gdc = gdc;
    s_l8 = framebuffer;
    s_width = width;
    s_height = height;
    s_stride = stride;
    s_atlas = font_atlas;
    if (s_l8 != NULL && s_stride >= s_width)
        memset(s_l8, L8_OFF_NORMAL, (size_t)s_stride * s_height);
}

uint8_t tnc155_upd7220_status(tnc155_upd7220 *gdc)
{
    uint8_t result = 0u;
    if (gdc->fifo_count == 0u)
        result |= GDC_SR_FIFO_EMPTY;
    if (gdc->fifo_count >= GDC_FIFO_CAPACITY)
        result |= GDC_SR_FIFO_FULL;
    if (gdc->fifo_direction == GDC_FIFO_DIRECTION_READ &&
        gdc->fifo_count != 0u)
        result |= GDC_SR_DATA_READY;
    if (gdc->drawing_in_progress)
        result |= GDC_SR_DRAWING_IN_PROGRESS;
    if (gdc->vsync_active)
        result |= GDC_SR_VSYNC_ACTIVE;
    if (gdc->hblank_active)
        result |= GDC_SR_HBLANK_ACTIVE;
    gdc->status = result;
    gdc->drawing_in_progress = false;
    return result;
}

uint8_t tnc155_upd7220_read_data(tnc155_upd7220 *gdc)
{
    uint8_t data;
    uint8_t tag;
    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_READ);
    if (fifo_pop(gdc, &data, &tag)) {
        (void)tag;
        gdc->read_data = data;
        return data;
    }
    ++gdc->fifo_underflows;
    return gdc->read_data;
}

static void process_command(tnc155_upd7220 *gdc, uint8_t command)
{
    if ((gdc->command & 0xe4u) == 0x20u && gdc->parameter_count != 0u) {
        ++gdc->scanout_commits;
    }
    ++gdc->command_count;
    ++gdc->command_histogram[command];
    gdc->command = command;
    gdc->parameter_count = 0u;
    gdc->parameter_expected = parameter_count(command);

    if (command == 0x00u) {
        ++gdc->reset_count;
        fifo_clear(gdc);
        gdc->display_enabled = false;
        gdc->cursor = 0u;
        gdc->drawing_in_progress = false;
        reset_figure_parameters(gdc);
            if (gdc == s_bound_gdc && s_l8 != NULL)
            memset(s_l8, L8_OFF_NORMAL, (size_t)s_stride * s_height);
    } else if ((command & 0xfeu) == 0x0eu) {
        gdc->display_enabled = (command & 1u) != 0u;
    } else if ((command & 0xfeu) == 0x0cu) {
        gdc->display_enabled = (command & 1u) != 0u;
    } else if (command == 0x6bu) {
        gdc->display_enabled = true;
    } else if (command == 0x6au) {
        gdc->display_enabled = false;
    } else if (command == 0x6cu) {
        execute_figure_draw(gdc);
    } else if (command == 0x68u) {
        execute_graphics_character_draw(gdc);
    }

    if ((command & 0xe4u) == 0x20u) {
        gdc->bitmap_mod = (uint8_t)(command & 3u);
        gdc->wdat_have_low = false;
        gdc->wdat_first_word = true;
    }
}

static void decode_sync_parameter(tnc155_upd7220 *gdc, unsigned index,
                                  uint8_t value)
{
    if (index == 0u) {
        gdc->display_mode = decode_display_mode(value);
    } else if (index == 1u) {
        gdc->active_words = (uint16_t)value + 2u;
        gdc->pitch = gdc->active_words;
    } else if (index == 2u) {
        gdc->hsync_width = (uint8_t)((value & 0x1fu) + 1u);
    } else if (index == 3u) {
        gdc->vsync_width = (uint8_t)(((value & 3u) << 3) |
                                     (gdc->parameters[2] >> 5));
        gdc->horizontal_front_porch = (uint8_t)((value >> 2) + 1u);
    } else if (index == 4u) {
        gdc->horizontal_back_porch = (uint8_t)((value & 0x3fu) + 1u);
    } else if (index == 5u) {
        gdc->vertical_front_porch = (uint8_t)(value & 0x3fu);
    } else if (index == 7u) {
        gdc->active_lines = (uint16_t)(gdc->parameters[6] |
                            ((uint16_t)(value & 3u) << 8));
        gdc->vertical_back_porch = (uint8_t)(value >> 2);
    }
}

static void process_parameter(tnc155_upd7220 *gdc, uint8_t value)
{
    unsigned index = gdc->parameter_count;
    if (index < sizeof(gdc->parameters))
        gdc->parameters[index] = value;
    if (gdc->parameter_count != 0xffu)
        ++gdc->parameter_count;

    if (gdc->command == 0x47u && index == 0u) {
        gdc->pitch = value;
    } else if (gdc->command == 0x46u && index == 0u) {
        gdc->graphics_character_zoom = (uint8_t)(value & 0x0fu);
        gdc->display_zoom = (uint8_t)(value >> 4);
    } else if (gdc->command == 0x00u ||
               (gdc->command & 0xfeu) == 0x0eu) {
        decode_sync_parameter(gdc, index, value);
    } else if (gdc->command == 0x4bu && index == 0u) {
        gdc->lines_per_character = (uint8_t)((value & 0x1fu) + 1u);
    } else if (gdc->command == 0x49u && index == 2u) {
        gdc->cursor = (uint32_t)gdc->parameters[0] |
                      ((uint32_t)gdc->parameters[1] << 8) |
                      ((uint32_t)(value & 3u) << 16);
        gdc->mask = (uint16_t)(1u << ((value >> 4) & 15u));
    } else if (gdc->command == 0x4au && index == 1u) {
        gdc->mask = (uint16_t)(gdc->parameters[0] |
                              ((uint16_t)gdc->parameters[1] << 8));
    } else if (gdc->command == 0x4cu) {
        if (index == 0u) {
            gdc->figure_type = (uint8_t)((value & 0xf8u) >> 3);
            gdc->figure_direction = (uint8_t)(value & 7u);
        } else if (index == 1u) {
            gdc->figure_count = (uint16_t)((gdc->figure_count & 0x3f00u) |
                                           value);
        } else if (index == 2u) {
            gdc->figure_count = (uint16_t)(gdc->parameters[1] |
                ((uint16_t)(value & 0x3fu) << 8));
            gdc->figure_graphics_data =
                (value & 0x40u) != 0u &&
                gdc->display_mode == TNC155_GDC_MODE_MIXED;
        } else if (index == 4u) {
            gdc->figure_d = (uint16_t)(gdc->parameters[3] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 6u) {
            gdc->figure_d2 = (uint16_t)(gdc->parameters[5] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 8u) {
            gdc->figure_d1 = (uint16_t)(gdc->parameters[7] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 10u) {
            gdc->figure_dm = (uint16_t)(gdc->parameters[9] |
                ((uint16_t)(value & 0x3fu) << 8));
        }
    } else if ((gdc->command & 0xf0u) == 0x70u) {
        unsigned ram_address = (gdc->command & 15u) + index;
        if (ram_address < sizeof(gdc->parameter_ram)) {
            gdc->parameter_ram[ram_address] = value;
            if (ram_address == 8u)
                gdc->pattern = (uint16_t)((gdc->pattern & 0xff00u) | value);
            else if (ram_address == 9u)
                gdc->pattern = (uint16_t)((gdc->pattern & 0x00ffu) |
                                          ((uint16_t)value << 8));
        }
    } else if ((gdc->command & 0xe4u) == 0x20u) {
        uint8_t type = (uint8_t)((gdc->command & 0x18u) >> 3);
        bool byte_transfer = type == 2u || type == 3u;
        if (byte_transfer) {
            uint16_t supplied = type == 3u ? (uint16_t)value << 8 : value;
            write_wdat_word(gdc, supplied);
        } else if (!gdc->wdat_have_low) {
            gdc->wdat_low = value;
            gdc->wdat_have_low = true;
        } else {
            uint16_t supplied = (uint16_t)(gdc->wdat_low |
                                           ((uint16_t)value << 8));
            write_wdat_word(gdc, supplied);
            gdc->wdat_have_low = false;
        }
    }
}

void tnc155_upd7220_write_command(tnc155_upd7220 *gdc, uint8_t command)
{
    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_WRITE);
    (void)fifo_push(gdc, command, GDC_FIFO_TAG_COMMAND);
}

void tnc155_upd7220_write_parameter(tnc155_upd7220 *gdc, uint8_t value)
{
    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_WRITE);
    (void)fifo_push(gdc, value, GDC_FIFO_TAG_PARAMETER);
}

void tnc155_upd7220_service(tnc155_upd7220 *gdc, unsigned max_entries)
{
    uint8_t data;
    uint8_t tag;

    if (gdc == NULL || max_entries == 0u ||
        gdc->fifo_direction != GDC_FIFO_DIRECTION_WRITE)
        return;

    while (max_entries-- != 0u && fifo_pop(gdc, &data, &tag)) {
        gdc->last_fifo_data = data;
        gdc->last_fifo_tag = tag;
        if (tag == GDC_FIFO_TAG_COMMAND)
            process_command(gdc, data);
        else
            process_parameter(gdc, data);
        ++gdc->fifo_entries_processed;
    }
}

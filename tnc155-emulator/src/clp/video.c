#include "tnc155/video.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct debug_glyph {
    char character;
    uint8_t rows[7];
} debug_glyph;

static const debug_glyph debug_font[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}},     {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},     {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'I',{14,4,4,4,4,4,14}},
    {'H',{17,17,17,31,17,17,17}}, {'S',{15,16,16,14,1,1,30}},
    {'V',{17,17,17,17,17,10,4}},  {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},
    {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,25,21,19,19,17}}, {'P',{30,17,17,30,16,16,16}},
    {'R',{30,17,17,30,20,18,17}}, {'U',{17,17,17,17,17,17,14}},
    {'T',{31,4,4,4,4,4,4}},       {'Z',{31,1,2,4,8,16,31}},
    {'+',{0,4,4,31,4,4,0}},        {'-',{0,0,0,31,0,0,0}},
    {'.',{0,0,0,0,0,12,12}},       {'/',{1,2,2,4,8,8,16}},
    {'?',{14,17,1,2,4,0,4}}
};

static const uint8_t *find_debug_glyph(char character)
{
    size_t i;
    for (i = 0; i < sizeof(debug_font) / sizeof(debug_font[0]); ++i) {
        if (debug_font[i].character == character)
            return debug_font[i].rows;
    }
    return debug_font[sizeof(debug_font) / sizeof(debug_font[0]) - 1u].rows;
}

static void draw_character(uint32_t *pixels, unsigned pitch, unsigned x,
                           unsigned y, char character, uint32_t color)
{
    const uint8_t *rows;
    unsigned row;
    unsigned column;
    unsigned sx;
    unsigned sy;
    if (character == ' ')
        return;
    rows = find_debug_glyph(character);
    for (row = 0; row < 7u; ++row) {
        for (column = 0; column < 5u; ++column) {
            if ((rows[row] & (uint8_t)(0x10u >> column)) == 0u)
                continue;
            for (sy = 0; sy < 2u; ++sy) {
                for (sx = 0; sx < 2u; ++sx) {
                    unsigned px = x + column * 2u + sx;
                    unsigned py = y + row * 2u + sy;
                    if (px < TNC155_VIDEO_WIDTH && py < TNC155_VIDEO_HEIGHT)
                        pixels[py * pitch + px] = color;
                }
            }
        }
    }
}

static void draw_text(uint32_t *pixels, unsigned pitch, unsigned x, unsigned y,
                      const char *text, uint32_t color)
{
    while (*text != '\0') {
        draw_character(pixels, pitch, x, y, *text, color);
        x += 12u;
        ++text;
    }
}

static void hex4(char *out, uint16_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = digits[(value >> 12) & 15u];
    out[1] = digits[(value >> 8) & 15u];
    out[2] = digits[(value >> 4) & 15u];
    out[3] = digits[value & 15u];
    out[4] = '\0';
}

static void signed_dac(char *out, int value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned magnitude;
    out[0] = value < 0 ? '-' : '+';
    magnitude = (unsigned)(value < 0 ? -value : value) & 0x0fffu;
    out[1] = digits[(magnitude >> 8) & 15u];
    out[2] = digits[(magnitude >> 4) & 15u];
    out[3] = digits[magnitude & 15u];
    out[4] = '\0';
}

typedef struct video_partition {
    uint32_t start;
    uint16_t length;
    bool graphics;
    bool wide;
} video_partition;

static void draw_debug_panel(const tnc155_machine *machine, uint32_t *argb,
                             unsigned pitch_pixels)
{
    char value[5];
    char dac_value[5];
    draw_text(argb, pitch_pixels, 16, 16, "TNC 155 MAIN + CLP", 0xffd8e8c8u);
    draw_text(argb, pitch_pixels, 16, 56, "MAIN PC", 0xff9fcf8fu);
    hex4(value, machine->main.cpu.pc);
    draw_text(argb, pitch_pixels, 112, 56, value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 16, 96, "CLP  PC", 0xff9fcf8fu);
    hex4(value, machine->clp.cpu.pc);
    draw_text(argb, pitch_pixels, 112, 96, value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 16, 136, "PLC  PC", 0xff9fcf8fu);
    hex4(value, machine->main.plc.pc);
    draw_text(argb, pitch_pixels, 112, 136, value, 0xffffffffu);
    signed_dac(dac_value, machine->clp.analog.dac_negative ?
               -(int)machine->clp.analog.dac_magnitude :
               (int)machine->clp.analog.dac_magnitude);
    draw_text(argb, pitch_pixels, 16, 176, "DAC", 0xff9fcf8fu);
    draw_text(argb, pitch_pixels, 64, 176, dac_value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 16, 216, "SH X", 0xff9fcf8fu);
    signed_dac(dac_value, machine->clp.analog.held_dac[0]);
    draw_text(argb, pitch_pixels, 76, 216, dac_value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 148, 216, "Y", 0xff9fcf8fu);
    signed_dac(dac_value, machine->clp.analog.held_dac[1]);
    draw_text(argb, pitch_pixels, 172, 216, dac_value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 16, 256, "SH Z", 0xff9fcf8fu);
    signed_dac(dac_value, machine->clp.analog.held_dac[2]);
    draw_text(argb, pitch_pixels, 76, 256, dac_value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 148, 256, "IV", 0xff9fcf8fu);
    signed_dac(dac_value, machine->clp.analog.held_dac[3]);
    draw_text(argb, pitch_pixels, 184, 256, dac_value, 0xffffffffu);
    draw_text(argb, pitch_pixels, 16, 296, "SH S", 0xff9fcf8fu);
    signed_dac(dac_value, machine->clp.analog.held_dac[4]);
    draw_text(argb, pitch_pixels, 76, 296, dac_value, 0xffffffffu);
}

static video_partition decode_partition(const tnc155_upd7220 *gdc,
                                        unsigned area)
{
    unsigned base = area * 4u;
    video_partition part;
    uint8_t b0 = gdc->parameter_ram[base + 0u];
    uint8_t b1 = gdc->parameter_ram[base + 1u];
    uint8_t b2 = gdc->parameter_ram[base + 2u];
    uint8_t b3 = gdc->parameter_ram[base + 3u];

    if (gdc->display_mode == TNC155_GDC_MODE_CHARACTER) {
        part.start = (uint32_t)b0 | ((uint32_t)(b1 & 0x1fu) << 8);
    } else {
        part.start = (uint32_t)b0 | ((uint32_t)b1 << 8) |
                     ((uint32_t)(b2 & 3u) << 16);
    }
    part.length = (uint16_t)((b2 >> 4) | ((uint16_t)(b3 & 0x3fu) << 4));
    part.graphics = gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ||
                    (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
                     (b3 & 0x40u) != 0u);
    part.wide = (b3 & 0x80u) != 0u;
    return part;
}

unsigned tnc155_video_active_height(const tnc155_machine *machine)
{
    unsigned active;
    if (machine == NULL)
        return TNC155_VIDEO_TEXT_HEIGHT;
    active = machine->clp.gdc.active_lines;
    if (active == 0u)
        active = machine->clp.gdc.display_mode == TNC155_GDC_MODE_GRAPHICS ?
                 TNC155_VIDEO_GRAPHICS_HEIGHT : TNC155_VIDEO_TEXT_HEIGHT;
    if (active > TNC155_VIDEO_HEIGHT)
        active = TNC155_VIDEO_HEIGHT;
    return active;
}

static bool locate_scanline(const tnc155_upd7220 *gdc, unsigned y,
                            video_partition *part, unsigned *local_y)
{
    unsigned area;
    unsigned base_y = 0u;
    unsigned active = gdc->active_lines != 0u ? gdc->active_lines :
        (gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ?
         TNC155_VIDEO_GRAPHICS_HEIGHT : TNC155_VIDEO_TEXT_HEIGHT);

    for (area = 0u; area < 4u && base_y < active; ++area) {
        video_partition candidate = decode_partition(gdc, area);
        unsigned length = candidate.length;
        if (length == 0u && candidate.graphics)
            length = 0x400u;
        if (length == 0u)
            continue;
        if (length > active - base_y)
            length = active - base_y;
        if (y >= base_y && y < base_y + length) {
            *part = candidate;
            part->length = (uint16_t)length;
            *local_y = y - base_y;
            return true;
        }
        base_y += length;
    }
    return false;
}

/* The uPD7220 framebuffer is word-oriented: one 16-bit word represents
   sixteen graphics pixels (and one character cell in character mode).
   Keep scanout word-oriented as well.  This is particularly important on
   Cortex-M7, where re-reading the same external/large SRAM word 16 times is
   much more expensive than expanding it once into the ARGB framebuffer. */
static uint16_t load_be16(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] << 8 | src[1]);
}

static void fill_scanline(uint32_t *dst, uint32_t color)
{
    unsigned x;
    for (x = 0u; x < TNC155_VIDEO_WIDTH; ++x)
        dst[x] = color;
}

/* bits bit 0 is the leftmost output pixel.  This matches the existing
   graphics scanout convention. */
static void render_word(uint32_t *dst, uint16_t bits,
                        uint32_t off_color, uint32_t on_color)
{
    unsigned i;
    for (i = 0u; i < 16u; ++i) {
        dst[i] = (bits & 1u) != 0u ? on_color : off_color;
        bits >>= 1;
    }
}

/* Character generator pixels are MSB first and each source pixel is doubled
   horizontally to fill the 16-pixel uPD7220 character cell.  Convert that to
   the bit-0-left convention used by render_word(). */
static uint16_t expand_glyph_2x(uint8_t glyph)
{
    uint16_t bits = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) {
        if ((glyph & (uint8_t)(0x80u >> i)) != 0u)
            bits |= (uint16_t)(3u << (i * 2u));
    }
    return bits;
}

void tnc155_video_render(const tnc155_machine *machine, uint32_t *argb,
                         unsigned pitch_pixels, bool debug_overlay)
{
    /* Three visible luminance states: black, normal amber and brighter
       amber.  D0 still inverts VIDEO.  D1 selects the brighter ON level;
       both OFF combinations remain black. */
    static const uint32_t phosphor[4] = {
        0xff000000u, /* D1=0, VIDEO=0: black */
        0xffd88900u, /* D1=0, VIDEO=1: normal amber */
        0xff000000u, /* D1=1, VIDEO=0: black */
        0xffffc52au  /* D1=1, VIDEO=1: bright amber */
    };
    const tnc155_upd7220 *gdc = &machine->clp.gdc;
    const uint8_t *vram = gdc->scanout_character_video;
    unsigned y;
    unsigned active_height = tnc155_video_active_height(machine);
    unsigned line_height = gdc->lines_per_character != 0u ?
                           gdc->lines_per_character : 1u;
    unsigned base_pitch = gdc->pitch != 0u ? gdc->pitch : 1u;

    if (!gdc->display_enabled) {
        for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y)
            fill_scanline(argb + y * pitch_pixels, phosphor[0]);
    } else {
        for (y = 0u; y < active_height; ++y) {
            video_partition part;
            unsigned local_y;
            uint32_t *dst = argb + y * pitch_pixels;
            unsigned cell_x;

            if (!locate_scanline(gdc, y, &part, &local_y)) {
                fill_scanline(dst, phosphor[0]);
                continue;
            }

            if (!part.graphics) {
                uint32_t row_word = (part.start +
                    (local_y / line_height) * base_pitch) & 0x7fffu;
                unsigned glyph_row = local_y % line_height;

                for (cell_x = 0u; cell_x < TNC155_VIDEO_WIDTH / 16u;
                     ++cell_x) {
                    uint32_t word = (row_word + cell_x) & 0x7fffu;
                    uint16_t cell = load_be16(vram + word * 2u);
                    uint8_t ascii = (uint8_t)cell;
                    uint8_t mode_data = (uint8_t)(cell >> 8);
                    unsigned p1_mode = (mode_data >> 2) & 0x07u;
                    unsigned char6 = ascii & 0x3fu;
                    uint8_t glyph = tnc155_clp_font_row(
                        p1_mode, char6, glyph_row);
                    uint16_t bits = expand_glyph_2x(glyph);
                    bool bright = (mode_data & 0x02u) != 0u;
                    unsigned color_base = bright ? 2u : 0u;

                    if ((mode_data & 0x01u) != 0u)
                        bits ^= 0xffffu;
                    render_word(dst + cell_x * 16u, bits,
                                phosphor[color_base],
                                phosphor[color_base | 1u]);
                }
            } else {
                unsigned pitch = base_pitch;
                uint32_t row_word;

                if (gdc->display_mode == TNC155_GDC_MODE_MIXED)
                    pitch >>= 1;
                if (pitch == 0u)
                    pitch = 1u;
                row_word = (part.start + local_y * pitch) & 0x7fffu;

                for (cell_x = 0u; cell_x < TNC155_VIDEO_WIDTH / 16u;
                     ++cell_x) {
                    uint32_t word = (row_word + cell_x) & 0x7fffu;
                    uint16_t bits = load_be16(vram + word * 2u);
                    render_word(dst + cell_x * 16u, bits,
                                phosphor[0], phosphor[1]);
                }
            }
        }

        /* Active scanlines are now written exactly once.  Only the inactive
           tail needs clearing, avoiding the old full-frame clear pass. */
        for (y = active_height; y < TNC155_VIDEO_HEIGHT; ++y)
            fill_scanline(argb + y * pitch_pixels, phosphor[0]);
    }

    if (!debug_overlay)
        return;
    draw_debug_panel(machine, argb, pitch_pixels);
}

void tnc155_video_render_debug(const tnc155_machine *machine, uint32_t *argb,
                               unsigned pitch_pixels)
{
    unsigned x;
    unsigned y;
    for (y = 0; y < TNC155_DEBUG_HEIGHT; ++y)
        for (x = 0; x < TNC155_DEBUG_WIDTH; ++x)
            argb[y * pitch_pixels + x] = 0xff000000u;
    draw_debug_panel(machine, argb, pitch_pixels);
}

#include "tnc155/upd7220.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 512u
#define H 490u
#define ACTIVE_LINES 468u
#define ACTIVE_Y0 ((H - ACTIVE_LINES) / 2u)
#define TEXT_CELL_LINES 26u
#define ATLAS_BANKS 6u
#define ATLAS_GLYPHS 64u
#define ATLAS_ROWS 32u
#define ATLAS_ROW_BYTES 16u
#define ATLAS_BYTES (ATLAS_BANKS * ATLAS_GLYPHS * ATLAS_ROWS * ATLAS_ROW_BYTES)

static uint8_t fb[W * H];
static uint8_t atlas[ATLAS_BYTES];

static void service_cmd(tnc155_upd7220 *g, uint8_t v)
{
    tnc155_upd7220_write_command(g, v);
    tnc155_upd7220_service(g, 1u);
}

static void service_param(tnc155_upd7220 *g, uint8_t v)
{
    tnc155_upd7220_write_parameter(g, v);
    tnc155_upd7220_service(g, 1u);
}

static void sync_mode(tnc155_upd7220 *g, uint8_t mode, unsigned lines)
{
    uint8_t p[8] = {0};
    p[0] = mode;
    p[1] = 30u; /* 32 words => 512 native L8 pixels */
    p[6] = (uint8_t)lines;
    p[7] = (uint8_t)((lines >> 8) & 3u);
    service_cmd(g, 0x0fu);
    for (unsigned i = 0; i < 8; ++i) service_param(g, p[i]);
    service_cmd(g, 0x47u);
    service_param(g, 32u);
}

static void set_pram_partition0(tnc155_upd7220 *g, unsigned lines,
                                uint16_t pattern)
{
    uint8_t p[16] = {0};
    p[2] = (uint8_t)((lines & 15u) << 4);
    p[3] = (uint8_t)((lines >> 4) & 0x3fu);
    p[8] = (uint8_t)pattern;
    p[9] = (uint8_t)(pattern >> 8);
    service_cmd(g, 0x70u);
    for (unsigned i = 0; i < 16; ++i) service_param(g, p[i]);
}

static void set_pattern(tnc155_upd7220 *g, uint16_t pattern)
{
    service_cmd(g, 0x78u);
    service_param(g, (uint8_t)pattern);
    service_param(g, (uint8_t)(pattern >> 8));
}

static void set_mod(tnc155_upd7220 *g, unsigned mod)
{
    service_cmd(g, (uint8_t)(0x20u | (mod & 3u)));
}

static void set_cursor_xy(tnc155_upd7220 *g, unsigned x, unsigned y)
{
    uint32_t word = (uint32_t)y * 32u + x / 16u;
    unsigned bit = x & 15u;
    service_cmd(g, 0x49u);
    service_param(g, (uint8_t)word);
    service_param(g, (uint8_t)(word >> 8));
    service_param(g, (uint8_t)(((word >> 16) & 3u) | (bit << 4)));
}

static void set_cursor_cell(tnc155_upd7220 *g, unsigned col, unsigned row)
{
    uint32_t word = (uint32_t)row * 32u + col;
    service_cmd(g, 0x49u);
    service_param(g, (uint8_t)word);
    service_param(g, (uint8_t)(word >> 8));
    service_param(g, (uint8_t)((word >> 16) & 3u));
    service_cmd(g, 0x4au);
    service_param(g, 0xffu);
    service_param(g, 0xffu);
}

static void figs(tnc155_upd7220 *g, unsigned type, unsigned dir,
                 unsigned count, uint16_t d, uint16_t d2,
                 uint16_t d1, uint16_t dm)
{
    uint8_t p[11];
    memset(p, 0, sizeof(p));
    p[0] = (uint8_t)((type << 3) | (dir & 7u));
    p[1] = (uint8_t)count;
    p[2] = (uint8_t)((count >> 8) & 0x3fu);
    p[3] = (uint8_t)d;
    p[4] = (uint8_t)((d >> 8) & 0x3fu);
    p[5] = (uint8_t)d2;
    p[6] = (uint8_t)((d2 >> 8) & 0x3fu);
    p[7] = (uint8_t)d1;
    p[8] = (uint8_t)((d1 >> 8) & 0x3fu);
    p[9] = (uint8_t)dm;
    p[10] = (uint8_t)((dm >> 8) & 0x3fu);
    service_cmd(g, 0x4cu);
    for (unsigned i = 0; i < 11; ++i) service_param(g, p[i]);
}

static void figd_line(tnc155_upd7220 *g, unsigned x, unsigned y,
                      unsigned dir, unsigned count)
{
    set_cursor_xy(g, x, y);
    if (dir & 1u)
        figs(g, 1u, dir, count, 0u, 0u, 0u, 0xffffu);
    else
        figs(g, 1u, dir, count, 0x3fffu, 0u, 0u, 0xffffu);
    service_cmd(g, 0x6cu);
}

/* The hardware rectangle parameters are not raw pixel counts: DC=3,
   D=first-side pixels-1, D2=second-side pixels-1, D1=-1 and DM=D.
   The STM32 engine intentionally consumes those real uPD7220 semantics. */
static void figd_rect(tnc155_upd7220 *g, unsigned x, unsigned y,
                      unsigned dir, unsigned first_pixels,
                      unsigned second_pixels)
{
    uint16_t d;
    uint16_t d2;
    if (first_pixels == 0u || second_pixels == 0u)
        return;
    d = (uint16_t)(first_pixels - 1u);
    d2 = (uint16_t)(second_pixels - 1u);
    set_cursor_xy(g, x, y);
    figs(g, 8u, dir, 3u, d, d2, 0xffffu, d);
    service_cmd(g, 0x6cu);
}

static void wdat_graphics_word(tnc155_upd7220 *g, unsigned xword,
                               unsigned y, uint16_t bits, unsigned mod)
{
    uint32_t word = (uint32_t)y * 32u + xword;
    service_cmd(g, 0x49u);
    service_param(g, (uint8_t)word);
    service_param(g, (uint8_t)(word >> 8));
    service_param(g, (uint8_t)((word >> 16) & 3u));
    service_cmd(g, 0x4au);
    service_param(g, 0xffu);
    service_param(g, 0xffu);
    service_cmd(g, (uint8_t)(0x20u | (mod & 3u)));
    service_param(g, (uint8_t)bits);
    service_param(g, (uint8_t)(bits >> 8));
}

static void wdat_text_cell(tnc155_upd7220 *g, unsigned col, unsigned row,
                           uint8_t ascii, uint8_t mode_data)
{
    set_cursor_cell(g, col, row);
    service_cmd(g, 0x20u);
    service_param(g, ascii);
    service_param(g, mode_data);
}

static void text(tnc155_upd7220 *g, unsigned col, unsigned row,
                 const char *s, uint8_t mode_data)
{
    while (*s && col < 32u) {
        wdat_text_cell(g, col++, row, (uint8_t)*s++, mode_data);
    }
}

static int build_atlas(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *rom;
    long n;
    if (!f) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || n < 0x4000) {
        fclose(f);
        return 0;
    }
    rom = malloc((size_t)n);
    if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n) {
        free(rom); fclose(f); return 0;
    }
    fclose(f);

    for (unsigned bank = 0; bank < ATLAS_BANKS; ++bank) {
        unsigned p1_mode = bank == 0u ? 0u : bank + 2u;
        for (unsigned glyph = 0; glyph < ATLAS_GLYPHS; ++glyph) {
            for (unsigned row = 0; row < ATLAS_ROWS; ++row) {
                uint8_t bits = rom[((size_t)p1_mode * 64u + glyph) * 32u + row];
                uint8_t *dst = atlas + (((size_t)bank * ATLAS_GLYPHS + glyph) *
                               ATLAS_ROWS + row) * ATLAS_ROW_BYTES;
                for (unsigned bit = 0; bit < 8; ++bit) {
                    uint8_t px = (bits & (0x80u >> bit)) ? 0xdfu : 0x00u;
                    dst[bit * 2u] = px;
                    dst[bit * 2u + 1u] = px;
                }
            }
        }
    }
    free(rom);
    return 1;
}

static void rgb332(uint8_t i, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned r3 = (i >> 5) & 7u;
    unsigned g3 = (i >> 2) & 7u;
    unsigned b2 = i & 3u;
    *r = (uint8_t)((r3 * 255u + 3u) / 7u);
    *g = (uint8_t)((g3 * 255u + 3u) / 7u);
    *b = (uint8_t)((b2 * 255u + 1u) / 3u);
}

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (size_t i = 0; i < sizeof(fb); ++i) {
        uint8_t rgb[3];
        rgb332(fb[i], &rgb[0], &rgb[1], &rgb[2]);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 1;
}

static int pixel_on(unsigned x, unsigned y)
{
    return x < W && y < H && fb[(size_t)y * W + x] != 0u;
}

static int validate_rect(unsigned x0, unsigned y0,
                         unsigned x1, unsigned y1)
{
    y0 += ACTIVE_Y0;
    y1 += ACTIVE_Y0;
    return pixel_on(x0, y0) && pixel_on(x1, y0) &&
           pixel_on(x0, y1) && pixel_on(x1, y1);
}

static int validate_big_a(void)
{
    /* The four P1 fragment cells occupy columns 5..6 and text rows 5..6.
       With the real P2 CCHAR value (26 scanlines/cell), row 6 begins exactly
       26 raster lines after row 5.  Reject the old 32-line test setup that
       visibly split the large character across its horizontal centre. */
    const unsigned x0 = 5u * 16u;
    const unsigned x1 = 7u * 16u;
    const unsigned seam = ACTIVE_Y0 + 6u * TEXT_CELL_LINES;
    unsigned near_above = 0u;
    unsigned near_below = 0u;
    for (unsigned y = seam - 3u; y < seam; ++y)
        for (unsigned x = x0; x < x1; ++x)
            near_above += pixel_on(x, y) ? 1u : 0u;
    for (unsigned y = seam; y < seam + 3u; ++y)
        for (unsigned x = x0; x < x1; ++x)
            near_below += pixel_on(x, y) ? 1u : 0u;
    return near_above != 0u && near_below != 0u;
}

static int render_graphics(void)
{
    tnc155_upd7220 g;
    tnc155_upd7220_init(&g);
    tnc155_upd7220_bind_l8(&g, fb, W, H, W, atlas);
    sync_mode(&g, 0x02u, ACTIVE_LINES);
    set_pram_partition0(&g, ACTIVE_LINES, 0xffffu);
    set_mod(&g, 0u);

    /* Border: optimized solid horizontal/vertical FIGD lines. */
    figd_line(&g, 24, 24, 2, 463);
    figd_line(&g, 24, 443, 2, 463);
    figd_line(&g, 24, 24, 0, 419);
    figd_line(&g, 487, 24, 0, 419);

    /* Native Bresenham starburst through all eight GDC directions. */
    figd_line(&g, 256, 234, 0, 175);
    figd_line(&g, 256, 234, 4, 175);
    figd_line(&g, 256, 234, 2, 205);
    figd_line(&g, 256, 234, 6, 205);
    figd_line(&g, 256, 234, 1, 145);
    figd_line(&g, 256, 234, 3, 145);
    figd_line(&g, 256, 234, 5, 145);
    figd_line(&g, 256, 234, 7, 145);

    /* DIR=2 starts along +X and then turns counter-clockwise, therefore the
       supplied cursor is the lower-left corner for these axis-aligned boxes. */
    figd_rect(&g, 70, 160, 2, 151, 91);   /* x=70..220, y=70..160 */
    figd_rect(&g, 292, 410, 2, 146, 81);  /* x=292..437, y=330..410 */

    /* Patterned FIGD line: intentionally exercises the direct-L8 fallback. */
    set_pattern(&g, 0xaaaau);
    figd_line(&g, 70, 379, 2, 360);
    set_pattern(&g, 0xffffu);

    /* WDAT graphics words and MOD semantics. */
    for (unsigned i = 0; i < 12; ++i)
        wdat_graphics_word(&g, 6u + i, 289u, (i & 1u) ? 0xaaaau : 0x5555u, 0u);
    wdat_graphics_word(&g, 15u, 234u, 0xffffu, 1u); /* XOR through centre */

    if (!validate_rect(70u, 70u, 220u, 160u) ||
        !validate_rect(292u, 330u, 437u, 410u)) {
        fprintf(stderr, "rectangle regression failed\n");
        return 0;
    }
    return 1;
}

static int render_text(void)
{
    tnc155_upd7220 g;
    tnc155_upd7220_init(&g);
    tnc155_upd7220_bind_l8(&g, fb, W, H, W, atlas);

    /* P2 programs AL=468 and CCHAR=0x19, i.e. 26 raster lines per text row.
       468/26 = 18 rows.  The previous host test incorrectly used 32 lines,
       which inserted a visible horizontal gap through 2x2 large glyphs. */
    sync_mode(&g, 0x20u, ACTIVE_LINES);
    service_cmd(&g, 0x4bu);
    service_param(&g, TEXT_CELL_LINES - 1u);
    service_param(&g, 0u);
    service_param(&g, 0u);
    set_pram_partition0(&g, ACTIVE_LINES, 0xffffu);

    text(&g, 2, 0, "TNC 155  STM32  UPD7220", 0x00u);
    text(&g, 2, 1, "DIRECT L8  MODE 0", 0x00u);
    text(&g, 2, 2, "MODE 1 OFFSET", 0x04u);
    text(&g, 17, 2, "MODE 2", 0x08u);
    text(&g, 2, 3, "INVERSE", 0x01u);
    text(&g, 13, 3, "BRIGHT", 0x02u);

    /* One real P1 large glyph assembled exactly as P2 lays it out: four
       adjacent cells, modes 4/5 on the top row and 6/7 on the bottom row. */
    wdat_text_cell(&g, 5, 5, 'A', 0x10u);
    wdat_text_cell(&g, 6, 5, 'A', 0x14u);
    wdat_text_cell(&g, 5, 6, 'A', 0x18u);
    wdat_text_cell(&g, 6, 6, 'A', 0x1cu);
    text(&g, 9, 5, "BIG A = MODES 4+5", 0x00u);
    text(&g, 9, 6, "        MODES 6+7", 0x00u);

    if (!validate_big_a()) {
        fprintf(stderr, "large-glyph seam regression failed\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!build_atlas("roms/2340201A_nr1.rom")) {
        fprintf(stderr, "failed to load/build P1 atlas\n");
        return 1;
    }
    if (!render_graphics())
        return 2;
    if (!write_ppm("stm32_upd7220_graphics.ppm"))
        return 3;
    if (!render_text())
        return 4;
    if (!write_ppm("stm32_upd7220_text.ppm"))
        return 5;
    return 0;
}

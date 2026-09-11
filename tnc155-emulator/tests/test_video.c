#include "tnc155/video.h"
#include "tnc155/machine.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLACK_ARGB        0xff000000u
#define NORMAL_AMBER_ARGB 0xffd88900u
#define BRIGHT_AMBER_ARGB 0xffffc52au

int main(void)
{
    tnc155_machine normal;
    tnc155_machine inverse;
    tnc155_machine bright;
    uint32_t *a;
    uint32_t *b;
    size_t pixels = (size_t)TNC155_VIDEO_WIDTH * TNC155_VIDEO_HEIGHT;
    unsigned x, y;

    assert(tnc155_machine_init(&normal));
    inverse = normal;
    bright = normal;

    /* One character cell at GDC word 0.  >08, >09 and >0A select the same
       P1 mode (2).  D0 is inverse VIDEO and D1 selects brighter amber. */
    normal.clp.gdc.display_mode = TNC155_GDC_MODE_MIXED;
    inverse.clp.gdc.display_mode = TNC155_GDC_MODE_MIXED;
    bright.clp.gdc.display_mode = TNC155_GDC_MODE_MIXED;
    normal.clp.gdc.display_enabled = true;
    inverse.clp.gdc.display_enabled = true;
    bright.clp.gdc.display_enabled = true;
    normal.clp.gdc.active_lines = 32u;
    inverse.clp.gdc.active_lines = 32u;
    bright.clp.gdc.active_lines = 32u;
    normal.clp.gdc.lines_per_character = 32u;
    inverse.clp.gdc.lines_per_character = 32u;
    bright.clp.gdc.lines_per_character = 32u;
    normal.clp.gdc.pitch = 1u;
    inverse.clp.gdc.pitch = 1u;
    bright.clp.gdc.pitch = 1u;
    memset(normal.clp.gdc.parameter_ram, 0, sizeof(normal.clp.gdc.parameter_ram));
    memset(inverse.clp.gdc.parameter_ram, 0, sizeof(inverse.clp.gdc.parameter_ram));
    memset(bright.clp.gdc.parameter_ram, 0, sizeof(bright.clp.gdc.parameter_ram));
    normal.clp.gdc.parameter_ram[3] = 0x02u;
    inverse.clp.gdc.parameter_ram[3] = 0x02u;
    bright.clp.gdc.parameter_ram[3] = 0x02u;
    memset(normal.clp.gdc.scanout_character_video, 0,
           sizeof(normal.clp.gdc.scanout_character_video));
    memset(inverse.clp.gdc.scanout_character_video, 0,
           sizeof(inverse.clp.gdc.scanout_character_video));
    memset(bright.clp.gdc.scanout_character_video, 0,
           sizeof(bright.clp.gdc.scanout_character_video));
    normal.clp.gdc.scanout_character_video[0] = 0x08u;
    normal.clp.gdc.scanout_character_video[1] = 'M';
    inverse.clp.gdc.scanout_character_video[0] = 0x09u;
    inverse.clp.gdc.scanout_character_video[1] = 'M';
    bright.clp.gdc.scanout_character_video[0] = 0x0au;
    bright.clp.gdc.scanout_character_video[1] = 'M';
    normal.clp.graphics_dram[0] = 0xffu;
    normal.clp.graphics_dram[1] = 0xffu;

    a = calloc(pixels, sizeof(*a));
    b = calloc(pixels, sizeof(*b));
    assert(a != NULL && b != NULL);
    tnc155_video_render(&normal, a, TNC155_VIDEO_WIDTH, false);
    tnc155_video_render(&inverse, b, TNC155_VIDEO_WIDTH, false);

    /* D0 reverses every dot of the full 16x32 cell.  There is no dim/dark
       pedestal: the OFF level in both brightness modes is always black. */
    for (y = 0; y < 32u; ++y) {
        for (x = 0; x < 16u; ++x) {
            uint32_t na = a[y * TNC155_VIDEO_WIDTH + x];
            uint32_t ib = b[y * TNC155_VIDEO_WIDTH + x];
            assert(na == NORMAL_AMBER_ARGB || na == BLACK_ARGB);
            assert(ib == NORMAL_AMBER_ARGB || ib == BLACK_ARGB);
            assert((na == NORMAL_AMBER_ARGB) !=
                   (ib == NORMAL_AMBER_ARGB));
        }
    }

    tnc155_video_render(&bright, b, TNC155_VIDEO_WIDTH, false);
    /* D1 preserves glyph geometry and changes only an ON pixel from normal
       amber to brighter amber.  OFF remains black. */
    for (y = 0; y < 32u; ++y) {
        for (x = 0; x < 16u; ++x) {
            bool normal_set =
                a[y * TNC155_VIDEO_WIDTH + x] == NORMAL_AMBER_ARGB;
            assert(b[y * TNC155_VIDEO_WIDTH + x] ==
                   (normal_set ? BRIGHT_AMBER_ARGB : BLACK_ARGB));
        }
    }

    /* Text-mode scanout is IC31.6/P1-derived and independent of IC31.1. */
    normal.clp.graphics_dram[0] = 0u;
    normal.clp.graphics_dram[1] = 0u;
    tnc155_video_render(&normal, b, TNC155_VIDEO_WIDTH, false);
    assert(memcmp(a, b, pixels * sizeof(*a)) == 0);

    /* Pure graphics mode uses the normal amber ON level. */
    normal.clp.gdc.display_mode = TNC155_GDC_MODE_GRAPHICS;
    normal.clp.gdc.display_enabled = true;
    normal.clp.gdc.active_lines = 490u;
    normal.clp.gdc.pitch = 1u;
    memset(normal.clp.gdc.parameter_ram, 0,
           sizeof(normal.clp.gdc.parameter_ram));
    memset(normal.clp.gdc.scanout_character_video, 0,
           sizeof(normal.clp.gdc.scanout_character_video));
    memset(normal.clp.graphics_dram, 0xff, sizeof(normal.clp.graphics_dram));
    normal.clp.gdc.scanout_character_video[0] = 0x00u;
    normal.clp.gdc.scanout_character_video[1] = 0x01u;
    tnc155_video_render(&normal, a, TNC155_VIDEO_WIDTH, false);
    assert(a[0] == NORMAL_AMBER_ARGB);
    assert(a[1] == BLACK_ARGB);

    memset(normal.clp.graphics_dram, 0x00, sizeof(normal.clp.graphics_dram));
    tnc155_video_render(&normal, b, TNC155_VIDEO_WIDTH, false);
    assert(memcmp(a, b, pixels * sizeof(*a)) == 0);

    /* P2 visualization PRAM page 0: SAD=>02C0, LEN=>01EA (490 lines). */
    memset(normal.clp.gdc.scanout_character_video, 0,
           sizeof(normal.clp.gdc.scanout_character_video));
    normal.clp.gdc.pitch = 32u;
    normal.clp.gdc.parameter_ram[0] = 0xc0u;
    normal.clp.gdc.parameter_ram[1] = 0x02u;
    normal.clp.gdc.parameter_ram[2] = 0xa0u;
    normal.clp.gdc.parameter_ram[3] = 0x1eu;
    normal.clp.gdc.scanout_character_video[0x02c0u * 2u] = 0x00u;
    normal.clp.gdc.scanout_character_video[0x02c0u * 2u + 1u] = 0x01u;
    assert(tnc155_video_active_height(&normal) == 490u);
    tnc155_video_render(&normal, a, TNC155_VIDEO_WIDTH, false);
    assert(a[0] == NORMAL_AMBER_ARGB);

    /* BCTRL blanking produces black everywhere. */
    normal.clp.gdc.display_enabled = false;
    tnc155_video_render(&normal, b, TNC155_VIDEO_WIDTH, false);
    for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y)
        for (x = 0u; x < TNC155_VIDEO_WIDTH; ++x)
            assert(b[y * TNC155_VIDEO_WIDTH + x] == BLACK_ARGB);

    free(a);
    free(b);
    puts("character D0 inverse and D1 normal/bright amber: OK");
    return 0;
}

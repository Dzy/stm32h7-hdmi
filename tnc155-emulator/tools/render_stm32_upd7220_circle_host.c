#include "tnc155/upd7220.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 512u
#define H 490u
#define ACTIVE_LINES 468u
#define ACTIVE_Y0 ((H - ACTIVE_LINES) / 2u)

static uint8_t fb[W * H];

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

static void sync_graphics(tnc155_upd7220 *g)
{
    uint8_t p[8] = {0};
    p[0] = 0x02u;
    p[1] = 30u; /* 32 words = 512 pixels */
    p[6] = (uint8_t)ACTIVE_LINES;
    p[7] = (uint8_t)((ACTIVE_LINES >> 8) & 3u);
    service_cmd(g, 0x0fu);
    for (unsigned i = 0; i < 8u; ++i)
        service_param(g, p[i]);
    service_cmd(g, 0x47u);
    service_param(g, 32u);
}

static void set_pram(tnc155_upd7220 *g)
{
    uint8_t p[16] = {0};
    p[2] = (uint8_t)((ACTIVE_LINES & 15u) << 4);
    p[3] = (uint8_t)((ACTIVE_LINES >> 4) & 0x3fu);
    p[8] = 0xffu;
    p[9] = 0xffu;
    service_cmd(g, 0x70u);
    for (unsigned i = 0; i < 16u; ++i)
        service_param(g, p[i]);
}

static void set_replace(tnc155_upd7220 *g)
{
    service_cmd(g, 0x20u);
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

static void figs_arc(tnc155_upd7220 *g, unsigned dir,
                     uint16_t dc, uint16_t radius)
{
    uint16_t d = (uint16_t)(radius - 1u);
    uint16_t d2 = (uint16_t)(2u * (radius - 1u));
    uint16_t d1 = 0xffffu;
    uint16_t dm = 0u;
    uint8_t p[11] = {0};

    p[0] = (uint8_t)((4u << 3) | (dir & 7u)); /* FIGS type 4 = arc/circle */
    p[1] = (uint8_t)dc;
    p[2] = (uint8_t)((dc >> 8) & 0x3fu);
    p[3] = (uint8_t)d;
    p[4] = (uint8_t)((d >> 8) & 0x3fu);
    p[5] = (uint8_t)d2;
    p[6] = (uint8_t)((d2 >> 8) & 0x3fu);
    p[7] = (uint8_t)d1;
    p[8] = (uint8_t)((d1 >> 8) & 0x3fu);
    p[9] = (uint8_t)dm;
    p[10] = (uint8_t)((dm >> 8) & 0x3fu);

    service_cmd(g, 0x4cu);
    for (unsigned i = 0; i < 11u; ++i)
        service_param(g, p[i]);
    service_cmd(g, 0x6cu);
}

static void arc_from(tnc155_upd7220 *g, unsigned x, unsigned y,
                     unsigned dir, uint16_t dc, uint16_t radius)
{
    set_cursor_xy(g, x, y);
    figs_arc(g, dir, dc, radius);
}

static int pixel_on(unsigned x, unsigned y)
{
    return x < W && y < H && fb[(size_t)y * W + x] != 0u;
}

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (size_t i = 0; i < sizeof(fb); ++i) {
        uint8_t rgb[3];
        if (fb[i] != 0u) {
            rgb[0] = 0xd8u;
            rgb[1] = 0xffffu;
            rgb[2] = 0xffffu;
        } else {
            rgb[0] = rgb[1] = rgb[2] = 0u;
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 1;
}

int main(void)
{
    const unsigned cx = 256u;
    const unsigned cy = 234u;
    const uint16_t radius = 100u;
    /* NEC: DC = ceil(R * sin(45 deg)) = ceil(R / sqrt(2)). */
    const uint16_t dc = 71u;
    tnc155_upd7220 g;

    tnc155_upd7220_init(&g);
    tnc155_upd7220_bind_l8(&g, fb, W, H, W, NULL);
    sync_graphics(&g);
    set_pram(&g);
    set_replace(&g);

    /* A uPD7220 circle is eight independent 45-degree FIGD arcs.
       Each of the four axis intercepts is the start point for two arcs. */
    arc_from(&g, cx + radius, cy,          4u, dc, radius);
    arc_from(&g, cx + radius, cy,          7u, dc, radius);
    arc_from(&g, cx,          cy - radius, 1u, dc, radius);
    arc_from(&g, cx,          cy - radius, 6u, dc, radius);
    arc_from(&g, cx - radius, cy,          3u, dc, radius);
    arc_from(&g, cx - radius, cy,          0u, dc, radius);
    arc_from(&g, cx,          cy + radius, 2u, dc, radius);
    arc_from(&g, cx,          cy + radius, 5u, dc, radius);

    /* Cardinal points must all be present.  ACTIVE_Y0 is added by the
       direct-L8 mapper because the 468-line GDC image is centered in 490. */
    if (!pixel_on(cx + radius, cy + ACTIVE_Y0) ||
        !pixel_on(cx - radius, cy + ACTIVE_Y0) ||
        !pixel_on(cx, cy - radius + ACTIVE_Y0) ||
        !pixel_on(cx, cy + radius + ACTIVE_Y0)) {
        fprintf(stderr, "circle cardinal-point regression failed\n");
        return 2;
    }

    if (!write_ppm("stm32_upd7220_circle.ppm"))
        return 3;
    return 0;
}

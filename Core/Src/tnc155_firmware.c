#include "tnc155_firmware.h"

#include "ltdc.h"
#include "main.h"
#include "tnc155/machine.h"
#include "tnc155/serial_keyboard.h"
#include "tnc155/video.h"
#include "tnc155_default_user_ram.h"
#include "tnc155_usb_cdc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TNC155_MACHINE_ADDRESS       (SDRAM_BASE_ADDRESS + (10U * 1024U * 1024U))
#define TNC155_VIDEO_MIN_PERIOD_MS   20U
#define TNC155_DEBUG_PERIOD_MS       200U
#define TNC155_STEP_SLICE            4096U
#define TNC155_CATCHUP_LIMIT_CYCLES  (TNC155_MAIN_CPU_CLOCK_HZ / 10U)

#define HDMI_WIDTH  1920U
#define HDMI_HEIGHT 1080U
#define TNC_X0      ((HDMI_WIDTH - TNC155_VIDEO_WIDTH) / 2U)
#define TNC_Y0_MAX  ((HDMI_HEIGHT - TNC155_VIDEO_HEIGHT) / 2U)

#define DEBUG_X       12U
#define DEBUG_Y       12U
#define DEBUG_W       620U
#define DEBUG_H       164U
#define DEBUG_SCALE   2U
#define DEBUG_FG      0xffU
#define DEBUG_BG      0x00U

/* L8 values are CLUT indices, not four phosphor colours.  D1 selects the
   brightness of an ON pixel; both OFF states are black. */
enum {
    TNC_L8_OFF_NORMAL = 0x00,
    TNC_L8_ON_NORMAL  = 0xdf,
    TNC_L8_OFF_BRIGHT = 0x04,
    TNC_L8_ON_BRIGHT  = 0x71
};

_Static_assert((TNC155_MACHINE_ADDRESS + sizeof(tnc155_machine)) <=
               (SDRAM_BASE_ADDRESS + SDRAM_SIZE_BYTES),
               "TNC155 machine state does not fit external SDRAM");
_Static_assert(LTDC_VID_FORMAT == 8U,
               "TNC155 firmware currently targets the 1920x1080p60 L8 mode");

static tnc155_machine *const s_machine =
    (tnc155_machine *)(uintptr_t)TNC155_MACHINE_ADDRESS;
static tnc155_serial_keyboard s_keyboard;
static uint32_t s_epoch_ms;
static uint64_t s_epoch_cycles;
static uint32_t s_next_video_ms;
static uint32_t s_next_debug_ms;
static uint32_t s_rendered_fifo_entries;
static uint32_t s_rendered_words_written;
static uint32_t s_rendered_scanout_commits;
static volatile uint8_t s_front_fb;
static volatile uint8_t s_pending_fb;
static volatile uint8_t s_swap_pending;

volatile uint32_t g_tnc155_last_slice_core_cycles;
volatile uint32_t g_tnc155_max_slice_core_cycles;
volatile uint32_t g_tnc155_last_frame_core_cycles;
volatile uint8_t g_tnc155_faulted;

static const uint8_t debug_digits[10][7] = {
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},
    {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},
    {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
    {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e},
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},
    {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}
};

static const uint8_t debug_upper[26][7] = {
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
    {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f},
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
    {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f},
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0c},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}
};

static uint8_t debug_glyph_row(char c, unsigned row)
{
    if (row >= 7u)
        return 0u;
    if (c >= '0' && c <= '9')
        return debug_digits[(unsigned)(c - '0')][row];
    if (c >= 'A' && c <= 'Z')
        return debug_upper[(unsigned)(c - 'A')][row];
    if (c == '=')
        return (row == 2u || row == 4u) ? 0x1fu : 0u;
    if (c == '-')
        return row == 3u ? 0x1fu : 0u;
    if (c == ':')
        return (row == 2u || row == 5u) ? 0x04u : 0u;
    if (c == '.')
        return row == 6u ? 0x04u : 0u;
    return 0u;
}

static void debug_draw_char(uint8_t *dst, unsigned x0, unsigned y0, char c)
{
    unsigned gy;
    unsigned gx;
    unsigned sy;
    unsigned sx;

    for (gy = 0u; gy < 7u; ++gy) {
        uint8_t bits = debug_glyph_row(c, gy);
        for (gx = 0u; gx < 5u; ++gx) {
            uint8_t colour = (bits & (uint8_t)(1u << (4u - gx))) != 0u ?
                             DEBUG_FG : DEBUG_BG;
            for (sy = 0u; sy < DEBUG_SCALE; ++sy) {
                unsigned y = y0 + gy * DEBUG_SCALE + sy;
                if (y >= HDMI_HEIGHT)
                    continue;
                for (sx = 0u; sx < DEBUG_SCALE; ++sx) {
                    unsigned x = x0 + gx * DEBUG_SCALE + sx;
                    if (x < HDMI_WIDTH)
                        dst[(size_t)y * HDMI_WIDTH + x] = colour;
                }
            }
        }
    }
}

static void debug_draw_text(uint8_t *dst, unsigned x, unsigned y,
                            const char *text)
{
    while (*text != '\0') {
        debug_draw_char(dst, x, y, *text++);
        x += 6u * DEBUG_SCALE;
    }
}

static char *debug_append_text(char *p, const char *text)
{
    while (*text != '\0')
        *p++ = *text++;
    return p;
}

static char *debug_append_hex16(char *p, uint16_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    for (shift = 12; shift >= 0; shift -= 4)
        *p++ = hex[(value >> shift) & 0x0fu];
    return p;
}

static char *debug_append_hex32(char *p, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        *p++ = hex[(value >> shift) & 0x0fu];
    return p;
}

static char *debug_append_u32(char *p, uint32_t value)
{
    char reverse[10];
    unsigned count = 0u;
    if (value == 0u) {
        *p++ = '0';
        return p;
    }
    while (value != 0u && count < sizeof(reverse)) {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count != 0u)
        *p++ = reverse[--count];
    return p;
}

static void debug_draw_line(uint8_t *dst, unsigned line, const char *text)
{
    debug_draw_text(dst, DEBUG_X + 6u,
                    DEBUG_Y + 5u + line * (8u * DEBUG_SCALE), text);
}

static uint64_t slowest_machine_cycles(void)
{
    return s_machine->main.cpu.cycles < s_machine->clp.cpu.cycles ?
           s_machine->main.cpu.cycles : s_machine->clp.cpu.cycles;
}

static void draw_debug_overlay(uint8_t *dst)
{
    char line[64];
    char *p;
    uint32_t now_ms = HAL_GetTick();
    uint64_t expected_cycles = s_epoch_cycles +
        (uint64_t)(now_ms - s_epoch_ms) *
        (TNC155_MAIN_CPU_CLOCK_HZ / 1000u);
    uint64_t slowest = slowest_machine_cycles();
    uint64_t lag_cycles = expected_cycles > slowest ?
                          expected_cycles - slowest : 0u;
    uint32_t lag_ms = (uint32_t)(lag_cycles /
                                 (TNC155_MAIN_CPU_CLOCK_HZ / 1000u));
    unsigned y;

    for (y = DEBUG_Y; y < DEBUG_Y + DEBUG_H; ++y)
        memset(dst + (size_t)y * HDMI_WIDTH + DEBUG_X,
               DEBUG_BG, DEBUG_W);

    debug_draw_line(dst, 0u, "TNC155 STM32 DEBUG");

    p = debug_append_text(line, "MAIN PC=");
    p = debug_append_hex16(p, s_machine->main.cpu.pc);
    p = debug_append_text(p, " WP=");
    p = debug_append_hex16(p, s_machine->main.cpu.wp);
    p = debug_append_text(p, " ST=");
    p = debug_append_hex16(p, s_machine->main.cpu.st);
    *p = '\0';
    debug_draw_line(dst, 1u, line);

    p = debug_append_text(line, "CLP  PC=");
    p = debug_append_hex16(p, s_machine->clp.cpu.pc);
    p = debug_append_text(p, " WP=");
    p = debug_append_hex16(p, s_machine->clp.cpu.wp);
    p = debug_append_text(p, " ST=");
    p = debug_append_hex16(p, s_machine->clp.cpu.st);
    *p = '\0';
    debug_draw_line(dst, 2u, line);

    p = debug_append_text(line, "MAIN C=");
    p = debug_append_hex32(p, (uint32_t)s_machine->main.cpu.cycles);
    p = debug_append_text(p, " CLP C=");
    p = debug_append_hex32(p, (uint32_t)s_machine->clp.cpu.cycles);
    *p = '\0';
    debug_draw_line(dst, 3u, line);

    p = debug_append_text(line, "LAGMS=");
    p = debug_append_u32(p, lag_ms);
    p = debug_append_text(p, " TICK=");
    p = debug_append_hex32(p, now_ms);
    *p = '\0';
    debug_draw_line(dst, 4u, line);

    p = debug_append_text(line, "USB=");
    p = debug_append_text(p, TNC155_USB_CDC_Connected() ? "UP" : "DOWN");
    p = debug_append_text(p, " KD=");
    p = debug_append_hex32(p, (uint32_t)s_keyboard.key_down_messages);
    p = debug_append_text(p, " KU=");
    p = debug_append_hex32(p, (uint32_t)s_keyboard.key_up_messages);
    *p = '\0';
    debug_draw_line(dst, 5u, line);

    p = debug_append_text(line, "UNMAP=");
    p = debug_append_hex32(p, (uint32_t)s_keyboard.unmapped_messages);
    p = debug_append_text(p, " DUP=");
    p = debug_append_hex32(p, (uint32_t)s_keyboard.duplicate_key_downs);
    *p = '\0';
    debug_draw_line(dst, 6u, line);

    p = debug_append_text(line, "FAULT=");
    *p++ = g_tnc155_faulted ? '1' : '0';
    p = debug_append_text(p, " SWAP=");
    *p++ = s_swap_pending ? '1' : '0';
    p = debug_append_text(p, " FB=");
    *p++ = s_front_fb ? '1' : '0';
    *p = '\0';
    debug_draw_line(dst, 7u, line);

    p = debug_append_text(line, "SL=");
    p = debug_append_hex32(p, g_tnc155_last_slice_core_cycles);
    p = debug_append_text(p, " MX=");
    p = debug_append_hex32(p, g_tnc155_max_slice_core_cycles);
    p = debug_append_text(p, " FR=");
    p = debug_append_hex32(p, g_tnc155_last_frame_core_cycles);
    *p = '\0';
    debug_draw_line(dst, 8u, line);
}

static void load_rgb332_clut(void)
{
    uint32_t clut[256];
    uint32_t i;

    for (i = 0u; i < 256u; ++i) {
        uint32_t r3 = (i >> 5) & 7u;
        uint32_t g3 = (i >> 2) & 7u;
        uint32_t b2 = i & 3u;
        uint32_t r = (r3 * 255u + 3u) / 7u;
        uint32_t g = (g3 * 255u + 3u) / 7u;
        uint32_t b = (b2 * 255u + 1u) / 3u;
        clut[i] = (r << 16) | (g << 8) | b;
    }

    if (HAL_LTDC_ConfigCLUT(&hltdc, clut, 256u, 0u) != HAL_OK ||
        HAL_LTDC_EnableCLUT(&hltdc, 0u) != HAL_OK)
        Error_Handler();
    HAL_LTDC_DisableDither(&hltdc);
}

static uint32_t framebuffer_address(uint8_t index)
{
    return index != 0u ? FRAMEBUFFER1_ADDRESS : FRAMEBUFFER0_ADDRESS;
}

static void clear_framebuffer(uint32_t address)
{
    memset((void *)(uintptr_t)address, 0,
           (size_t)HDMI_WIDTH * HDMI_HEIGHT);
    __DSB();
}

typedef struct tnc_l8_partition {
    uint32_t start;
    uint16_t length;
    bool graphics;
} tnc_l8_partition;

static tnc_l8_partition decode_partition(const tnc155_upd7220 *gdc,
                                         unsigned area)
{
    unsigned base = area * 4u;
    tnc_l8_partition part;
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

static bool locate_scanline(const tnc155_upd7220 *gdc, unsigned y,
                            tnc_l8_partition *part, unsigned *local_y)
{
    unsigned area;
    unsigned base_y = 0u;
    unsigned active = gdc->active_lines != 0u ? gdc->active_lines :
        (gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ?
         TNC155_VIDEO_GRAPHICS_HEIGHT : TNC155_VIDEO_TEXT_HEIGHT);

    for (area = 0u; area < 4u && base_y < active; ++area) {
        tnc_l8_partition candidate = decode_partition(gdc, area);
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

static void render_tnc_l8(uint8_t *dst)
{
    static const uint8_t phosphor[4] = {
        TNC_L8_OFF_NORMAL, TNC_L8_ON_NORMAL,
        TNC_L8_OFF_BRIGHT, TNC_L8_ON_BRIGHT
    };
    const tnc155_upd7220 *gdc = &s_machine->clp.gdc;
    unsigned active_height = tnc155_video_active_height(s_machine);
    unsigned line_height = gdc->lines_per_character != 0u ?
                           gdc->lines_per_character : 1u;
    unsigned dst_y0;
    unsigned y;

    if (active_height > TNC155_VIDEO_HEIGHT)
        active_height = TNC155_VIDEO_HEIGHT;

    /* Always clear the maximum TNC rectangle; 468/490-line mode transitions
       then cannot leave stale scanlines in the framebuffer. */
    for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y)
        memset(dst + (size_t)(TNC_Y0_MAX + y) * HDMI_WIDTH + TNC_X0,
               TNC_L8_OFF_NORMAL, TNC155_VIDEO_WIDTH);

    if (!gdc->display_enabled)
        return;

    dst_y0 = (HDMI_HEIGHT - active_height) / 2u;
    for (y = 0u; y < active_height; ++y) {
        tnc_l8_partition part;
        unsigned local_y;
        unsigned effective_pitch;
        uint8_t *row;
        unsigned word_column;

        if (!locate_scanline(gdc, y, &part, &local_y))
            continue;

        row = dst + (size_t)(dst_y0 + y) * HDMI_WIDTH + TNC_X0;
        effective_pitch = gdc->pitch != 0u ? gdc->pitch : 1u;

        if (!part.graphics) {
            uint32_t line_base = part.start +
                (local_y / line_height) * effective_pitch;

            for (word_column = 0u; word_column < 32u; ++word_column) {
                uint32_t word = (line_base + word_column) & 0x7fffu;
                uint16_t cell = (uint16_t)(
                    (uint16_t)gdc->scanout_character_video[word * 2u] << 8 |
                    gdc->scanout_character_video[word * 2u + 1u]);
                uint8_t ascii = (uint8_t)cell;
                uint8_t mode_data = (uint8_t)(cell >> 8);
                unsigned p1_mode = (mode_data >> 2) & 0x07u;
                uint8_t glyph = tnc155_clp_font_row(
                    p1_mode, ascii & 0x3fu, local_y % line_height);
                bool inverse = (mode_data & 0x01u) != 0u;
                unsigned colour_base = (mode_data & 0x02u) != 0u ? 2u : 0u;
                unsigned bit;
                uint8_t *cell_dst = row + word_column * 16u;

                for (bit = 0u; bit < 8u; ++bit) {
                    bool set = (glyph & (uint8_t)(0x80u >> bit)) != 0u;
                    uint8_t colour;
                    if (inverse)
                        set = !set;
                    colour = phosphor[colour_base | (set ? 1u : 0u)];
                    cell_dst[bit * 2u] = colour;
                    cell_dst[bit * 2u + 1u] = colour;
                }
            }
        } else {
            unsigned pitch = effective_pitch;
            if (gdc->display_mode == TNC155_GDC_MODE_MIXED)
                pitch >>= 1;
            if (pitch == 0u)
                pitch = 1u;

            for (word_column = 0u; word_column < 32u; ++word_column) {
                uint32_t word = (part.start + local_y * pitch + word_column) &
                                0x7fffu;
                uint16_t bits = (uint16_t)(
                    (uint16_t)gdc->scanout_character_video[word * 2u] << 8 |
                    gdc->scanout_character_video[word * 2u + 1u]);
                unsigned bit;
                uint8_t *word_dst = row + word_column * 16u;
                for (bit = 0u; bit < 16u; ++bit)
                    word_dst[bit] = (bits & (uint16_t)(1u << bit)) != 0u ?
                                    TNC_L8_ON_NORMAL : TNC_L8_OFF_NORMAL;
            }
        }
    }
}

static void copy_tnc_l8(uint8_t *dst, const uint8_t *src)
{
    unsigned y;
    for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y) {
        size_t offset = (size_t)(TNC_Y0_MAX + y) * HDMI_WIDTH + TNC_X0;
        memcpy(dst + offset, src + offset, TNC155_VIDEO_WIDTH);
    }
}

static bool gdc_video_dirty(void)
{
    const tnc155_upd7220 *gdc = &s_machine->clp.gdc;
    return gdc->fifo_entries_processed != s_rendered_fifo_entries ||
           gdc->words_written != s_rendered_words_written ||
           gdc->scanout_commits != s_rendered_scanout_commits;
}

static void remember_rendered_gdc_state(void)
{
    const tnc155_upd7220 *gdc = &s_machine->clp.gdc;
    s_rendered_fifo_entries = gdc->fifo_entries_processed;
    s_rendered_words_written = gdc->words_written;
    s_rendered_scanout_commits = gdc->scanout_commits;
}

static void run_machine_slice(void)
{
    uint32_t start_core_cycles = DWT->CYCCNT;
    uint32_t now_ms = HAL_GetTick();
    uint64_t target_cycles = s_epoch_cycles +
        (uint64_t)(now_ms - s_epoch_ms) *
        (TNC155_MAIN_CPU_CLOCK_HZ / 1000u);
    uint64_t slowest = slowest_machine_cycles();
    unsigned steps = 0u;

    if (target_cycles > slowest + TNC155_CATCHUP_LIMIT_CYCLES)
        target_cycles = slowest + TNC155_CATCHUP_LIMIT_CYCLES;

    while (steps < TNC155_STEP_SLICE &&
           (s_machine->main.cpu.cycles < target_cycles ||
            s_machine->clp.cpu.cycles < target_cycles)) {
        tms9995_step_result result = tnc155_machine_step(s_machine, NULL);
        if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE) {
            g_tnc155_faulted = 1u;
            break;
        }
        ++steps;
    }

    g_tnc155_last_slice_core_cycles = DWT->CYCCNT - start_core_cycles;
    if (g_tnc155_last_slice_core_cycles > g_tnc155_max_slice_core_cycles)
        g_tnc155_max_slice_core_cycles = g_tnc155_last_slice_core_cycles;
}

static bool present_frame(bool redraw_tnc)
{
    uint32_t start_core_cycles;
    uint8_t back_fb;
    uint8_t *dst;

    if (s_swap_pending != 0u)
        return false;

    start_core_cycles = DWT->CYCCNT;
    back_fb = (uint8_t)(s_front_fb ^ 1u);
    dst = (uint8_t *)(uintptr_t)framebuffer_address(back_fb);

    if (redraw_tnc) {
        render_tnc_l8(dst);
    } else {
        const uint8_t *front =
            (const uint8_t *)(uintptr_t)framebuffer_address(s_front_fb);
        copy_tnc_l8(dst, front);
    }

    draw_debug_overlay(dst);

    __DSB();
    if (HAL_LTDC_SetAddress_NoReload(&hltdc, framebuffer_address(back_fb), 0u)
            != HAL_OK) {
        g_tnc155_faulted = 1u;
        return false;
    }

    s_pending_fb = back_fb;
    s_swap_pending = 1u;
    if (HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        s_swap_pending = 0u;
        g_tnc155_faulted = 1u;
        return false;
    }

    g_tnc155_last_frame_core_cycles = DWT->CYCCNT - start_core_cycles;
    return true;
}

bool TNC155_Firmware_Init(void)
{
    uint64_t slowest;
    uint32_t now;

    g_tnc155_faulted = 0u;
    g_tnc155_last_slice_core_cycles = 0u;
    g_tnc155_max_slice_core_cycles = 0u;
    g_tnc155_last_frame_core_cycles = 0u;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    load_rgb332_clut();
    clear_framebuffer(FRAMEBUFFER0_ADDRESS);
    clear_framebuffer(FRAMEBUFFER1_ADDRESS);
    s_front_fb = 0u;
    s_pending_fb = 0u;
    s_swap_pending = 0u;

    if (!tnc155_machine_init(s_machine))
        return false;

    if (tnc155_default_user_ram_size != sizeof(s_machine->main.user_ram))
        return false;
    memcpy(s_machine->main.user_ram, tnc155_default_user_ram,
           sizeof(s_machine->main.user_ram));
    s_machine->main.user_ram_loaded = true;
    s_machine->main.user_ram_dirty = false;

    tnc155_machine_set_control_voltage_enabled(s_machine, true);

    tnc155_serial_keyboard_init(&s_keyboard);
    TNC155_USB_CDC_Init(&s_keyboard, &s_machine->main.keyboard);

    slowest = slowest_machine_cycles();
    now = HAL_GetTick();
    s_epoch_ms = now;
    s_epoch_cycles = slowest;
    s_next_video_ms = now + TNC155_VIDEO_MIN_PERIOD_MS;
    s_next_debug_ms = now + TNC155_DEBUG_PERIOD_MS;

    /* Force the first frame to establish both the TNC image and the initial
       GDC generation snapshot. */
    s_rendered_fifo_entries = UINT32_MAX;
    s_rendered_words_written = UINT32_MAX;
    s_rendered_scanout_commits = UINT32_MAX;
    if (!present_frame(true))
        return false;
    remember_rendered_gdc_state();
    return true;
}

void TNC155_Firmware_Task(void)
{
    uint32_t now;
    bool dirty;
    bool video_due;
    bool debug_due;
    bool redraw_tnc;

    TNC155_USB_CDC_Task();

    if (g_tnc155_faulted == 0u)
        run_machine_slice();

    now = HAL_GetTick();
    dirty = gdc_video_dirty();
    video_due = (int32_t)(now - s_next_video_ms) >= 0;
    debug_due = (int32_t)(now - s_next_debug_ms) >= 0;
    redraw_tnc = dirty && video_due;

    if (s_swap_pending == 0u && (redraw_tnc || debug_due)) {
        if (present_frame(redraw_tnc)) {
            if (redraw_tnc) {
                remember_rendered_gdc_state();
                s_next_video_ms = now + TNC155_VIDEO_MIN_PERIOD_MS;
            }
            s_next_debug_ms = now + TNC155_DEBUG_PERIOD_MS;
        }
    }
}

void TNC155_Firmware_LTDCReloadComplete(void)
{
    if (s_swap_pending != 0u) {
        s_front_fb = s_pending_fb;
        s_swap_pending = 0u;
    }
}

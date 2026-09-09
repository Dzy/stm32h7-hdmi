#include "tnc155_firmware.h"

#include "dma2d.h"
#include "ltdc.h"
#include "main.h"
#include "tnc155/machine.h"
#include "tnc155/roms.h"
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
#define TNC155_HW_TICK_HZ            1000U
#define TNC155_DEC_TICKS_PER_MS      (TNC155_MAIN_CPU_CLOCK_HZ / 16000U)
#define TNC155_PENDING_LEVEL3        (1U << 2)

#define HDMI_WIDTH  1920U
#define HDMI_HEIGHT 1080U
#define TNC_X0      ((HDMI_WIDTH - TNC155_VIDEO_WIDTH) / 2U)
#define TNC_Y0_MAX  ((HDMI_HEIGHT - TNC155_VIDEO_HEIGHT) / 2U)

#define TNC_NATIVE_WIDTH        TNC155_VIDEO_WIDTH
#define TNC_NATIVE_HEIGHT       TNC155_VIDEO_HEIGHT
#define TNC_NATIVE_BYTES        ((size_t)TNC_NATIVE_WIDTH * TNC_NATIVE_HEIGHT)
#define TNC_NATIVE_ADDRESS      AXI_SRAM_BASE_ADDRESS
#define TNC_FONT_ATLAS_BANKS    6U
#define TNC_FONT_ATLAS_GLYPHS   64U
#define TNC_FONT_ATLAS_ROWS     32U
#define TNC_FONT_ATLAS_ROW_BYTES 16U
#define TNC_FONT_ATLAS_BYTES    ((size_t)TNC_FONT_ATLAS_BANKS * \
                                 TNC_FONT_ATLAS_GLYPHS * \
                                 TNC_FONT_ATLAS_ROWS * \
                                 TNC_FONT_ATLAS_ROW_BYTES)
#define TNC_FONT_ATLAS_ADDRESS  (TNC_NATIVE_ADDRESS + \
                                 ((TNC_NATIVE_BYTES + 31U) & ~(size_t)31U))
#define TNC_DMA2D_WIDTH         (TNC_NATIVE_WIDTH / 2U)
#define TNC_DMA2D_FB_STRIDE     (HDMI_WIDTH / 2U)
#define TNC_DMA2D_LINE_OFFSET   (TNC_DMA2D_FB_STRIDE - TNC_DMA2D_WIDTH)
#define TNC_DMA2D_TIMEOUT_MS    10U

#define DEBUG_X       12U
#define DEBUG_Y       12U
#define DEBUG_W       620U
#define DEBUG_H       164U
#define DEBUG_SCALE   2U
#define DEBUG_FG      0xffU
#define DEBUG_BG      0x00U

/* L8 values are CLUT indices, not four phosphor colours. D1 selects the
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
_Static_assert((TNC_NATIVE_WIDTH & 1U) == 0U && (HDMI_WIDTH & 1U) == 0U &&
               (TNC_X0 & 1U) == 0U,
               "DMA2D raw L8 blit requires even width and X alignment");
_Static_assert(TNC_NATIVE_BYTES <= (512U * 1024U),
               "TNC155 native staging image does not fit AXI SRAM");
_Static_assert((TNC_FONT_ATLAS_ADDRESS + TNC_FONT_ATLAS_BYTES) <=
               (AXI_SRAM_BASE_ADDRESS + AXI_SRAM_SIZE_BYTES),
               "TNC155 native L8 image plus font atlas do not fit AXI SRAM");
_Static_assert((offsetof(tnc155_upd7220, scanout_character_video) & 3u) == 0u,
               "uPD7220 scanout must be 32-bit aligned");
_Static_assert((TNC155_MAIN_CPU_CLOCK_HZ % 16000U) == 0U,
               "1 ms hardware tick must map exactly to TMS9995 decrementer ticks");
_Static_assert((TNC155_STEP_SLICE % TNC155_CPU_QUANTUM) == 0U,
               "firmware slice must contain complete CPU quanta");

static tnc155_machine *const s_machine =
    (tnc155_machine *)(uintptr_t)TNC155_MACHINE_ADDRESS;
static uint8_t *const s_native_frame =
    (uint8_t *)(uintptr_t)TNC_NATIVE_ADDRESS;
static uint8_t *const s_font_atlas =
    (uint8_t *)(uintptr_t)TNC_FONT_ATLAS_ADDRESS;
static tnc155_serial_keyboard s_keyboard;

/* The P1 font is expanded once at boot directly into a byte-per-pixel L8
   atlas in AXI SRAM using the normal phosphor CLUT indices.  Modes 0..2
   share the canonical mode-0 glyph bank and apply only their measured
   vertical offsets when blitted; modes 3..7 each have one bank.  Normal
   text is therefore four aligned 32-bit copies per glyph row; inverse and
   bright attributes are derived with word-wide arithmetic, never bitmaps. */
static uint32_t s_graphics_byte_lut[256][2];

static uint32_t s_next_video_ms;
static uint32_t s_next_debug_ms;
static uint32_t s_rendered_fifo_entries;
static uint32_t s_rendered_words_written;
static uint32_t s_rendered_scanout_commits;
static uint8_t s_native_full_redraw;
static volatile uint32_t s_pending_hw_ms;
static volatile uint8_t s_hw_time_active;
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

static void draw_debug_overlay(uint8_t *dst)
{
    char line[64];
    char *p;
    uint32_t now_ms = HAL_GetTick();
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

    p = debug_append_text(line, "ALT=1 HWMS=");
    p = debug_append_u32(p, now_ms);
    *p = '\0';
    debug_draw_line(dst, 3u, line);

    p = debug_append_text(line, "PEND=");
    p = debug_append_u32(p, s_pending_hw_ms);
    p = debug_append_text(p, " STEP=");
    p = debug_append_u32(p, TNC155_STEP_SLICE);
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

static uint32_t framebuffer_tnc_address(uint8_t index)
{
    return framebuffer_address(index) +
           (uint32_t)((size_t)TNC_Y0_MAX * HDMI_WIDTH + TNC_X0);
}

static void clear_framebuffer(uint32_t address)
{
    memset((void *)(uintptr_t)address, 0,
           (size_t)HDMI_WIDTH * HDMI_HEIGHT);
    __DSB();
}

static bool init_raster_luts(void)
{
    const tnc155_rom_view *font = tnc155_rom_get(TNC155_ROM_P1);
    unsigned bank;
    unsigned glyph;
    unsigned row;
    unsigned pattern;

    if (font == NULL || font->bytes == NULL || font->size < 0x4000u)
        return false;

    for (bank = 0u; bank < TNC_FONT_ATLAS_BANKS; ++bank) {
        unsigned p1_mode = bank == 0u ? 0u : bank + 2u;
        for (glyph = 0u; glyph < TNC_FONT_ATLAS_GLYPHS; ++glyph) {
            for (row = 0u; row < TNC_FONT_ATLAS_ROWS; ++row) {
                uint8_t bits = font->bytes[
                    ((size_t)p1_mode * 64u + glyph) * 32u + row];
                uint8_t *dst = s_font_atlas +
                    (((size_t)bank * TNC_FONT_ATLAS_GLYPHS + glyph) *
                     TNC_FONT_ATLAS_ROWS + row) * TNC_FONT_ATLAS_ROW_BYTES;
                unsigned bit;

                for (bit = 0u; bit < 8u; ++bit) {
                    uint8_t pixel = (bits & (0x80u >> bit)) != 0u ?
                                    TNC_L8_ON_NORMAL : TNC_L8_OFF_NORMAL;
                    dst[bit * 2u] = pixel;
                    dst[bit * 2u + 1u] = pixel;
                }
            }
        }
    }

    for (pattern = 0u; pattern < 256u; ++pattern) {
        uint8_t *expanded = (uint8_t *)&s_graphics_byte_lut[pattern][0];
        unsigned bit;
        for (bit = 0u; bit < 8u; ++bit)
            expanded[bit] = (pattern & (1u << bit)) != 0u ?
                            TNC_L8_ON_NORMAL : TNC_L8_OFF_NORMAL;
    }
    return true;
}

static bool configure_dma2d_raw_l8(void)
{
    hdma2d.Init.Mode = DMA2D_M2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = TNC_DMA2D_LINE_OFFSET;
    hdma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.Init.BytesSwap = DMA2D_BYTES_REGULAR;
    hdma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    hdma2d.LayerCfg[1].InputOffset = 0u;
    hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
    hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[1].InputAlpha = 0xffu;
    hdma2d.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.LayerCfg[1].ChromaSubSampling = DMA2D_NO_CSS;

    return HAL_DMA2D_Init(&hdma2d) == HAL_OK &&
           HAL_DMA2D_ConfigLayer(&hdma2d, 1u) == HAL_OK;
}

static bool dma2d_copy_tnc(uint32_t source, uint32_t destination,
                           bool source_is_fullhd)
{
    /* RGB565 is deliberately only a 16-bit transport unit here. The two
       bytes of each DMA2D pixel are two adjacent L8 pixels, and because input
       and output formats are identical M2M performs no conversion. */
    WRITE_REG(hdma2d.Instance->FGOR,
              source_is_fullhd ? TNC_DMA2D_LINE_OFFSET : 0u);

    if (HAL_DMA2D_Start(&hdma2d, source, destination,
                        TNC_DMA2D_WIDTH, TNC_NATIVE_HEIGHT) != HAL_OK)
        return false;
    return HAL_DMA2D_PollForTransfer(&hdma2d, TNC_DMA2D_TIMEOUT_MS) == HAL_OK;
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

/* P1 modes 0..2 contain the same 64-glyph small font at different
   vertical positions. ROM comparison gives the common offsets below for
   63/64 glyphs; the two exceptional glyphs are also exact copies of mode 0,
   just at a different Y offset. Therefore modes 0..2 all read the mode-0
   source bank and only alter source-row addressing. */
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

static unsigned p1_atlas_bank(unsigned p1_mode)
{
    return p1_mode <= 2u ? 0u : p1_mode - 2u;
}

static void blit_atlas_row(unsigned attr, const uint32_t *src,
                           uint32_t *dst)
{
    unsigned i;

    switch (attr & 3u) {
    case 0u:
        /* Dominant path: atlas bytes already are final L8 pixels. */
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

static uint16_t load_backing_word16(const tnc155_upd7220 *gdc, uint32_t word)
{
    uint32_t address = (word & 0x7fffu) * 2u;
    return (uint16_t)((uint16_t)gdc->character_video_dram[address] << 8 |
                      gdc->character_video_dram[address + 1u]);
}

static uint32_t load_backing_pair32(const tnc155_upd7220 *gdc,
                                    uint32_t even_word)
{
    uint32_t raw;
    uint32_t address = (even_word & 0x7fffu) * 2u;
    memcpy(&raw, &gdc->character_video_dram[address], sizeof(raw));
    return __REV16(raw);
}

static void render_text_cell(uint16_t cell, unsigned dst_base_y,
                             unsigned cell_y, unsigned cell_height,
                             unsigned word_column)
{
    static const uint32_t zero_row[4] = {0u, 0u, 0u, 0u};
    uint8_t ascii = (uint8_t)cell;
    uint8_t mode_data = (uint8_t)(cell >> 8);
    unsigned p1_mode = (mode_data >> 2) & 0x07u;
    unsigned character = ascii & 0x3fu;
    unsigned attr = ((mode_data & 0x02u) != 0u ? 2u : 0u) |
                    ((mode_data & 0x01u) != 0u ? 1u : 0u);
    unsigned bank = p1_atlas_bank(p1_mode);
    int y_offset = p1_mode <= 2u ?
                   p1_small_font_y_offset(p1_mode, character) : 0;
    unsigned row_in_cell;

    for (row_in_cell = 0u; row_in_cell < cell_height; ++row_in_cell) {
        int source_row = (int)row_in_cell - y_offset;
        const uint32_t *src = zero_row;
        uint32_t *dst = (uint32_t *)(void *)(s_native_frame +
            (size_t)(dst_base_y + cell_y + row_in_cell) *
            TNC_NATIVE_WIDTH + word_column * 16u);

        if (source_row >= 0 && source_row < (int)TNC_FONT_ATLAS_ROWS) {
            src = (const uint32_t *)(const void *)(s_font_atlas +
                (((size_t)bank * TNC_FONT_ATLAS_GLYPHS + character) *
                 TNC_FONT_ATLAS_ROWS + (unsigned)source_row) *
                TNC_FONT_ATLAS_ROW_BYTES);
        }
        blit_atlas_row(attr, src, dst);
    }
}

static void render_text_partition(const tnc155_upd7220 *gdc,
                                  const tnc_l8_partition *part,
                                  unsigned dst_base_y, unsigned line_height,
                                  unsigned effective_pitch)
{
    unsigned cell_y;

    for (cell_y = 0u; cell_y < part->length; cell_y += line_height) {
        unsigned cell_height = line_height;
        uint32_t line_base;
        unsigned word_column = 0u;

        if (cell_height > (unsigned)part->length - cell_y)
            cell_height = (unsigned)part->length - cell_y;
        line_base = part->start + (cell_y / line_height) * effective_pitch;

        while (word_column < 32u) {
            uint32_t word = (line_base + word_column) & 0x7fffu;

            if ((word & 1u) == 0u && word_column + 1u < 32u) {
                uint32_t pair = load_backing_pair32(gdc, word);
                render_text_cell((uint16_t)pair, dst_base_y, cell_y,
                                 cell_height, word_column);
                render_text_cell((uint16_t)(pair >> 16), dst_base_y, cell_y,
                                 cell_height, word_column + 1u);
                word_column += 2u;
            } else {
                render_text_cell(load_backing_word16(gdc, word),
                                 dst_base_y, cell_y, cell_height, word_column);
                ++word_column;
            }
        }
    }
}

static void render_graphics_word(uint8_t *row, unsigned word_column,
                                 uint16_t bits)
{
    uint32_t *dst = (uint32_t *)(void *)(row + word_column * 16u);
    const uint32_t *lo = &s_graphics_byte_lut[(uint8_t)bits][0];
    const uint32_t *hi = &s_graphics_byte_lut[(uint8_t)(bits >> 8)][0];

    dst[0] = lo[0];
    dst[1] = lo[1];
    dst[2] = hi[0];
    dst[3] = hi[1];
}

static void render_graphics_partition(const tnc155_upd7220 *gdc,
                                      const tnc_l8_partition *part,
                                      unsigned dst_base_y,
                                      unsigned effective_pitch)
{
    unsigned pitch = effective_pitch;
    unsigned local_y;

    if (gdc->display_mode == TNC155_GDC_MODE_MIXED)
        pitch >>= 1;
    if (pitch == 0u)
        pitch = 1u;

    for (local_y = 0u; local_y < part->length; ++local_y) {
        uint8_t *row = s_native_frame +
                       (size_t)(dst_base_y + local_y) * TNC_NATIVE_WIDTH;
        uint32_t line_base = part->start + local_y * pitch;
        unsigned word_column = 0u;

        while (word_column < 32u) {
            uint32_t word = (line_base + word_column) & 0x7fffu;

            if ((word & 1u) == 0u && word_column + 1u < 32u) {
                uint32_t pair = load_backing_pair32(gdc, word);
                render_graphics_word(row, word_column, (uint16_t)pair);
                render_graphics_word(row, word_column + 1u,
                                     (uint16_t)(pair >> 16));
                word_column += 2u;
            } else {
                render_graphics_word(row, word_column,
                                     load_backing_word16(gdc, word));
                ++word_column;
            }
        }
    }
}

static void native_l8_invalidate(void *opaque,
                                 const tnc155_upd7220 *gdc)
{
    (void)opaque;
    (void)gdc;
    s_native_full_redraw = 1u;
}

static void native_l8_word_written(void *opaque,
                                   const tnc155_upd7220 *gdc,
                                   uint32_t word, uint16_t value)
{
    unsigned active_height;
    unsigned line_height;
    unsigned effective_pitch;
    unsigned native_y0;
    unsigned base_y = 0u;
    unsigned area;

    (void)opaque;
    if (s_native_full_redraw != 0u || !gdc->display_enabled)
        return;

    active_height = tnc155_video_active_height(s_machine);
    if (active_height > TNC_NATIVE_HEIGHT)
        active_height = TNC_NATIVE_HEIGHT;
    native_y0 = (TNC_NATIVE_HEIGHT - active_height) / 2u;
    line_height = gdc->lines_per_character != 0u ?
                  gdc->lines_per_character : 1u;
    effective_pitch = gdc->pitch != 0u ? gdc->pitch : 1u;
    word &= 0x7fffu;

    for (area = 0u; area < 4u && base_y < active_height; ++area) {
        tnc_l8_partition part = decode_partition(gdc, area);
        unsigned length = part.length;
        unsigned pitch;
        uint32_t delta;
        unsigned local;
        unsigned column;

        if (length == 0u && part.graphics)
            length = 0x400u;
        if (length == 0u)
            continue;
        if (length > active_height - base_y)
            length = active_height - base_y;

        pitch = effective_pitch;
        if (part.graphics && gdc->display_mode == TNC155_GDC_MODE_MIXED)
            pitch >>= 1;
        if (pitch == 0u)
            pitch = 1u;

        delta = (word - (part.start & 0x7fffu)) & 0x7fffu;
        local = (unsigned)(delta / pitch);
        column = (unsigned)(delta % pitch);

        if (part.graphics) {
            if (local < length && column < 32u) {
                uint8_t *row = s_native_frame +
                    (size_t)(native_y0 + base_y + local) * TNC_NATIVE_WIDTH;
                render_graphics_word(row, column, value);
            }
        } else {
            unsigned cell_y = local * line_height;
            if (column < 32u && cell_y < length) {
                unsigned cell_height = line_height;
                if (cell_height > length - cell_y)
                    cell_height = length - cell_y;
                render_text_cell(value, native_y0 + base_y, cell_y,
                                 cell_height, column);
            }
        }
        base_y += length;
    }
}

static bool render_tnc_native(void)
{
    const tnc155_upd7220 *gdc = &s_machine->clp.gdc;
    unsigned active_height = tnc155_video_active_height(s_machine);
    unsigned line_height = gdc->lines_per_character != 0u ?
                           gdc->lines_per_character : 1u;
    unsigned effective_pitch = gdc->pitch != 0u ? gdc->pitch : 1u;
    unsigned native_y0;
    unsigned base_y = 0u;
    unsigned area;

    memset(s_native_frame, TNC_L8_OFF_NORMAL, TNC_NATIVE_BYTES);

    if (!gdc->display_enabled)
        goto done;
    if (active_height > TNC_NATIVE_HEIGHT)
        active_height = TNC_NATIVE_HEIGHT;

    native_y0 = (TNC_NATIVE_HEIGHT - active_height) / 2u;

    for (area = 0u; area < 4u && base_y < active_height; ++area) {
        tnc_l8_partition part = decode_partition(gdc, area);
        unsigned length = part.length;

        if (length == 0u && part.graphics)
            length = 0x400u;
        if (length == 0u)
            continue;
        if (length > active_height - base_y)
            length = active_height - base_y;
        part.length = (uint16_t)length;

        if (part.graphics)
            render_graphics_partition(gdc, &part, native_y0 + base_y,
                                      effective_pitch);
        else
            render_text_partition(gdc, &part, native_y0 + base_y,
                                  line_height, effective_pitch);
        base_y += length;
    }

done:
    s_native_full_redraw = 0u;
    return true;
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

/* In STM32 firmware the TMS9995 internal decrementer is driven from the real
   1 kHz Cortex SysTick timebase, not from guest instruction cycle accounting.
   At a 12 MHz TMS input clock the decrementer advances at 12 MHz / 16 =
   750 kHz, exactly 750 ticks per millisecond.  Multiple expiries collapse
   naturally into the level-3 pending latch, so a whole elapsed interval can
   be applied in O(1) time. */
static void advance_decrementer_hw_ms(tms9995 *cpu, uint32_t elapsed_ms)
{
    uint64_t ticks;
    uint32_t current;
    uint32_t reload;
    uint32_t remainder;

    if (cpu == NULL || elapsed_ms == 0u ||
        (cpu->flags & 0x0002u) == 0u ||
        (cpu->flags & 0x0001u) != 0u ||
        cpu->decrementer_value == 0u)
        return;

    ticks = (uint64_t)elapsed_ms * TNC155_DEC_TICKS_PER_MS;
    current = cpu->decrementer_value;
    if (ticks < current) {
        cpu->decrementer_value = (uint16_t)(current - (uint32_t)ticks);
        return;
    }

    ticks -= current;
    cpu->pending_interrupts |= TNC155_PENDING_LEVEL3;
    reload = cpu->decrementer_start;
    if (reload == 0u) {
        cpu->decrementer_value = 0u;
        return;
    }

    remainder = (uint32_t)(ticks % reload);
    cpu->decrementer_value = (uint16_t)(remainder == 0u ?
                                        reload : reload - remainder);
}

static uint32_t take_pending_hw_ms(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t elapsed;

    __disable_irq();
    elapsed = s_pending_hw_ms;
    s_pending_hw_ms = 0u;
    if (primask == 0u)
        __enable_irq();
    return elapsed;
}

static void service_hardware_time(void)
{
    uint32_t elapsed = take_pending_hw_ms();
    uint32_t i;

    if (elapsed == 0u)
        return;

    /* Board wiring, emergency monoflops and other millisecond-scale physical
       effects keep their exact real-time source.  This loop normally runs
       once; it only catches up if a long render/USB operation delayed the
       foreground task. */
    for (i = 0u; i < elapsed; ++i)
        tnc155_machine_service_1ms(s_machine);

    advance_decrementer_hw_ms(&s_machine->main.cpu, elapsed);
    advance_decrementer_hw_ms(&s_machine->clp.cpu, elapsed);
}

static void run_machine_slice(void)
{
    uint32_t start_core_cycles = DWT->CYCCNT;
    unsigned quanta;

    /* Run flat out in complete 8-instruction CPU quanta.  The machine runner
       now keeps MAIN or CLP selected for the whole quantum and handles Q67
       HOLD/idle donation internally at instruction boundaries, removing the
       per-instruction scheduler re-entry from this foreground hot loop. */
    for (quanta = 0u;
         quanta < (TNC155_STEP_SLICE / TNC155_CPU_QUANTUM);
         ++quanta) {
        tms9995_step_result result = tnc155_machine_run_quantum(s_machine);
        if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE) {
            g_tnc155_faulted = 1u;
            break;
        }
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
    bool dma_ok;

    if (s_swap_pending != 0u)
        return false;

    start_core_cycles = DWT->CYCCNT;
    back_fb = (uint8_t)(s_front_fb ^ 1u);
    dst = (uint8_t *)(uintptr_t)framebuffer_address(back_fb);

    if (redraw_tnc) {
        if (s_native_full_redraw != 0u && !render_tnc_native()) {
            g_tnc155_faulted = 1u;
            return false;
        }
        /* Normal text/graphics writes have already updated the AXI L8 image
           incrementally.  Publish its dirty cache lines to DMA2D once per
           presented frame; mapping/mode changes take the full rebuild above. */
        SCB_CleanDCache_by_Addr((uint32_t *)(void *)s_native_frame,
                                (int32_t)TNC_NATIVE_BYTES);
        __DSB();
        dma_ok = dma2d_copy_tnc(TNC_NATIVE_ADDRESS,
                                framebuffer_tnc_address(back_fb), false);
    } else {
        dma_ok = dma2d_copy_tnc(framebuffer_tnc_address(s_front_fb),
                                framebuffer_tnc_address(back_fb), true);
    }
    if (!dma_ok) {
        g_tnc155_faulted = 1u;
        return false;
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
    uint32_t now;

    s_hw_time_active = 0u;
    s_pending_hw_ms = 0u;
    g_tnc155_faulted = 0u;
    g_tnc155_last_slice_core_cycles = 0u;
    g_tnc155_max_slice_core_cycles = 0u;
    g_tnc155_last_frame_core_cycles = 0u;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if (!init_raster_luts())
        return false;
    if (!configure_dma2d_raw_l8())
        return false;

    load_rgb332_clut();
    clear_framebuffer(FRAMEBUFFER0_ADDRESS);
    clear_framebuffer(FRAMEBUFFER1_ADDRESS);
    s_front_fb = 0u;
    s_pending_fb = 0u;
    s_swap_pending = 0u;

    if (!tnc155_machine_init(s_machine))
        return false;
    s_native_full_redraw = 1u;
    tnc155_upd7220_set_native_video_callbacks(
        &s_machine->clp.gdc, native_l8_word_written,
        native_l8_invalidate, NULL);

    if (tnc155_default_user_ram_size != sizeof(s_machine->main.user_ram))
        return false;
    memcpy(s_machine->main.user_ram, tnc155_default_user_ram,
           sizeof(s_machine->main.user_ram));
    s_machine->main.user_ram_loaded = true;
    s_machine->main.user_ram_dirty = false;

    tnc155_machine_set_control_voltage_enabled(s_machine, true);

    tnc155_serial_keyboard_init(&s_keyboard);
    TNC155_USB_CDC_Init(&s_keyboard, &s_machine->main.keyboard);

    now = HAL_GetTick();
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

    /* Start physical-time accounting only after all emulated state and video
       initialization is complete.  SysTick from this point is the timing
       authority for watchdogs and decrementers. */
    s_pending_hw_ms = 0u;
    s_hw_time_active = 1u;
    return true;
}

void TNC155_Firmware_Task(void)
{
    uint32_t now;
    bool dirty;
    bool video_due;
    bool debug_due;
    bool redraw_tnc;

    service_hardware_time();
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

void TNC155_Firmware_SysTickISR(void)
{
    /* Keep the ISR constant-time and independent of SDRAM/emulator state.
       Foreground consumes this hardware-derived elapsed time in batches. */
    if (s_hw_time_active != 0u && s_pending_hw_ms != UINT32_MAX)
        ++s_pending_hw_ms;
}

void TNC155_Firmware_LTDCReloadComplete(void)
{
    if (s_swap_pending != 0u) {
        s_front_fb = s_pending_fb;
        s_swap_pending = 0u;
    }
}

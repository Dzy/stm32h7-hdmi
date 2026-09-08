#include "tnc155_firmware.h"

#include "ltdc.h"
#include "main.h"
#include "tnc155/machine.h"
#include "tnc155/serial_keyboard.h"
#include "tnc155/video.h"
#include "tnc155_default_user_ram.h"
#include "tnc155_usb_cdc.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TNC155_STAGE_ADDRESS       (SDRAM_BASE_ADDRESS + (8U * 1024U * 1024U))
#define TNC155_MACHINE_ADDRESS     (SDRAM_BASE_ADDRESS + (10U * 1024U * 1024U))
#define TNC155_FRAME_PERIOD_MS     20U
#define TNC155_STEP_SLICE          256U
#define TNC155_CATCHUP_LIMIT_CYCLES (TNC155_MAIN_CPU_CLOCK_HZ / 10U)

#define HDMI_WIDTH  1920U
#define HDMI_HEIGHT 1080U

#define DEBUG_X       12U
#define DEBUG_Y       12U
#define DEBUG_W       620U
#define DEBUG_H       164U
#define DEBUG_SCALE   2U
#define DEBUG_FG      0xffU
#define DEBUG_BG      0x00U

_Static_assert((TNC155_STAGE_ADDRESS +
                TNC155_VIDEO_WIDTH * TNC155_VIDEO_HEIGHT * sizeof(uint32_t)) <=
               TNC155_MACHINE_ADDRESS,
               "TNC155 ARGB staging buffer overlaps machine state");
_Static_assert((TNC155_MACHINE_ADDRESS + sizeof(tnc155_machine)) <=
               (SDRAM_BASE_ADDRESS + SDRAM_SIZE_BYTES),
               "TNC155 machine state does not fit external SDRAM");
_Static_assert(LTDC_VID_FORMAT == 8U,
               "TNC155 firmware currently targets the 1920x1080p60 mode");

static tnc155_machine *const s_machine =
    (tnc155_machine *)(uintptr_t)TNC155_MACHINE_ADDRESS;
static uint32_t *const s_stage =
    (uint32_t *)(uintptr_t)TNC155_STAGE_ADDRESS;
static tnc155_serial_keyboard s_keyboard;
static uint32_t s_epoch_ms;
static uint64_t s_epoch_cycles;
static uint32_t s_next_frame_ms;
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
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, /* A */
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, /* B */
    {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f}, /* C */
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}, /* D */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, /* E */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, /* F */
    {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f}, /* G */
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, /* H */
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0c}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, /* L */
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, /* O */
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, /* P */
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, /* Q */
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, /* R */
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, /* S */
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a}, /* W */
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}, /* X */
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, /* Y */
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}  /* Z */
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

static uint8_t rgb332_from_argb(uint32_t argb)
{
    uint8_t r = (uint8_t)(argb >> 16);
    uint8_t g = (uint8_t)(argb >> 8);
    uint8_t b = (uint8_t)argb;
    return (uint8_t)((r & 0xe0u) | ((g >> 3) & 0x1cu) | (b >> 6));
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

static void render_frame(void)
{
    uint32_t start_core_cycles;
    uint8_t back_fb;
    uint8_t *dst;
    unsigned active_height;
    unsigned src_y;
    unsigned dst_y0;
    unsigned x;
    unsigned y;

    if (s_swap_pending != 0u)
        return;

    start_core_cycles = DWT->CYCCNT;
    back_fb = (uint8_t)(s_front_fb ^ 1u);
    dst = (uint8_t *)(uintptr_t)framebuffer_address(back_fb);
    active_height = tnc155_video_active_height(s_machine);
    if (active_height > TNC155_VIDEO_HEIGHT)
        active_height = TNC155_VIDEO_HEIGHT;

    tnc155_video_render(s_machine, s_stage, TNC155_VIDEO_WIDTH, false);

    /* Clear the complete maximum TNC rectangle in the back buffer first so
       transitions between 468- and 490-line modes leave no stale scanlines. */
    for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y) {
        unsigned dy = (HDMI_HEIGHT - TNC155_VIDEO_HEIGHT) / 2u + y;
        memset(dst + (size_t)dy * HDMI_WIDTH +
               (HDMI_WIDTH - TNC155_VIDEO_WIDTH) / 2u,
               0, TNC155_VIDEO_WIDTH);
    }

    dst_y0 = (HDMI_HEIGHT - active_height) / 2u;
    src_y = 0u;
    for (y = 0u; y < active_height; ++y, ++src_y) {
        uint8_t *row = dst + (size_t)(dst_y0 + y) * HDMI_WIDTH +
                       (HDMI_WIDTH - TNC155_VIDEO_WIDTH) / 2u;
        const uint32_t *src = s_stage +
                              (size_t)src_y * TNC155_VIDEO_WIDTH;
        for (x = 0u; x < TNC155_VIDEO_WIDTH; ++x)
            row[x] = rgb332_from_argb(src[x]);
    }

    draw_debug_overlay(dst);

    __DSB();
    if (HAL_LTDC_SetAddress_NoReload(&hltdc, framebuffer_address(back_fb), 0u)
            != HAL_OK) {
        g_tnc155_faulted = 1u;
        return;
    }

    /* Arm software state before requesting the VBlank reload.  The reload
       interrupt may fire immediately after the request; setting these first
       prevents the callback from observing an unarmed swap and losing it. */
    s_pending_fb = back_fb;
    s_swap_pending = 1u;
    if (HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        s_swap_pending = 0u;
        g_tnc155_faulted = 1u;
        return;
    }

    g_tnc155_last_frame_core_cycles = DWT->CYCCNT - start_core_cycles;
}

bool TNC155_Firmware_Init(void)
{
    uint64_t slowest;

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

    /* Standalone STM32 bring-up has no separate machine-side F1 input yet.
       Keep the same physical model as the SDL frontend but latch the external
       control-voltage return on automatically so the TNC can complete boot. */
    tnc155_machine_set_control_voltage_enabled(s_machine, true);

    tnc155_serial_keyboard_init(&s_keyboard);
    TNC155_USB_CDC_Init(&s_keyboard, &s_machine->main.keyboard);

    slowest = slowest_machine_cycles();
    s_epoch_ms = HAL_GetTick();
    s_epoch_cycles = slowest;
    s_next_frame_ms = s_epoch_ms;

    render_frame();
    return true;
}

void TNC155_Firmware_Task(void)
{
    uint32_t now;

    TNC155_USB_CDC_Task();

    if (g_tnc155_faulted == 0u)
        run_machine_slice();

    /* Keep rendering the overlay even after an emulation fault.  A faulted
       machine then freezes its PC/cycle counters while TICK continues, which
       makes the failure mode visible without a debugger attached. */
    now = HAL_GetTick();
    if ((int32_t)(now - s_next_frame_ms) >= 0) {
        s_next_frame_ms += TNC155_FRAME_PERIOD_MS;
        if ((int32_t)(now - s_next_frame_ms) >= 0)
            s_next_frame_ms = now + TNC155_FRAME_PERIOD_MS;
        render_frame();
    }
}

void TNC155_Firmware_LTDCReloadComplete(void)
{
    if (s_swap_pending != 0u) {
        s_front_fb = s_pending_fb;
        s_swap_pending = 0u;
    }
}

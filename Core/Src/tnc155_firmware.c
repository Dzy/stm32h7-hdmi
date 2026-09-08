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

static uint64_t slowest_machine_cycles(void)
{
    return s_machine->main.cpu.cycles < s_machine->clp.cpu.cycles ?
           s_machine->main.cpu.cycles : s_machine->clp.cpu.cycles;
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
    if (g_tnc155_faulted != 0u)
        return;

    run_machine_slice();

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

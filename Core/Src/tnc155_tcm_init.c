#include "tnc155_firmware.h"

#include "ltdc.h"
#include "main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Linker symbols emitted by tools/make_tnc155_linker.py. */
extern uint8_t __tnc155_itcm_load__[];
extern uint8_t __tnc155_itcm_start__[];
extern uint8_t __tnc155_itcm_end__[];
extern uint8_t __tnc155_dtcm_load__[];
extern uint8_t __tnc155_dtcm_start__[];
extern uint8_t __tnc155_dtcm_end__[];
extern uint8_t __tnc155_dtcm_bss_start__[];
extern uint8_t __tnc155_dtcm_bss_end__[];

/* tnc155_firmware.c is compiled with Init renamed to this inner symbol. */
bool TNC155_Firmware_Core_Init(void);

static void copy_region(uint8_t *dst, const uint8_t *src, uint8_t *end)
{
    size_t bytes = (size_t)(end - dst);
    if (bytes != 0u)
        memcpy(dst, src, bytes);
}

static void zero_region(uint8_t *start, uint8_t *end)
{
    size_t bytes = (size_t)(end - start);
    if (bytes != 0u)
        memset(start, 0, bytes);
}

static void TNC155_TCM_Init(void)
{
    /* Do not depend on reset-state implementation details. */
    SCB->ITCMCR |= SCB_ITCMCR_EN_Msk;
    SCB->DTCMCR |= SCB_DTCMCR_EN_Msk;
    __DSB();
    __ISB();

    copy_region(__tnc155_itcm_start__, __tnc155_itcm_load__,
                __tnc155_itcm_end__);
    copy_region(__tnc155_dtcm_start__, __tnc155_dtcm_load__,
                __tnc155_dtcm_end__);
    zero_region(__tnc155_dtcm_bss_start__, __tnc155_dtcm_bss_end__);

    /* ITCM is tightly coupled rather than cacheable, but instruction fetches
       must not cross the copy until all stores are globally complete. */
    __DSB();
    __ISB();
}

static void TNC155_ApplyAmberCLUT(void)
{
    uint32_t clut[256];
    uint32_t i;

    /* Core_Init has already configured and enabled the L8 CLUT before it
       schedules the first vertical-blanking framebuffer reload.  Therefore
       update only the CLUT contents here: HAL_LTDC_EnableCLUT() performs an
       immediate LTDC reload and can cancel that pending VBlank reload, leaving
       the firmware's s_swap_pending flag stuck forever. */
    for (i = 0u; i < 256u; ++i) {
        uint32_t r3 = (i >> 5) & 7u;
        uint32_t g3 = (i >> 2) & 7u;
        uint32_t b2 = i & 3u;
        uint32_t r = (r3 * 255u + 3u) / 7u;
        uint32_t g = (g3 * 255u + 3u) / 7u;
        uint32_t b = (b2 * 255u + 1u) / 3u;
        clut[i] = (r << 16) | (g << 8) | b;
    }

    /* The real monitor has two ON intensities, not a four-level phosphor.
       VIDEO=0 is black regardless of the brightness attribute.  VIDEO=1 is
       either normal amber or the brighter amber level. */
    clut[0x00u] = 0x000000u; /* brightness=0, VIDEO=0: black */
    clut[0x04u] = 0x000000u; /* brightness=1, VIDEO=0: black */
    clut[0xdfu] = 0xd88900u; /* brightness=0, VIDEO=1: normal amber */
    clut[0x71u] = 0xffc52au; /* brightness=1, VIDEO=1: bright amber */
    clut[0xffu] = 0xffffffu; /* debug text remains white */

    /* ConfigCLUT only writes CLUTWR entries and does not touch LTDC->SRCR.
       The CLUT is already enabled by Core_Init, so no reload is required. */
    if (HAL_LTDC_ConfigCLUT(&hltdc, clut, 256u, 0u) != HAL_OK)
        Error_Handler();
    HAL_LTDC_DisableDither(&hltdc);
}

bool TNC155_Firmware_Init(void)
{
    TNC155_TCM_Init();
    if (!TNC155_Firmware_Core_Init())
        return false;
    TNC155_ApplyAmberCLUT();
    return true;
}

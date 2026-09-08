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

    /* Preserve ordinary RGB332 for the debug overlay and any diagnostic
       colours.  Only the four indices used by the emulated TNC phosphor are
       replaced with amber monitor levels. */
    for (i = 0u; i < 256u; ++i) {
        uint32_t r3 = (i >> 5) & 7u;
        uint32_t g3 = (i >> 2) & 7u;
        uint32_t b2 = i & 3u;
        uint32_t r = (r3 * 255u + 3u) / 7u;
        uint32_t g = (g3 * 255u + 3u) / 7u;
        uint32_t b = (b2 * 255u + 1u) / 3u;
        clut[i] = (r << 16) | (g << 8) | b;
    }

    /* Indices are defined by the direct L8 TNC renderer. */
    clut[0x00u] = 0x000000u; /* black */
    clut[0x04u] = 0x2a1800u; /* dark amber pedestal */
    clut[0x71u] = 0x805000u; /* dim amber */
    clut[0xdfu] = 0xffb000u; /* bright amber */
    clut[0xffu] = 0xffffffu; /* debug text remains white */

    if (HAL_LTDC_ConfigCLUT(&hltdc, clut, 256u, 0u) != HAL_OK ||
        HAL_LTDC_EnableCLUT(&hltdc, 0u) != HAL_OK)
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

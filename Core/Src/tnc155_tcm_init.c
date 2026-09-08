#include "tnc155_firmware.h"

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

bool TNC155_Firmware_Init(void)
{
    TNC155_TCM_Init();
    return TNC155_Firmware_Core_Init();
}

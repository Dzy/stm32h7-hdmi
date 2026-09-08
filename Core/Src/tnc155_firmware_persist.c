#include "tnc155_firmware.h"

#include "main.h"
#include "tnc155/machine.h"
#include "tnc155_default_user_ram.h"
#include "tnc155_nvram_flash.h"

#include <stdint.h>

#define TNC155_MACHINE_ADDRESS (SDRAM_BASE_ADDRESS + (10U * 1024U * 1024U))

static tnc155_machine *const s_persist_machine =
    (tnc155_machine *)(uintptr_t)TNC155_MACHINE_ADDRESS;
static uint32_t s_persistence_pause_ms;

/* tnc155_firmware.c is compiled with symbol renames for Init/Task. */
bool TNC155_Firmware_Core_Init(void);
void TNC155_Firmware_Core_Task(void);

/* tnc155_firmware.c is also compiled so all of its HAL_GetTick() calls resolve
   here.  Persistence uses the real HAL_GetTick(), while the emulation/video
   pacing excludes time spent synchronously erasing/programming Bank 2. */
uint32_t TNC155_PacedTick(void)
{
    return HAL_GetTick() - s_persistence_pause_ms;
}

bool TNC155_Firmware_Init(void)
{
    uint32_t flash_start;

    s_persistence_pause_ms = 0u;
    if (!TNC155_Firmware_Core_Init())
        return false;

    flash_start = HAL_GetTick();
    if (!TNC155_NVRAM_Init(&s_persist_machine->main,
                           tnc155_default_user_ram,
                           tnc155_default_user_ram_size))
        return false;
    s_persistence_pause_ms += HAL_GetTick() - flash_start;
    return true;
}

void TNC155_Firmware_Task(void)
{
    uint32_t stall_ms;

    TNC155_Firmware_Core_Task();
    stall_ms = TNC155_NVRAM_Task(&s_persist_machine->main);
    s_persistence_pause_ms += stall_ms;
}

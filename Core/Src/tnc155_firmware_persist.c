#include "tnc155_firmware.h"

#include "main.h"
#include "tnc155/machine.h"
#include "tnc155_nvram_flash.h"

#include <stdint.h>

#define TNC155_MACHINE_ADDRESS (SDRAM_BASE_ADDRESS + (10U * 1024U * 1024U))

static tnc155_machine *const s_persist_machine =
    (tnc155_machine *)(uintptr_t)TNC155_MACHINE_ADDRESS;
static uint32_t s_persistence_pause_ms;

bool TNC155_Firmware_Core_Init(void);
void TNC155_Firmware_Core_Task(void);

uint32_t TNC155_PacedTick(void)
{
    return HAL_GetTick() - s_persistence_pause_ms;
}

bool TNC155_Firmware_Init(void)
{
    s_persistence_pause_ms = 0u;

    /* Core_Init creates the machine and board devices.  Before the first
       emulated instruction is ever executed, replace User RAM from the
       flash-resident NVRAM slot selected by CRC/sequence. */
    if (!TNC155_Firmware_Core_Init())
        return false;
    if (!TNC155_NVRAM_Init(&s_persist_machine->main))
        return false;
    return true;
}

void TNC155_Firmware_Task(void)
{
    uint32_t stall_ms;

    TNC155_Firmware_Core_Task();
    stall_ms = TNC155_NVRAM_Task(&s_persist_machine->main);
    s_persistence_pause_ms += stall_ms;
}

#ifndef TNC155_NVRAM_FLASH_H
#define TNC155_NVRAM_FLASH_H

#include "tnc155/mainboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool TNC155_NVRAM_Flash_Load(uint8_t *dst, size_t size,
                             uint32_t *sequence_out);
bool TNC155_NVRAM_Flash_Save(const uint8_t *src, size_t size,
                             uint32_t *sequence_inout);
uint32_t TNC155_NVRAM_Flash_LastError(void);

/* High-level service used by the standalone STM32 firmware.  Init loads the
   newest valid A/B flash slot or falls back to the embedded image.  Task
   observes mainboard->user_ram_dirty and commits after a quiet interval. */
bool TNC155_NVRAM_Init(tnc155_mainboard *board,
                       const uint8_t *fallback, size_t fallback_size);
void TNC155_NVRAM_Task(tnc155_mainboard *board);
bool TNC155_NVRAM_LoadedFromFlash(void);
uint32_t TNC155_NVRAM_Sequence(void);
uint32_t TNC155_NVRAM_SaveCount(void);
uint32_t TNC155_NVRAM_SaveErrorCount(void);

#endif

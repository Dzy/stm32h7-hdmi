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

/* Boot succeeds only when a valid flash-resident User RAM slot exists. */
bool TNC155_NVRAM_Init(tnc155_mainboard *board);
/* Returns milliseconds spent in a synchronous flash transaction on this call. */
uint32_t TNC155_NVRAM_Task(tnc155_mainboard *board);
bool TNC155_NVRAM_LoadedFromFlash(void);
uint32_t TNC155_NVRAM_Sequence(void);
uint32_t TNC155_NVRAM_SaveCount(void);
uint32_t TNC155_NVRAM_SaveErrorCount(void);

#endif

#ifndef TNC155_NVRAM_FLASH_H
#define TNC155_NVRAM_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool TNC155_NVRAM_Flash_Load(uint8_t *dst, size_t size,
                             uint32_t *sequence_out);
bool TNC155_NVRAM_Flash_Save(const uint8_t *src, size_t size,
                             uint32_t *sequence_inout);
uint32_t TNC155_NVRAM_Flash_LastError(void);

#endif

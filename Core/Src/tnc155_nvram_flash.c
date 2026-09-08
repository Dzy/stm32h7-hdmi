#include "tnc155_nvram_flash.h"

#include "main.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_flash_ex.h"

#include <string.h>

#define TNC155_NVRAM_SLOT_SIZE      0x00020000u
#define TNC155_NVRAM_SLOT_A_ADDRESS 0x081C0000u
#define TNC155_NVRAM_SLOT_B_ADDRESS 0x081E0000u
#define TNC155_NVRAM_SLOT_A_SECTOR  FLASH_SECTOR_6
#define TNC155_NVRAM_SLOT_B_SECTOR  FLASH_SECTOR_7
#define TNC155_NVRAM_MAGIC          0x544E4331u
#define TNC155_NVRAM_VERSION        1u
#define TNC155_FLASHWORD_SIZE       32u
#define TNC155_NVRAM_SAVE_DELAY_MS  5000u
#define TNC155_NVRAM_RETRY_MS       10000u

typedef struct tnc155_nvram_header {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t length;
    uint32_t crc32;
    uint32_t reserved[3];
} tnc155_nvram_header;

_Static_assert(sizeof(tnc155_nvram_header) == TNC155_FLASHWORD_SIZE,
               "NVRAM header must occupy one H7 flash word");

static uint32_t s_last_error;
static uint32_t s_sequence;
static uint32_t s_persisted_crc;
static uint32_t s_save_due_ms;
static uint32_t s_save_count;
static uint32_t s_save_error_count;
static bool s_loaded_from_flash;
static bool s_save_pending;

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0u; i < size; ++i) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^
                  (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static const tnc155_nvram_header *slot_header(uint32_t address)
{
    return (const tnc155_nvram_header *)(uintptr_t)address;
}

static const uint8_t *slot_payload(uint32_t address)
{
    return (const uint8_t *)(uintptr_t)(address + sizeof(tnc155_nvram_header));
}

static bool slot_valid(uint32_t address, size_t size,
                       uint32_t *sequence_out)
{
    const tnc155_nvram_header *header = slot_header(address);
    if (size == 0u ||
        size > TNC155_NVRAM_SLOT_SIZE - sizeof(tnc155_nvram_header) ||
        header->magic != TNC155_NVRAM_MAGIC ||
        header->version != TNC155_NVRAM_VERSION ||
        header->length != size)
        return false;
    if (crc32_bytes(slot_payload(address), size) != header->crc32)
        return false;
    if (sequence_out != NULL)
        *sequence_out = header->sequence;
    return true;
}

static bool sequence_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

bool TNC155_NVRAM_Flash_Load(uint8_t *dst, size_t size,
                             uint32_t *sequence_out)
{
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    bool valid_a;
    bool valid_b;
    uint32_t address;
    uint32_t sequence;

    if (dst == NULL || size == 0u ||
        size > TNC155_NVRAM_SLOT_SIZE - sizeof(tnc155_nvram_header))
        return false;

    valid_a = slot_valid(TNC155_NVRAM_SLOT_A_ADDRESS, size, &seq_a);
    valid_b = slot_valid(TNC155_NVRAM_SLOT_B_ADDRESS, size, &seq_b);
    if (!valid_a && !valid_b)
        return false;

    if (valid_a && (!valid_b || sequence_newer(seq_a, seq_b))) {
        address = TNC155_NVRAM_SLOT_A_ADDRESS;
        sequence = seq_a;
    } else {
        address = TNC155_NVRAM_SLOT_B_ADDRESS;
        sequence = seq_b;
    }

    memcpy(dst, slot_payload(address), size);
    if (sequence_out != NULL)
        *sequence_out = sequence;
    return true;
}

static bool erase_slot(uint32_t sector)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0xffffffffu;
    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_2;
    erase.Sector = sector;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_4;
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
        s_last_error = HAL_FLASH_GetError();
        return false;
    }
    return true;
}

static bool program_flashword(uint32_t address, const uint8_t bytes[32])
{
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                          (uint32_t)(uintptr_t)bytes) != HAL_OK) {
        s_last_error = HAL_FLASH_GetError();
        return false;
    }
    return true;
}

bool TNC155_NVRAM_Flash_Save(const uint8_t *src, size_t size,
                             uint32_t *sequence_inout)
{
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    bool valid_a;
    bool valid_b;
    uint32_t target_address;
    uint32_t target_sector;
    uint32_t next_sequence;
    size_t offset;
    tnc155_nvram_header header;
    uint8_t flashword[TNC155_FLASHWORD_SIZE] __attribute__((aligned(32)));
    bool ok = false;

    s_last_error = 0u;
    if (src == NULL || size == 0u ||
        size > TNC155_NVRAM_SLOT_SIZE - sizeof(tnc155_nvram_header))
        return false;

    valid_a = slot_valid(TNC155_NVRAM_SLOT_A_ADDRESS, size, &seq_a);
    valid_b = slot_valid(TNC155_NVRAM_SLOT_B_ADDRESS, size, &seq_b);

    if (valid_a && (!valid_b || sequence_newer(seq_a, seq_b))) {
        target_address = TNC155_NVRAM_SLOT_B_ADDRESS;
        target_sector = TNC155_NVRAM_SLOT_B_SECTOR;
        next_sequence = seq_a + 1u;
    } else if (valid_b) {
        target_address = TNC155_NVRAM_SLOT_A_ADDRESS;
        target_sector = TNC155_NVRAM_SLOT_A_SECTOR;
        next_sequence = seq_b + 1u;
    } else {
        target_address = TNC155_NVRAM_SLOT_A_ADDRESS;
        target_sector = TNC155_NVRAM_SLOT_A_SECTOR;
        next_sequence = sequence_inout != NULL && *sequence_inout != 0u ?
                        *sequence_inout + 1u : 1u;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        s_last_error = HAL_FLASH_GetError();
        return false;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
    if (!erase_slot(target_sector))
        goto out;

    for (offset = 0u; offset < size; offset += TNC155_FLASHWORD_SIZE) {
        size_t count = size - offset;
        if (count > TNC155_FLASHWORD_SIZE)
            count = TNC155_FLASHWORD_SIZE;
        memset(flashword, 0xff, sizeof(flashword));
        memcpy(flashword, src + offset, count);
        if (!program_flashword(target_address + sizeof(tnc155_nvram_header) +
                               (uint32_t)offset, flashword))
            goto out;
    }

    memset(&header, 0xff, sizeof(header));
    header.magic = TNC155_NVRAM_MAGIC;
    header.version = TNC155_NVRAM_VERSION;
    header.sequence = next_sequence;
    header.length = (uint32_t)size;
    header.crc32 = crc32_bytes(src, size);
    memcpy(flashword, &header, sizeof(header));
    if (!program_flashword(target_address, flashword))
        goto out;

    SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)target_address,
                                 (int32_t)TNC155_NVRAM_SLOT_SIZE);
    if (!slot_valid(target_address, size, NULL)) {
        s_last_error = 0xffffffffu;
        goto out;
    }

    if (sequence_inout != NULL)
        *sequence_inout = next_sequence;
    ok = true;

out:
    (void)HAL_FLASH_Lock();
    return ok;
}

uint32_t TNC155_NVRAM_Flash_LastError(void)
{
    return s_last_error;
}

bool TNC155_NVRAM_Init(tnc155_mainboard *board)
{
    if (board == NULL)
        return false;

    s_last_error = 0u;
    s_sequence = 0u;
    s_save_count = 0u;
    s_save_error_count = 0u;
    s_save_pending = false;
    s_loaded_from_flash = TNC155_NVRAM_Flash_Load(
        board->user_ram, sizeof(board->user_ram), &s_sequence);
    if (!s_loaded_from_flash)
        return false;

    s_persisted_crc = crc32_bytes(board->user_ram, sizeof(board->user_ram));
    board->user_ram_loaded = true;
    board->user_ram_dirty = false;
    return true;
}

uint32_t TNC155_NVRAM_Task(tnc155_mainboard *board)
{
    uint32_t now;
    uint32_t flash_start;

    if (board == NULL || !board->user_ram_loaded)
        return 0u;

    now = HAL_GetTick();
    if (board->user_ram_dirty) {
        uint32_t current_crc;
        board->user_ram_dirty = false;
        current_crc = crc32_bytes(board->user_ram, sizeof(board->user_ram));
        if (current_crc != s_persisted_crc && !s_save_pending) {
            s_save_pending = true;
            s_save_due_ms = now + TNC155_NVRAM_SAVE_DELAY_MS;
        }
    }

    if (!s_save_pending || (int32_t)(now - s_save_due_ms) < 0)
        return 0u;

    flash_start = HAL_GetTick();
    if (TNC155_NVRAM_Flash_Save(board->user_ram, sizeof(board->user_ram),
                                &s_sequence)) {
        s_persisted_crc = crc32_bytes(board->user_ram,
                                      sizeof(board->user_ram));
        ++s_save_count;
        s_save_pending = false;
    } else {
        ++s_save_error_count;
        s_save_due_ms = HAL_GetTick() + TNC155_NVRAM_RETRY_MS;
    }
    return HAL_GetTick() - flash_start;
}

bool TNC155_NVRAM_LoadedFromFlash(void)
{
    return s_loaded_from_flash;
}

uint32_t TNC155_NVRAM_Sequence(void)
{
    return s_sequence;
}

uint32_t TNC155_NVRAM_SaveCount(void)
{
    return s_save_count;
}

uint32_t TNC155_NVRAM_SaveErrorCount(void)
{
    return s_save_error_count;
}

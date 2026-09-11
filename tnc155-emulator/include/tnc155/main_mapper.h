#ifndef TNC155_MAIN_MAPPER_H
#define TNC155_MAIN_MAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * TNC 151/155 MAIN memory mapper IC21.
 *
 * IC21 is modelled only as a dynamic logical-page -> 20-bit physical-address
 * translator.  It does not know which ROM/RAM/device answers the physical
 * address; MAIN-board chip-select logic does that afterwards.
 *
 * P3 has one documented 4 KiB non-mapped boot/service window: CPU
 * >0000..>0FFF goes directly to P3 >0000..>0FFF on the standard bus and is
 * unaffected by IC21.  The firmware nevertheless writes/tests the segment-0
 * mapper register; that register value is retained but is not on the P3
 * direct-window address path.
 *
 * For logical segments 1..D, reset/default behaviour is transparent/identity:
 * before firmware changes a mapper register, logical page N translates to
 * physical page N.  There is no mapper-enabled/programmed flag.
 *
 * CPU-internal TMS9995 addresses never reach this module: the CPU core owns
 * its on-chip RAM and decrementer before an external bus access is issued.
 * Logical >E000..>FFFF has no IC21 page registers and remains on the MAIN
 * standard address bus.
 */

#define TNC155_MAIN_MAPPER_SEGMENT_COUNT 14u
#define TNC155_MAIN_MAPPER_REGISTER_BASE 0xf7c0u
#define TNC155_MAIN_MAPPER_REGISTER_LAST 0xf7cdu

#define TNC155_MAIN_MAPPED_LAST          0xdfffu
#define TNC155_MAIN_DIRECT_FIRST         0xe000u

typedef enum tnc155_main_mapper_route {
    /* P3's documented 4 KiB non-mapped window on the standard address bus. */
    TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT,

    /* CPU >1000..>DFFF translated by the current IC21 page value. */
    TNC155_MAIN_MAPPER_ROUTE_EXPANDED,

    /* CPU >E000..>FFFF bypasses IC21 and remains on the standard bus. */
    TNC155_MAIN_MAPPER_ROUTE_STANDARD_DIRECT
} tnc155_main_mapper_route;

typedef struct tnc155_main_mapper {
    uint8_t page[TNC155_MAIN_MAPPER_SEGMENT_COUNT];
} tnc155_main_mapper;

typedef struct tnc155_main_mapper_translation {
    uint16_t logical_address;
    uint8_t segment;
    tnc155_main_mapper_route route;

    /* Valid only when route == EXPANDED. */
    uint8_t mapper_code;
    uint32_t expanded_address; /* 20-bit: mapper byte:A11..A0 */
} tnc155_main_mapper_translation;

/* Hot-path helper for addresses already known to be in CPU >1000..>DFFF.
 * IC21 remapping is only an address calculation; it never copies a 4 KiB
 * page.  Keeping this helper inline lets STM32 word/opcode paths perform one
 * page-table byte load plus shifts/ORs, with no temporary translation struct. */
static inline uint32_t
tnc155_main_mapper_expand_mapped(const tnc155_main_mapper *mapper,
                                 uint16_t logical_address,
                                 uint8_t *mapper_code)
{
    unsigned segment = (unsigned)(logical_address >> 12);
    uint8_t code = mapper->page[segment];

    if (mapper_code != NULL)
        *mapper_code = code;
    return ((uint32_t)code << 12) |
           ((uint32_t)logical_address & 0x0fffu);
}

void tnc155_main_mapper_reset(tnc155_main_mapper *mapper);

bool tnc155_main_mapper_set_page(tnc155_main_mapper *mapper,
                                  unsigned segment, uint8_t mapper_code);

/* Accepts only >F7C0..>F7CD.  Segment 1..D writes become effective
 * immediately.  Segment 0 is writable/testable too, but CPU >0000..>0FFF is
 * the separate non-mapped P3 window and therefore does not use its value. */
bool tnc155_main_mapper_write_register(tnc155_main_mapper *mapper,
                                       uint16_t address, uint8_t value);

tnc155_main_mapper_translation
tnc155_main_mapper_translate(const tnc155_main_mapper *mapper,
                             uint16_t logical_address);

/* CLP has no mapper.  Its separate inter-board path exports only A5..A15. */
#define TNC155_CLP_SHARED_BASE            0xf800u
#define TNC155_CLP_SHARED_SIZE            0x0800u
#define TNC155_CLP_SHARED_ADDR_MASK       0x07ffu
#define TNC155_MAIN_Q67_CLP_OFFSET        0x1800u

bool tnc155_clp_shared_to_main_q67(uint16_t clp_address,
                                   uint16_t *main_address,
                                   uint16_t *q67_offset);

#endif

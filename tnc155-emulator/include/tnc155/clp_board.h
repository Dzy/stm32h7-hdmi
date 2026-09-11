#ifndef TNC155_CLP_BOARD_H
#define TNC155_CLP_BOARD_H

#include "tnc155/tms9995.h"
#include "tnc155/upd7220.h"
#include "tnc155/tms9902.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tnc155_analog_output {
    uint16_t dac_magnitude;
    bool dac_negative;
    bool sample[5];
    int16_t held_dac[5];
    uint64_t dac_bit_writes;
    uint64_t sample_events[5];
} tnc155_analog_output;

typedef struct tnc155_clp_board {
    tms9995 cpu;
    _Alignas(4) uint8_t fast_ram[0x2000]; /* 30.2: documented 8 KiB fast RAM */
    _Alignas(4) uint8_t ram[0x2000];      /* 30.3: documented 8 KiB RAM */
    /* Board 32 gate-array/register decode at >F100..>F7FF.  The old virtual
       axis model is deliberately gone: >F400..>F5FF are now just ordinary
       gate-array bytes until real external counter hardware is connected. */
    uint8_t gate_array_registers[0x0700];
    tnc155_analog_output analog;
    tnc155_upd7220 gdc;
    tnc155_tms9902 serial;
    /* IC31.1 graphics/visualization work DRAM, physically 2 x 64K x 4
       (64 KiB total).  P2 accesses it through a 256-byte aperture at
       >E800..>E8FF.  An 8-bit page latch selects one of 256 pages. */
    uint8_t graphics_dram[0x10000];
    uint8_t graphics_dram_page;
    uint32_t graphics_dram_page_writes;
    uint32_t graphics_dram_reads;
    uint32_t graphics_dram_writes;
    uint16_t graphics_dram_last_address;
    uint8_t graphics_dram_last_value;
    uint16_t graphics_dram_last_pc;
    bool graphics_dram_last_write;
    uint16_t graphics_dram_page_last_pc;
    uint16_t character_address_latch;
    uint8_t character_scanline;
    uint8_t shared_ram_storage[0x0800];
    uint8_t *shared_ram;              /* gated shared window >F800..>FFFF */
    size_t shared_ram_size;
    bool shared_ram_enabled;          /* CRU physical bit >1000 gates window */
    /* Set only by an actual CLP access to the enabled shared window.  The
       machine scheduler uses this transient flag to model MAIN HOLD for the
       shared-memory bus cycle, not for the whole time CRU >1000 is asserted. */
    bool shared_ram_accessed;
    uint32_t shared_ram_denied_reads;
    uint32_t shared_ram_denied_writes;
    uint32_t gdc_reads;
    uint32_t gdc_writes;
    uint16_t last_gdc_read_pc;
    uint16_t last_gdc_write_pc;
    uint32_t gdc_poll_reset;
    uint32_t gdc_poll_empty;
    uint32_t gdc_poll_command;
    uint32_t gdc_poll_parameter;
    uint16_t gdc_write_log[256];      /* low byte=value, bit 8=E001 port */
    uint16_t gdc_write_pc_log[256];
    uint16_t gdc_write_log_count;
    uint16_t cru_write_address_log[256];
    uint8_t cru_write_value_log[256];
    uint16_t cru_write_log_count;
    uint32_t handshake_reads;
    uint32_t handshake_writes;
    uint16_t last_handshake_read_pc;
    uint16_t last_handshake_write_pc;
    uint16_t last_handshake_value;
    uint32_t fatal_entries;
    uint16_t first_fatal_source_pc;
    uint16_t first_fatal_code;
    uint16_t first_fatal_r6;
    uint16_t first_fatal_r7;
    uint16_t first_fatal_r8;
    uint32_t unmapped_reads;
    uint16_t last_unmapped_address;
    uint64_t emergency_monoflop_last_trigger_cycle;
    uint32_t emergency_monoflop_triggers;
    bool emergency_monoflop_triggered;
    bool emergency_monoflop_q;
} tnc155_clp_board;

bool tnc155_clp_board_init(tnc155_clp_board *board);
void tnc155_clp_attach_shared_ram(tnc155_clp_board *board, uint8_t *memory,
                                  size_t size);
uint8_t tnc155_clp_read_byte(tnc155_clp_board *board, uint16_t address,
                             tms9995_bus_cycle cycle);
void tnc155_clp_write_byte(tnc155_clp_board *board, uint16_t address,
                           uint8_t value);

/* P1 2340201A is organized as 8 modes x 64 characters x 32 scan lines:

       A13..A11 = mode
       A10..A5  = 6-bit character code
       A4..A0   = scan line

   The raw 512 x 32 view is retained for ROM-level diagnostics only. */
uint8_t tnc155_clp_font_physical_row(unsigned glyph, unsigned row);
uint8_t tnc155_clp_font_row(unsigned mode, unsigned character, unsigned row);

void tnc155_clp_gate_trace_enable(int enabled);

#endif

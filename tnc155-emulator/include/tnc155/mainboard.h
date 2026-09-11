#ifndef TNC155_MAINBOARD_H
#define TNC155_MAINBOARD_H

#include "tnc155/tms9995.h"
#include "tnc155/plc.h"
#include "tnc155/io_backplane.h"
#include "tnc155/i8279.h"
#include "tnc155/tms9902.h"
#include "tnc155/main_mapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TNC155_MAIN_WORK_RAM_SIZE 0x2000u
#define TNC155_MAIN_USER_RAM_SIZE 0x12000u
#define TNC155_KEY_TRACE_SIZE 512u
#define TNC155_KEY_TRACE_INSTRUCTIONS 16u
#define TNC155_ENTER_TRACE_INSTRUCTIONS 128u


typedef struct tnc155_main_bus_fault {
    bool active;
    bool write;
    bool physical_valid;
    tms9995_bus_cycle cycle;
    uint16_t pc;
    uint16_t wp;
    uint16_t logical_address;
    uint8_t mapper_segment;
    uint8_t mapper_code;
    uint32_t physical_address;
    uint16_t value;
    uint8_t width;
} tnc155_main_bus_fault;

typedef struct tnc155_key_trace_entry {
    uint32_t key_number;
    uint8_t raw_code;
    uint8_t step;
    uint16_t pc;
    uint16_t opcode;
    uint16_t r0;
    uint16_t r1;
    uint16_t r2;
    uint16_t r3;
} tnc155_key_trace_entry;

typedef struct tnc155_mainboard {
    tms9995 cpu;
    tnc155_plc plc;                    /* Discrete one-bit PLC processor */
    /* Physical machine I/O lives on the machine-level common backplane.
       MAIN reaches it only through CRU; this pointer is the bus attachment. */
    tnc155_io_backplane *io_backplane;
    tnc155_i8279 keyboard;
    tnc155_tms9902 serial;
    _Alignas(4) uint8_t ram[TNC155_MAIN_WORK_RAM_SIZE];
    /* STM32 scheduler sets shared_ram_blocked while CLP holds Q67.  A MAIN
       external access to >F800..>FFFF then raises shared_ram_stall instead of
       touching RAM.  The scheduler restarts that guest instruction from its
       already-known instruction PC after CLP releases the hold. */
    bool shared_ram_blocked;
    bool shared_ram_stall;
    /* Battery-backed expanded-bus User RAM: nine physical 8 KiB devices.
       Board-level physical decoding is kept separate from IC21 translation. */
    _Alignas(4) uint8_t user_ram[TNC155_MAIN_USER_RAM_SIZE];
    bool user_ram_loaded;
    bool user_ram_dirty;
    tnc155_main_mapper mapper;         /* IC21 regs 0..D; P3 segment 0 bypasses */
    uint32_t mapper_value_writes[256];
    uint64_t mapped_reads[256];
    uint64_t mapped_writes[256];
    uint16_t first_ff_write_pc;
    uint16_t first_ff_write_address;
    uint16_t last_ff_write_pc;
    uint16_t last_ff_write_address;
    uint8_t first_ff_write_value;
    uint8_t last_ff_write_value;
    uint32_t fixed_reads[TNC155_MAIN_WORK_RAM_SIZE];
    uint32_t fixed_writes[TNC155_MAIN_WORK_RAM_SIZE];
    uint32_t plc_steps;
    bool plc_aperture_active;
    uint32_t handshake_reads;
    uint32_t handshake_writes;
    uint16_t last_handshake_read_pc;
    uint16_t last_handshake_write_pc;
    uint16_t last_handshake_value;
    uint32_t unmapped_reads;
    uint16_t last_unmapped_address;
    tnc155_main_bus_fault bus_fault;
    uint32_t fatal_entries;
    uint16_t first_fatal_source_pc;
    uint16_t first_fatal_r7;
    uint16_t last_executed_pc;
    uint64_t emergency_monoflop_last_trigger_cycle;
    uint32_t emergency_monoflop_triggers;
    bool emergency_monoflop_triggered;
    bool emergency_monoflop_q;
    uint16_t cru_write_addresses[64];
    uint32_t cru_write_high_counts[64];
    uint32_t cru_write_low_counts[64];
    uint16_t cru_write_first_pcs[64];
    uint16_t cru_write_last_pcs[64];
    uint8_t cru_write_address_count;
    /* Short CPU trace beginning with each P8279 FIFO read.  This keeps the
       physical SL/RL code visible while locating P3's key translation and
       dispatch logic; it does not alter keyboard data. */
    tnc155_key_trace_entry key_trace[TNC155_KEY_TRACE_SIZE];
    uint32_t key_trace_total;
    uint32_t key_trace_key_number;
    uint8_t key_trace_raw_code;
    uint8_t key_trace_remaining;
    uint16_t entry_error_first_write_pc;
} tnc155_mainboard;

bool tnc155_mainboard_init(tnc155_mainboard *board);
void tnc155_mainboard_attach_io_backplane(tnc155_mainboard *board,
                                          tnc155_io_backplane *backplane);
bool tnc155_mainboard_load_user_ram(tnc155_mainboard *board,
                                    const char *path);
bool tnc155_mainboard_save_user_ram(tnc155_mainboard *board,
                                    const char *path);
uint8_t tnc155_mainboard_read_byte(tnc155_mainboard *board, uint16_t address,
                                   tms9995_bus_cycle cycle);
void tnc155_mainboard_write_byte(tnc155_mainboard *board, uint16_t address,
                                 uint8_t value);

#endif

#ifndef TNC155_TMS9995_H
#define TNC155_TMS9995_H

#include <stdbool.h>
#include <stdint.h>

typedef enum tms9995_bus_cycle {
    TMS9995_BUS_DATA_READ,
    TMS9995_BUS_OPCODE_FETCH,
    TMS9995_BUS_DATA_WRITE,
    TMS9995_BUS_INTERRUPT_ACK
} tms9995_bus_cycle;

typedef uint16_t (*tms9995_read_word_fn)(void *opaque, uint16_t address,
                                         tms9995_bus_cycle cycle);
typedef uint16_t (*tms9995_read_opcode_word_fn)(void *opaque,
                                                uint16_t address);
typedef uint8_t (*tms9995_read_byte_fn)(void *opaque, uint16_t address,
                                        tms9995_bus_cycle cycle);
typedef void (*tms9995_write_word_fn)(void *opaque, uint16_t address,
                                      uint16_t value,
                                      tms9995_bus_cycle cycle);
typedef void (*tms9995_write_byte_fn)(void *opaque, uint16_t address,
                                      uint8_t value,
                                      tms9995_bus_cycle cycle);
typedef bool (*tms9995_cru_read_bit_fn)(void *opaque, uint16_t bit_address);
typedef void (*tms9995_cru_write_bit_fn)(void *opaque, uint16_t bit_address,
                                         bool value);

typedef struct tms9995_bus {
    void *opaque;
    tms9995_read_word_fn read_word;
    tms9995_read_opcode_word_fn read_opcode_word;
    tms9995_read_byte_fn read_byte;
    tms9995_write_word_fn write_word;
    tms9995_write_byte_fn write_byte;
    tms9995_cru_read_bit_fn cru_read_bit;
    tms9995_cru_write_bit_fn cru_write_bit;
} tms9995_bus;

typedef enum tms9995_step_result {
    TMS9995_STEP_OK,
    TMS9995_STEP_IDLE,
    TMS9995_STEP_ILLEGAL_OPCODE,
    TMS9995_STEP_BUS_ERROR
} tms9995_step_result;

typedef enum tms9995_interrupt_line {
    TMS9995_INTERRUPT_NMI,
    TMS9995_INTERRUPT_LEVEL1,
    TMS9995_INTERRUPT_LEVEL4
} tms9995_interrupt_line;

typedef struct tms9995 {
    tms9995_bus bus;
    uint16_t wp;
    uint16_t pc;
    uint16_t st;
    uint16_t ir;
    uint64_t cycles;
    _Alignas(4) uint8_t internal_ram[256];
    uint16_t decrementer_start;
    uint16_t decrementer_value;
    uint8_t decrementer_phase;
    uint16_t flags;
    uint8_t pending_interrupts;
    uint8_t interrupt_inhibit;
    /* Semantic line polarity: true means the physical active-low input is
       asserted.  INT1/INT4 use these levels in addition to their internal
       pulse-catching request latches. */
    bool nmi_active;
    bool int1_active;
    bool int4_active;
    bool mid_flag;
    uint32_t mid_count;
    uint16_t first_mid_pc;
    uint16_t first_mid_opcode;
    uint16_t last_mid_pc;
    uint16_t last_mid_opcode;
    bool execute_override_valid;
    bool executing_x;
    uint16_t execute_override;
    bool idle;
} tms9995;

enum {
    TMS9995_ST_LGT = 0x8000u,
    TMS9995_ST_AGT = 0x4000u,
    TMS9995_ST_EQ  = 0x2000u,
    TMS9995_ST_C   = 0x1000u,
    TMS9995_ST_OV  = 0x0800u,
    TMS9995_ST_OP  = 0x0400u,
    TMS9995_ST_X   = 0x0200u,
    TMS9995_ST_IM  = 0x000fu
};

void tms9995_init(tms9995 *cpu, const tms9995_bus *bus);
bool tms9995_reset(tms9995 *cpu);
tms9995_step_result tms9995_step(tms9995 *cpu);
void tms9995_set_interrupt_line(tms9995 *cpu, tms9995_interrupt_line line,
                                bool asserted);
void tms9995_advance_clock(tms9995 *cpu, unsigned input_clock_cycles);
void tms9995_decrementer_event(tms9995 *cpu);
uint16_t tms9995_get_register(tms9995 *cpu, unsigned reg);

#endif

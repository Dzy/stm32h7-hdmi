#ifndef TNC155_PLC_H
#define TNC155_PLC_H

#include <stdbool.h>
#include <stdint.h>

#define TNC155_PLC_BIT_COUNT 4096u
#define TNC155_PLC_MARKER_COUNT 3280u
#define TNC155_PLC_INPUT_COUNT 128u
#define TNC155_PLC_OUTPUT_COUNT 64u
#define TNC155_PLC_INPUT_BASE 0x0cd0u
#define TNC155_PLC_OUTPUT_BASE 0x0e50u

typedef enum tnc155_plc_step_result {
    TNC155_PLC_STEP_OK,
    TNC155_PLC_STEP_RESERVED_OPCODE
} tnc155_plc_step_result;

typedef struct tnc155_plc {
    uint8_t bits[TNC155_PLC_BIT_COUNT];
    uint16_t pc;
    bool logic_result;
    bool chain_active;
    uint64_t instructions;
    uint32_t reserved_opcodes;
    uint16_t last_word;
    uint16_t last_operand;
    uint8_t last_opcode;
} tnc155_plc;

void tnc155_plc_reset(tnc155_plc *plc);
bool tnc155_plc_read_bit(const tnc155_plc *plc, uint16_t address);
void tnc155_plc_write_bit(tnc155_plc *plc, uint16_t address, bool value);
bool tnc155_plc_read_marker(const tnc155_plc *plc, unsigned marker);
void tnc155_plc_write_marker(tnc155_plc *plc, unsigned marker, bool value);
bool tnc155_plc_read_input(const tnc155_plc *plc, unsigned input);
void tnc155_plc_set_input(tnc155_plc *plc, unsigned input, bool value);
bool tnc155_plc_read_output(const tnc155_plc *plc, unsigned output);
tnc155_plc_step_result tnc155_plc_step_word(tnc155_plc *plc, uint16_t word);

#endif

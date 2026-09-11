#include "tnc155/plc.h"

#include <string.h>

void tnc155_plc_reset(tnc155_plc *plc)
{
    if (plc != NULL)
        memset(plc, 0, sizeof(*plc));
}

bool tnc155_plc_read_bit(const tnc155_plc *plc, uint16_t address)
{
    return plc != NULL && plc->bits[address & 0x0fffu] != 0u;
}

void tnc155_plc_write_bit(tnc155_plc *plc, uint16_t address, bool value)
{
    if (plc != NULL)
        plc->bits[address & 0x0fffu] = value ? 1u : 0u;
}

bool tnc155_plc_read_marker(const tnc155_plc *plc, unsigned marker)
{
    return marker < TNC155_PLC_MARKER_COUNT &&
           tnc155_plc_read_bit(plc, (uint16_t)marker);
}

void tnc155_plc_write_marker(tnc155_plc *plc, unsigned marker, bool value)
{
    if (marker < TNC155_PLC_MARKER_COUNT)
        tnc155_plc_write_bit(plc, (uint16_t)marker, value);
}

bool tnc155_plc_read_input(const tnc155_plc *plc, unsigned input)
{
    return input < TNC155_PLC_INPUT_COUNT &&
           tnc155_plc_read_bit(plc,
                               (uint16_t)(TNC155_PLC_INPUT_BASE + input));
}

void tnc155_plc_set_input(tnc155_plc *plc, unsigned input, bool value)
{
    if (input < TNC155_PLC_INPUT_COUNT)
        tnc155_plc_write_bit(plc,
                             (uint16_t)(TNC155_PLC_INPUT_BASE + input), value);
}

bool tnc155_plc_read_output(const tnc155_plc *plc, unsigned output)
{
    return output < TNC155_PLC_OUTPUT_COUNT &&
           tnc155_plc_read_bit(plc,
                               (uint16_t)(TNC155_PLC_OUTPUT_BASE + output));
}

tnc155_plc_step_result tnc155_plc_step_word(tnc155_plc *plc, uint16_t word)
{
    uint8_t opcode;
    uint16_t operand;
    bool value;
    if (plc == NULL)
        return TNC155_PLC_STEP_RESERVED_OPCODE;

    opcode = (uint8_t)(word >> 12);
    operand = word & 0x0fffu;
    value = tnc155_plc_read_bit(plc, operand);
    plc->last_word = word;
    plc->last_opcode = opcode;
    plc->last_operand = operand;
    ++plc->instructions;

    switch (opcode) {
    case 0x0u: /* NOP */
    case 0xfu: /* NOP (empty erased command memory) */
        return TNC155_PLC_STEP_OK;
    case 0x1u: /* U: AND */
        plc->logic_result = (plc->chain_active ? plc->logic_result : true) && value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x2u: /* UN: AND with inverted operand */
        plc->logic_result = (plc->chain_active ? plc->logic_result : true) && !value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x3u: /* O: OR */
        plc->logic_result = (plc->chain_active ? plc->logic_result : false) || value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x4u: /* ON: OR with inverted operand */
        plc->logic_result = (plc->chain_active ? plc->logic_result : false) || !value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x5u: /* XO: exclusive OR */
        plc->logic_result = (plc->chain_active ? plc->logic_result : false) != value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x6u: /* XON: exclusive OR with inverted operand */
        plc->logic_result = (plc->chain_active ? plc->logic_result : false) != !value;
        plc->chain_active = true;
        return TNC155_PLC_STEP_OK;
    case 0x7u: /* S */
        if (plc->logic_result)
            tnc155_plc_write_bit(plc, operand, true);
        break;
    case 0x8u: /* SN */
        if (!plc->logic_result)
            tnc155_plc_write_bit(plc, operand, true);
        break;
    case 0x9u: /* R */
        if (plc->logic_result)
            tnc155_plc_write_bit(plc, operand, false);
        break;
    case 0xau: /* RN */
        if (!plc->logic_result)
            tnc155_plc_write_bit(plc, operand, false);
        break;
    case 0xbu: /* =: assignment */
        tnc155_plc_write_bit(plc, operand, plc->logic_result);
        break;
    default: /* C..E are not defined by the TNC 151/155 PLC manual. */
        ++plc->reserved_opcodes;
        return TNC155_PLC_STEP_RESERVED_OPCODE;
    }

    /* Assignment and S/SN/R/RN terminate a logic sequence.  The next U/UN
       starts with logic 1; the next O/ON/XO/XON starts with logic 0. */
    plc->chain_active = false;
    return TNC155_PLC_STEP_OK;
}

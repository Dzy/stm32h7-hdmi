#include "tnc155/io_backplane.h"

#include <string.h>

static bool input_value(const tnc155_io_backplane *backplane, unsigned input)
{
    unsigned card = input / 64u;
    unsigned channel = input % 64u;

    if (backplane == NULL || card >= TNC155_IO_BACKPLANE_CARD_COUNT)
        return false;
    if (channel == 63u)
        return backplane->card[card].overload_ok;
    return channel < TNC155_IO_BACKPLANE_INPUTS_PER_CARD &&
           backplane->card[card].inputs[channel];
}

static void output_value(tnc155_io_backplane *backplane, unsigned output,
                         bool value)
{
    unsigned card = output / 32u;
    unsigned channel = output % 32u;

    if (backplane == NULL || card >= TNC155_IO_BACKPLANE_CARD_COUNT)
        return;
#ifndef TNC155_FIRMWARE
    ++backplane->card[card].output_writes;
#endif

    /* A31/A63 are momentary overload-reset strobes.  They do not latch as
       physical output terminals. */
    if (channel == 31u) {
        if (value) {
            backplane->card[card].overload_ok = true;
#ifndef TNC155_FIRMWARE
            ++backplane->card[card].overload_resets;
#endif
        }
        return;
    }

    if (channel < TNC155_IO_BACKPLANE_OUTPUTS_PER_CARD) {
        backplane->card[card].outputs[channel] = value;
        /* The only standard hard-wired output feedback is A6 -> E20.
           Update that one signal at its actual state-change site instead of
           recomputing all pendant/machine wiring after every CRU output bit. */
        if (output == 6u)
            backplane->card[0].inputs[20] = value;
    }
}

void tnc155_io_backplane_reset(tnc155_io_backplane *backplane)
{
    unsigned card;
    if (backplane == NULL)
        return;
    memset(backplane, 0, sizeof(*backplane));
    for (card = 0; card < TNC155_IO_BACKPLANE_CARD_COUNT; ++card)
        backplane->card[card].overload_ok = true;
    /* Standard machine pendant/terminal wiring: STOP is NC and healthy,
       feed enable is present; momentary START/rapid and manual traverse are idle. */
    backplane->stop_button = false;
    backplane->feed_enable = true;
    /* The +24 V source itself is continuously present at the TNC supply
       terminals.  The machine-side control-voltage return/relay starts open
       and is latched on by the operator (F1 in the SDL frontend).  The TNC
       emergency-stop contact is a healthy-closed contact in series with the
       returned feedback path. */
    backplane->control_voltage_24v = true;
    backplane->control_voltage_enabled = false;
    backplane->emergency_stop_contact_closed = true;
    tnc155_io_backplane_update_machine_wiring(backplane);
}

bool tnc155_io_backplane_read_input_terminal(
    const tnc155_io_backplane *backplane, unsigned input)
{
    if (input >= TNC155_IO_BACKPLANE_INPUT_COUNT)
        return false;
    return input_value(backplane, input);
}

void tnc155_io_backplane_set_input_terminal(tnc155_io_backplane *backplane,
                                             unsigned input, bool value)
{
    unsigned card = input / 64u;
    unsigned channel = input % 64u;

    if (backplane == NULL || card >= TNC155_IO_BACKPLANE_CARD_COUNT ||
        channel >= TNC155_IO_BACKPLANE_INPUTS_PER_CARD)
        return;
    backplane->card[card].inputs[channel] = value;
}

bool tnc155_io_backplane_read_output_terminal(
    const tnc155_io_backplane *backplane, unsigned output)
{
    unsigned card = output / 32u;
    unsigned channel = output % 32u;

    return backplane != NULL && card < TNC155_IO_BACKPLANE_CARD_COUNT &&
           channel < TNC155_IO_BACKPLANE_OUTPUTS_PER_CARD &&
           backplane->card[card].outputs[channel];
}

void tnc155_io_backplane_set_overload(tnc155_io_backplane *backplane,
                                      unsigned card, bool overloaded)
{
    if (backplane != NULL && card < TNC155_IO_BACKPLANE_CARD_COUNT)
        backplane->card[card].overload_ok = !overloaded;
}

void tnc155_io_backplane_set_control_voltage_24v(
    tnc155_io_backplane *backplane, bool on)
{
    if (backplane != NULL)
        backplane->control_voltage_24v = on;
}

bool tnc155_io_backplane_control_voltage_24v(
    const tnc155_io_backplane *backplane)
{
    return backplane != NULL && backplane->control_voltage_24v;
}

void tnc155_io_backplane_set_control_voltage_enabled(
    tnc155_io_backplane *backplane, bool on)
{
    if (backplane != NULL)
        backplane->control_voltage_enabled = on;
}

bool tnc155_io_backplane_control_voltage_enabled(
    const tnc155_io_backplane *backplane)
{
    return backplane != NULL && backplane->control_voltage_enabled;
}

void tnc155_io_backplane_set_emergency_stop_contact_closed(
    tnc155_io_backplane *backplane, bool closed)
{
    if (backplane != NULL)
        backplane->emergency_stop_contact_closed = closed;
}

bool tnc155_io_backplane_emergency_stop_contact_closed(
    const tnc155_io_backplane *backplane)
{
    return backplane != NULL && backplane->emergency_stop_contact_closed;
}

bool tnc155_io_backplane_cru_read_bit(tnc155_io_backplane *backplane,
                                      uint16_t bit_address, bool *value)
{
    unsigned input;
    if (backplane == NULL || value == NULL ||
        bit_address < TNC155_IO_BACKPLANE_CRU_INPUT_BASE ||
        bit_address > TNC155_IO_BACKPLANE_CRU_INPUT_LAST)
        return false;

    input = (unsigned)(bit_address - TNC155_IO_BACKPLANE_CRU_INPUT_BASE);
    *value = input_value(backplane, input);
#ifndef TNC155_FIRMWARE
    ++backplane->card[input / 64u].input_reads;
    ++backplane->cru_input_reads;
    backplane->last_cru_read_address = bit_address;
#endif
    return true;
}

bool tnc155_io_backplane_cru_write_bit(tnc155_io_backplane *backplane,
                                       uint16_t bit_address, bool value)
{
    unsigned output;
    if (backplane == NULL ||
        bit_address < TNC155_IO_BACKPLANE_CRU_OUTPUT_BASE ||
        bit_address > TNC155_IO_BACKPLANE_CRU_OUTPUT_LAST)
        return false;

    output = (unsigned)(bit_address - TNC155_IO_BACKPLANE_CRU_OUTPUT_BASE);
    output_value(backplane, output, value);
#ifndef TNC155_FIRMWARE
    ++backplane->cru_output_writes;
    backplane->last_cru_write_address = bit_address;
#endif
    return true;
}

void tnc155_io_backplane_update_machine_wiring(tnc155_io_backplane *b)
{
    if (!b) return;
    /* J5 standard wiring. Active input means +24 V at the E terminal. */
    b->card[0].inputs[23] = !b->stop_button; /* E23 STOP, NC */
    b->card[0].inputs[22] = b->start_button; /* E22 START, NO */
    b->card[0].inputs[21] = b->rapid_traverse_button;
    b->card[0].inputs[19] = b->manual_traverse_switch;
    b->card[0].inputs[18] = b->feed_enable;
    /* Standard spindle-lock contactor auxiliary contact: A6 -> E20. */
    b->card[0].inputs[20] = b->card[0].outputs[6];
}
void tnc155_io_backplane_set_start_button(tnc155_io_backplane *b, bool v){if(b){b->start_button=v;tnc155_io_backplane_update_machine_wiring(b);}}
void tnc155_io_backplane_set_stop_button(tnc155_io_backplane *b, bool v){if(b){b->stop_button=v;tnc155_io_backplane_update_machine_wiring(b);}}
void tnc155_io_backplane_set_rapid_traverse_button(tnc155_io_backplane *b, bool v){if(b){b->rapid_traverse_button=v;tnc155_io_backplane_update_machine_wiring(b);}}
void tnc155_io_backplane_set_manual_traverse_switch(tnc155_io_backplane *b, bool v){if(b){b->manual_traverse_switch=v;tnc155_io_backplane_update_machine_wiring(b);}}
void tnc155_io_backplane_set_feed_enable(tnc155_io_backplane *b, bool v){if(b){b->feed_enable=v;tnc155_io_backplane_update_machine_wiring(b);}}

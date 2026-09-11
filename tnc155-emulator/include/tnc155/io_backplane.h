#ifndef TNC155_IO_BACKPLANE_H
#define TNC155_IO_BACKPLANE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * One physical I/O backplane for the TNC 155 machine interface.
 *
 * The backplane owns the physical E/A terminals.  MAIN firmware does not
 * access these arrays directly: the only CPU-facing interface is the CRU
 * read/write decoder below.
 *
 * Card 1:
 *   E0..E62   physical inputs
 *   E63       output-stage healthy / no-overload feedback
 *   A0..A30   physical outputs
 *   A31       overload-reset strobe (not a latched output)
 *
 * Card 2:
 *   E64..E126 physical inputs
 *   E127      output-stage healthy / no-overload feedback
 *   A32..A62  physical outputs
 *   A63       overload-reset strobe (not a latched output)
 *
 * ROM-proven MAIN CRU transfer windows:
 *   read  >6400..>647F -> E0..E127
 *   write >6440..>647F -> A0..A63
 *
 * Read and write decode intentionally overlap at >6440..>647F.  CRU bus
 * direction selects whether the same physical CRU number means E64..E127
 * or A0..A63.
 */

#define TNC155_IO_BACKPLANE_CARD_COUNT 2u
#define TNC155_IO_BACKPLANE_INPUTS_PER_CARD 63u
#define TNC155_IO_BACKPLANE_OUTPUTS_PER_CARD 31u
#define TNC155_IO_BACKPLANE_INPUT_COUNT 128u
#define TNC155_IO_BACKPLANE_OUTPUT_COUNT 64u

#define TNC155_IO_BACKPLANE_CRU_INPUT_BASE  0x6400u
#define TNC155_IO_BACKPLANE_CRU_INPUT_LAST  0x647fu
#define TNC155_IO_BACKPLANE_CRU_OUTPUT_BASE 0x6440u
#define TNC155_IO_BACKPLANE_CRU_OUTPUT_LAST 0x647fu

typedef struct tnc155_io_backplane_card {
    bool inputs[TNC155_IO_BACKPLANE_INPUTS_PER_CARD];
    bool outputs[TNC155_IO_BACKPLANE_OUTPUTS_PER_CARD];
    bool overload_ok;
    uint64_t input_reads;
    uint64_t output_writes;
    uint32_t overload_resets;
} tnc155_io_backplane_card;

typedef struct tnc155_io_backplane {
    tnc155_io_backplane_card card[TNC155_IO_BACKPLANE_CARD_COUNT];
    /* Raw machine-interface supply.  This represents the permanently
       available external +24 V source, upstream of the TNC emergency-stop
       contact. */
    bool control_voltage_24v;
    /* External machine control-voltage return/relay path.  The SDL frontend
       uses F1 to latch this on.  It is downstream of the TNC emergency-stop
       output, so J1/8 can still interrupt the voltage returned to E8/J5/8. */
    bool control_voltage_enabled;
    /* Functional state of the TNC emergency-stop relay contact at J1/8.
       Healthy = closed; a self-test pulse or fault opens the contact. */
    bool emergency_stop_contact_closed;
    bool start_button;
    bool stop_button;
    bool rapid_traverse_button;
    bool manual_traverse_switch;
    bool feed_enable;
    uint64_t cru_input_reads;
    uint64_t cru_output_writes;
    uint16_t last_cru_read_address;
    uint16_t last_cru_write_address;
} tnc155_io_backplane;

void tnc155_io_backplane_reset(tnc155_io_backplane *backplane);

/* External machine wiring / terminal side.  These functions model signals
 * arriving at or leaving the physical backplane; CPU firmware must use CRU. */
bool tnc155_io_backplane_read_input_terminal(
    const tnc155_io_backplane *backplane, unsigned input);
void tnc155_io_backplane_set_input_terminal(tnc155_io_backplane *backplane,
                                             unsigned input, bool value);
bool tnc155_io_backplane_read_output_terminal(
    const tnc155_io_backplane *backplane, unsigned output);
void tnc155_io_backplane_set_overload(tnc155_io_backplane *backplane,
                                      unsigned card, bool overloaded);
void tnc155_io_backplane_set_control_voltage_24v(
    tnc155_io_backplane *backplane, bool on);
bool tnc155_io_backplane_control_voltage_24v(
    const tnc155_io_backplane *backplane);
void tnc155_io_backplane_set_control_voltage_enabled(
    tnc155_io_backplane *backplane, bool on);
bool tnc155_io_backplane_control_voltage_enabled(
    const tnc155_io_backplane *backplane);
void tnc155_io_backplane_set_emergency_stop_contact_closed(
    tnc155_io_backplane *backplane, bool closed);
bool tnc155_io_backplane_emergency_stop_contact_closed(
    const tnc155_io_backplane *backplane);
void tnc155_io_backplane_set_start_button(tnc155_io_backplane *, bool);
void tnc155_io_backplane_set_stop_button(tnc155_io_backplane *, bool);
void tnc155_io_backplane_set_rapid_traverse_button(tnc155_io_backplane *, bool);
void tnc155_io_backplane_set_manual_traverse_switch(tnc155_io_backplane *, bool);
void tnc155_io_backplane_set_feed_enable(tnc155_io_backplane *, bool);
void tnc155_io_backplane_update_machine_wiring(tnc155_io_backplane *);

/* CPU-facing CRU bus interface.  Return true when the backplane decodes the
 * supplied CRU bit address.  For reads, *value receives the bus value. */
bool tnc155_io_backplane_cru_read_bit(tnc155_io_backplane *backplane,
                                      uint16_t bit_address, bool *value);
bool tnc155_io_backplane_cru_write_bit(tnc155_io_backplane *backplane,
                                       uint16_t bit_address, bool value);

#endif

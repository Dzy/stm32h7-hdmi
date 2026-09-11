#include "tnc155/io_backplane.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_io_backplane io;
    bool value = false;

    tnc155_io_backplane_reset(&io);

    /* Healthy output stages appear at E63/E127. */
    assert(tnc155_io_backplane_read_input_terminal(&io, 63u));
    assert(tnc155_io_backplane_read_input_terminal(&io, 127u));
    /* Standard terminal/pendant wiring defaults. */
    assert(tnc155_io_backplane_control_voltage_24v(&io));
    assert(!tnc155_io_backplane_control_voltage_enabled(&io));
    assert(tnc155_io_backplane_emergency_stop_contact_closed(&io));
    assert(tnc155_io_backplane_read_input_terminal(&io, 23u)); /* NC STOP */
    assert(!tnc155_io_backplane_read_input_terminal(&io, 22u)); /* START idle */
    assert(tnc155_io_backplane_read_input_terminal(&io, 18u)); /* feed enable */
    tnc155_io_backplane_set_start_button(&io, true);
    assert(tnc155_io_backplane_read_input_terminal(&io, 22u));
    tnc155_io_backplane_set_start_button(&io, false);
    tnc155_io_backplane_set_stop_button(&io, true);
    assert(!tnc155_io_backplane_read_input_terminal(&io, 23u));
    tnc155_io_backplane_set_stop_button(&io, false);

    assert(!tnc155_io_backplane_read_input_terminal(&io, 8u));

    /* External wiring drives E terminals; MAIN sees them only through CRU. */
    tnc155_io_backplane_set_input_terminal(&io, 0u, true);
    tnc155_io_backplane_set_input_terminal(&io, 8u, true);
    tnc155_io_backplane_set_input_terminal(&io, 64u, true);
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x6400u, &value) && value);
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x6408u, &value) && value);
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x6440u, &value) && value);

    /* The overlapping address is direction-decoded: write >6440 is A0, not
       E64.  Read >6440 above remains E64. */
    assert(tnc155_io_backplane_cru_write_bit(&io, 0x6440u, true));
    assert(tnc155_io_backplane_read_output_terminal(&io, 0u));
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x6440u, &value) && value);

    assert(tnc155_io_backplane_cru_write_bit(&io, 0x645fu, false));
    assert(!tnc155_io_backplane_read_output_terminal(&io, 31u));
    tnc155_io_backplane_set_overload(&io, 0u, true);
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x643fu, &value) && !value);
    assert(tnc155_io_backplane_cru_write_bit(&io, 0x645fu, true));
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x643fu, &value) && value);
    assert(io.card[0].overload_resets == 1u);

    tnc155_io_backplane_set_overload(&io, 1u, true);
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x647fu, &value) && !value);
    assert(tnc155_io_backplane_cru_write_bit(&io, 0x647fu, true));
    assert(tnc155_io_backplane_cru_read_bit(&io, 0x647fu, &value) && value);
    assert(io.card[1].overload_resets == 1u);

    assert(!tnc155_io_backplane_cru_read_bit(&io, 0x63ffu, &value));
    assert(!tnc155_io_backplane_cru_write_bit(&io, 0x643fu, true));

    tnc155_io_backplane_set_control_voltage_24v(&io, true);
    assert(tnc155_io_backplane_control_voltage_24v(&io));
    tnc155_io_backplane_set_control_voltage_enabled(&io, true);
    assert(tnc155_io_backplane_control_voltage_enabled(&io));
    tnc155_io_backplane_set_emergency_stop_contact_closed(&io, false);
    assert(!tnc155_io_backplane_emergency_stop_contact_closed(&io));
    tnc155_io_backplane_set_emergency_stop_contact_closed(&io, true);
    assert(tnc155_io_backplane_emergency_stop_contact_closed(&io));

    puts("common CRU I/O backplane: OK");
    return 0;
}

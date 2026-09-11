#include "tnc155/plc_io.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_plc_io io;
    tnc155_plc_io_reset(&io);

    /* Healthy board status occupies E63/E127; ordinary inputs remain idle. */
    assert(!tnc155_plc_io_read_input(&io, 31u));
    assert(tnc155_plc_io_read_input(&io, 63u));
    assert(tnc155_plc_io_read_input(&io, 127u));
    tnc155_plc_io_set_input(&io, 8u, true);
    assert(tnc155_plc_io_read_input(&io, 8u));

    tnc155_plc_io_write_output(&io, 0u, true);
    assert(tnc155_plc_io_read_output(&io, 0u));
    tnc155_plc_io_set_overload(&io, 0u, true);
    assert(!tnc155_plc_io_read_input(&io, 63u));
    tnc155_plc_io_write_output(&io, 31u, true);
    assert(tnc155_plc_io_read_input(&io, 63u));
    assert(io.board[0].overload_resets == 1u);

    /* A31/A63 acknowledge overload; they are not ordinary output latches. */
    assert(!tnc155_plc_io_read_output(&io, 31u));
    assert(!tnc155_plc_io_read_output(&io, 63u));
    puts("physical PLC I/O board: OK");
    return 0;
}

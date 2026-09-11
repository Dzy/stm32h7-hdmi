#include "tnc155/plc_io.h"

#include <string.h>

void tnc155_plc_io_reset(tnc155_plc_io *io)
{
    unsigned board;
    if (io == NULL)
        return;
    memset(io, 0, sizeof(*io));
    for (board = 0; board < TNC155_PLC_IO_BOARD_COUNT; ++board)
        io->board[board].overload_ok = true;
}

bool tnc155_plc_io_read_input(tnc155_plc_io *io, unsigned input)
{
    unsigned board = input / 64u;
    unsigned channel = input % 64u;
    if (io == NULL || board >= TNC155_PLC_IO_BOARD_COUNT)
        return false;
    ++io->board[board].input_reads;
    if (channel == 63u)
        return io->board[board].overload_ok;
    return channel < TNC155_PLC_IO_INPUTS_PER_BOARD &&
           io->board[board].inputs[channel];
}

void tnc155_plc_io_set_input(tnc155_plc_io *io, unsigned input, bool value)
{
    unsigned board = input / 64u;
    unsigned channel = input % 64u;
    if (io != NULL && board < TNC155_PLC_IO_BOARD_COUNT &&
        channel < TNC155_PLC_IO_INPUTS_PER_BOARD)
        io->board[board].inputs[channel] = value;
}

bool tnc155_plc_io_read_output(const tnc155_plc_io *io, unsigned output)
{
    unsigned board = output / 32u;
    unsigned channel = output % 32u;
    return io != NULL && board < TNC155_PLC_IO_BOARD_COUNT &&
           channel < TNC155_PLC_IO_OUTPUTS_PER_BOARD &&
           io->board[board].outputs[channel];
}

void tnc155_plc_io_write_output(tnc155_plc_io *io, unsigned output,
                                bool value)
{
    unsigned board = output / 32u;
    unsigned channel = output % 32u;
    if (io == NULL || board >= TNC155_PLC_IO_BOARD_COUNT)
        return;
    ++io->board[board].output_writes;
    if (channel == 31u) {
        if (value) {
            io->board[board].overload_ok = true;
            ++io->board[board].overload_resets;
        }
        return;
    }
    io->board[board].outputs[channel] = value;
}

void tnc155_plc_io_set_overload(tnc155_plc_io *io, unsigned board,
                                bool overloaded)
{
    if (io != NULL && board < TNC155_PLC_IO_BOARD_COUNT)
        io->board[board].overload_ok = !overloaded;
}

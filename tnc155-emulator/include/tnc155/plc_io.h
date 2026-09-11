#ifndef TNC155_PLC_IO_H
#define TNC155_PLC_IO_H

#include <stdbool.h>
#include <stdint.h>

#define TNC155_PLC_IO_BOARD_COUNT 2u
#define TNC155_PLC_IO_INPUTS_PER_BOARD 63u
#define TNC155_PLC_IO_OUTPUTS_PER_BOARD 31u

typedef struct tnc155_plc_io_channel {
    bool inputs[TNC155_PLC_IO_INPUTS_PER_BOARD];
    bool outputs[TNC155_PLC_IO_OUTPUTS_PER_BOARD];
    bool overload_ok;
    uint64_t input_reads;
    uint64_t output_writes;
    uint32_t overload_resets;
} tnc155_plc_io_channel;

/* Physical PL100/PL110-style I/O.  This is intentionally separate from the
   discrete PLC processor's 4096-bit operand RAM. */
typedef struct tnc155_plc_io {
    tnc155_plc_io_channel board[TNC155_PLC_IO_BOARD_COUNT];
} tnc155_plc_io;

void tnc155_plc_io_reset(tnc155_plc_io *io);
bool tnc155_plc_io_read_input(tnc155_plc_io *io, unsigned input);
void tnc155_plc_io_set_input(tnc155_plc_io *io, unsigned input, bool value);
bool tnc155_plc_io_read_output(const tnc155_plc_io *io, unsigned output);
void tnc155_plc_io_write_output(tnc155_plc_io *io, unsigned output,
                                bool value);
void tnc155_plc_io_set_overload(tnc155_plc_io *io, unsigned board,
                                bool overloaded);

#endif

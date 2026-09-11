# uPD7220 functional host FIFO — 2026-09-07

This update replaces the immediate command/parameter shortcut with an actual
16-byte uPD7220 host FIFO in the TNC155 CLP path.

## Implemented

- `>E001` command writes and `>E000` parameter/data writes are queued as tagged
  FIFO entries instead of being executed directly by the host bus write.
- FIFO storage is a proper 16-entry ring buffer with head/tail/count state.
- Status bit `FIFO EMPTY` is generated from the queue occupancy.
- Status bit `FIFO FULL` is asserted at 16 queued bytes.
- FIFO direction state is modeled; changing host direction clears the queue in
  the same architectural manner as the NEC programming model.
- Processing a RESET command clears the FIFO/command pipeline.
- The command processor consumes queued bytes in order through
  `tnc155_upd7220_service()`.
- The machine scheduler services one queued GDC host byte per scheduler step.
  This keeps command execution asynchronous with respect to the CLP bus without
  inventing an unproven GDC/pixel-clock ratio.
- Overrun, underflow, high-watermark and processed-entry counters were added for
  diagnostics.
- Headless and SDL diagnostics report FIFO occupancy/high-water/overruns.

## Deliberately not claimed

The FIFO is functional but not cycle-accurate.  The physical uPD7220/board
clock ratio has not yet been decoded, so the exact byte-consumption cadence is
not modeled.  VSYNC/HBLANK timing is unchanged.

Read-side commands such as full RDAT/DMA behavior remain separate work; this
change establishes the host FIFO mechanism used by the command/parameter path.

## Regression coverage

`test_upd7220` verifies:

- initial FIFO-empty status,
- filling all 16 entries,
- FIFO-full status,
- overrun accounting for a 17th unserviced byte,
- partial and complete servicing,
- FIFO-empty restoration,
- high-watermark and processed-entry accounting.

`test_clp_board` verifies that CLP writes to `>E001/>E000` first enter the FIFO
and take effect only after command-processor service.

All 13 CTest tests pass.  A 10,000,000-step combined-machine smoke run completes
with MAIN unmapped reads = 0, CLP unmapped reads = 0, FIFO overruns = 0 and FIFO
high-watermark = 1 on the currently reached P2 boot path.

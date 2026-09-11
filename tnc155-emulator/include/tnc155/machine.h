#ifndef TNC155_MACHINE_H
#define TNC155_MACHINE_H

#include "tnc155/clp_board.h"
#include "tnc155/mainboard.h"
#include "tnc155/io_backplane.h"

#include <stdbool.h>
#include <stdint.h>

#define TNC155_MAIN_CPU_CLOCK_HZ 12000000u
#define TNC155_CLP_CPU_CLOCK_HZ  12000000u
#define TNC155_TMS9995_CYCLE_HZ TNC155_MAIN_CPU_CLOCK_HZ
#define TNC155_CPU_QUANTUM 1u
/* Service-manual switch-off limits for the two emergency-stop monoflops:
 * position-loop calculator: min. 11 ms, max. 26 ms
 * main calculator:          min. 23 ms, max. 76 ms
 *
 * Model the earliest specified switch-off point. */
#define TNC155_CLP_EMERGENCY_MONOFLOP_MS 11u
#define TNC155_MAIN_EMERGENCY_MONOFLOP_MS 23u
#define TNC155_CLP_EMERGENCY_MONOFLOP_CYCLES \
    ((TNC155_TMS9995_CYCLE_HZ * TNC155_CLP_EMERGENCY_MONOFLOP_MS) / 1000u)
#define TNC155_MAIN_EMERGENCY_MONOFLOP_CYCLES \
    ((TNC155_TMS9995_CYCLE_HZ * TNC155_MAIN_EMERGENCY_MONOFLOP_MS) / 1000u)

typedef struct tnc155_machine {
    tnc155_io_backplane io_backplane;
    tnc155_mainboard main;
    tnc155_clp_board clp;
    uint64_t main_instructions;
    uint64_t clp_instructions;
    bool main_hold_asserted;
    uint64_t main_hold_steps;
    /* STM32 firmware runs the two TMS9995 cores in fixed eight-instruction
       quanta.  CLP CRU >1000 is an immediate MAIN HOLD request.  While it is
       asserted, MAIN executes no instruction slots and complete quanta remain
       with CLP.  If CLP clears CRU >1000 during a quantum, CLP finishes that
       quantum and MAIN receives the following fresh eight-slot quantum. */
    uint8_t firmware_next_cpu;
    uint8_t firmware_quantum_remaining;
    bool firmware_skip_main_once;
    /* STM32 physical-time watchdog accounting.  The board-level CRU handlers
       increment emergency_monoflop_triggers synchronously; the 1 kHz SysTick
       service notices a new trigger and restarts these millisecond timers. */
    uint32_t main_emergency_seen_triggers;
    uint32_t clp_emergency_seen_triggers;
    uint16_t main_emergency_elapsed_ms;
    uint16_t clp_emergency_elapsed_ms;
} tnc155_machine;

bool tnc155_machine_init(tnc155_machine *machine);
void tnc155_machine_set_control_voltage_24v(tnc155_machine *machine, bool on);
void tnc155_machine_set_control_voltage_enabled(tnc155_machine *machine,
                                                bool on);
void tnc155_machine_service_1ms(tnc155_machine *machine);
tms9995_step_result tnc155_machine_step(tnc155_machine *machine,
                                        bool *stepped_main);
#ifdef TNC155_FIRMWARE
tms9995_step_result tnc155_machine_run_quantum(tnc155_machine *machine);
#endif
bool tnc155_machine_run(tnc155_machine *machine, uint64_t instruction_limit);

#endif

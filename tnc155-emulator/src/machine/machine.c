#include "tnc155/machine.h"

#include <limits.h>
#include <string.h>

static void update_emergency_feedback(tnc155_machine *machine)
{
#ifdef TNC155_FIRMWARE
    if (machine->clp.emergency_monoflop_triggered &&
        machine->clp.emergency_monoflop_q &&
        machine->clp_emergency_elapsed_ms >=
            TNC155_CLP_EMERGENCY_MONOFLOP_MS)
        machine->clp.emergency_monoflop_q = false;
    if (machine->main.emergency_monoflop_triggered &&
        machine->main.emergency_monoflop_q &&
        machine->main_emergency_elapsed_ms >=
            TNC155_MAIN_EMERGENCY_MONOFLOP_MS)
        machine->main.emergency_monoflop_q = false;
#else
    if (machine->clp.emergency_monoflop_triggered &&
        machine->clp.emergency_monoflop_q &&
        machine->clp.cpu.cycles -
                machine->clp.emergency_monoflop_last_trigger_cycle >=
            TNC155_CLP_EMERGENCY_MONOFLOP_CYCLES)
        machine->clp.emergency_monoflop_q = false;
    if (machine->main.emergency_monoflop_triggered &&
        machine->main.emergency_monoflop_q &&
        machine->main.cpu.cycles -
                machine->main.emergency_monoflop_last_trigger_cycle >=
            TNC155_MAIN_EMERGENCY_MONOFLOP_CYCLES)
        machine->main.emergency_monoflop_q = false;
#endif

    tnc155_io_backplane_set_emergency_stop_contact_closed(
        &machine->io_backplane,
        (!machine->main.emergency_monoflop_triggered ||
         machine->main.emergency_monoflop_q) &&
            (!machine->clp.emergency_monoflop_triggered ||
             machine->clp.emergency_monoflop_q));

    tnc155_io_backplane_set_input_terminal(
        &machine->io_backplane, 8u,
        tnc155_io_backplane_control_voltage_24v(&machine->io_backplane) &&
            tnc155_io_backplane_control_voltage_enabled(
                &machine->io_backplane) &&
            tnc155_io_backplane_emergency_stop_contact_closed(
                &machine->io_backplane));
}

/* Each processor board has its own TMS9902.  The TMS9902 INT output is an
   active-low level signal; tnc155_tms9902_interrupt() returns its semantic
   asserted state, which is therefore wired only to that board's TMS9995 INT1
   input.  Sample both board-level wires at every CPU instruction boundary. */
static inline void sync_serial_interrupts(tnc155_machine *machine)
{
    tms9995_set_interrupt_line(
        &machine->main.cpu, TMS9995_INTERRUPT_LEVEL1,
        tnc155_tms9902_interrupt(&machine->main.serial));
    tms9995_set_interrupt_line(
        &machine->clp.cpu, TMS9995_INTERRUPT_LEVEL1,
        tnc155_tms9902_interrupt(&machine->clp.serial));
}

void tnc155_machine_service_1ms(tnc155_machine *machine)
{
    if (machine == NULL)
        return;

    if (machine->main.emergency_monoflop_triggers !=
        machine->main_emergency_seen_triggers) {
        machine->main_emergency_seen_triggers =
            machine->main.emergency_monoflop_triggers;
        machine->main_emergency_elapsed_ms = 0u;
    } else if (machine->main.emergency_monoflop_triggered &&
               machine->main.emergency_monoflop_q &&
               machine->main_emergency_elapsed_ms != UINT16_MAX) {
        ++machine->main_emergency_elapsed_ms;
    }

    if (machine->clp.emergency_monoflop_triggers !=
        machine->clp_emergency_seen_triggers) {
        machine->clp_emergency_seen_triggers =
            machine->clp.emergency_monoflop_triggers;
        machine->clp_emergency_elapsed_ms = 0u;
    } else if (machine->clp.emergency_monoflop_triggered &&
               machine->clp.emergency_monoflop_q &&
               machine->clp_emergency_elapsed_ms != UINT16_MAX) {
        ++machine->clp_emergency_elapsed_ms;
    }

    /* Physical I/O wiring and emergency feedback are driven from the 1 kHz
       hardware timebase.  Virtual axis mechanics are intentionally absent. */
    tnc155_io_backplane_update_machine_wiring(&machine->io_backplane);
    update_emergency_feedback(machine);
}

bool tnc155_machine_init(tnc155_machine *machine)
{
    if (machine == NULL)
        return false;
    memset(machine, 0, sizeof(*machine));
    tnc155_io_backplane_reset(&machine->io_backplane);
    if (!tnc155_mainboard_init(&machine->main) ||
        !tnc155_clp_board_init(&machine->clp))
        return false;
    tnc155_mainboard_attach_io_backplane(&machine->main,
                                         &machine->io_backplane);

    tnc155_clp_attach_shared_ram(&machine->clp,
                                 &machine->main.ram[0x1800], 0x0800u);
    update_emergency_feedback(machine);
    return true;
}

void tnc155_machine_set_control_voltage_24v(tnc155_machine *machine, bool on)
{
    if (machine == NULL)
        return;
    tnc155_io_backplane_set_control_voltage_24v(&machine->io_backplane, on);
    update_emergency_feedback(machine);
}

void tnc155_machine_set_control_voltage_enabled(tnc155_machine *machine,
                                                bool on)
{
    if (machine == NULL)
        return;
    tnc155_io_backplane_set_control_voltage_enabled(&machine->io_backplane,
                                                    on);
    update_emergency_feedback(machine);
}

#ifdef TNC155_FIRMWARE
/* Execute one already-scheduled firmware instruction slot.  While CLP keeps
   CRU >1000 asserted, MAIN is physically held and a forced CLP slot must never
   be donated back to MAIN even if the CLP core is idle. */
static tms9995_step_result firmware_execute_slot(tnc155_machine *machine,
                                                 bool scheduled_main,
                                                 bool forced_clp_for_hold)
{
    tms9995 *cpu;
    bool run_main = scheduled_main;
    bool skip_cpu_step = false;
    bool hold_blocks_donation;

    sync_serial_interrupts(machine);
    machine->main_hold_asserted = machine->clp.shared_ram_enabled;
    machine->clp.shared_ram_accessed = false;
    machine->main.shared_ram_blocked = false;
    machine->main.shared_ram_stall = false;

    cpu = run_main ? &machine->main.cpu : &machine->clp.cpu;
    hold_blocks_donation = forced_clp_for_hold ||
                           machine->clp.shared_ram_enabled;

    if (cpu->idle && cpu->pending_interrupts == 0u) {
        if (!hold_blocks_donation) {
            tms9995 *other = run_main ? &machine->clp.cpu : &machine->main.cpu;
            if (!other->idle || other->pending_interrupts != 0u) {
                run_main = !run_main;
                cpu = other;
            } else {
                skip_cpu_step = true;
            }
        } else {
            skip_cpu_step = true;
        }
    }

    return skip_cpu_step ? TMS9995_STEP_IDLE : tms9995_step(cpu);
}

/* Run one complete eight-slot CPU quantum.  CLP CRU >1000 is an immediate
   MAIN HOLD request.  Once asserted, no MAIN instruction is executed and all
   complete quanta are assigned to CLP.  If CLP clears the request during its
   current quantum, that quantum is still completed by CLP and the following
   quantum is a fresh eight-slot MAIN quantum. */
tms9995_step_result tnc155_machine_run_quantum(tnc155_machine *machine)
{
    tms9995_step_result result = TMS9995_STEP_OK;
    unsigned slot;
    bool quantum_main;
    bool forced_clp_quantum;

    if (machine == NULL)
        return TMS9995_STEP_BUS_ERROR;

    forced_clp_quantum = machine->clp.shared_ram_enabled;
    quantum_main = !forced_clp_quantum && machine->firmware_next_cpu == 0u;
    machine->main_hold_asserted = forced_clp_quantum;

    if (quantum_main) {
        for (slot = 0u; slot < TNC155_CPU_QUANTUM; ++slot) {
            result = firmware_execute_slot(machine, true, false);
            if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE)
                return result;
        }
        machine->firmware_next_cpu = 1u;
    } else {
        for (slot = 0u; slot < TNC155_CPU_QUANTUM; ++slot) {
            result = firmware_execute_slot(machine, false,
                                           forced_clp_quantum);
            if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE)
                return result;
            if (machine->clp.shared_ram_enabled)
                ++machine->main_hold_steps;
        }

        /* HOLD may have been released by an instruction inside this CLP
           quantum.  CLP nevertheless finishes all eight slots. */
        machine->main_hold_asserted = machine->clp.shared_ram_enabled;
        machine->firmware_next_cpu = machine->clp.shared_ram_enabled ? 1u : 0u;
    }

    if (machine->clp.gdc.fifo_count != 0u) {
        machine->clp.gdc.host_pc = machine->clp.cpu.pc;
        tnc155_upd7220_service(&machine->clp.gdc, TNC155_CPU_QUANTUM);
    }

    machine->firmware_quantum_remaining = 0u;
    return result;
}
#endif

tms9995_step_result tnc155_machine_step(tnc155_machine *machine,
                                        bool *stepped_main)
{
    tms9995 *cpu;
    tms9995_step_result result;
#ifndef TNC155_FIRMWARE
    uint64_t before;
#endif
    uint16_t instruction_pc;
    bool run_main;
#ifdef TNC155_FIRMWARE
    bool firmware_forced_clp_for_hold = false;
    bool firmware_skip_cpu_step = false;
#endif

    if (machine == NULL)
        return TMS9995_STEP_BUS_ERROR;

    sync_serial_interrupts(machine);

#ifndef TNC155_FIRMWARE
    update_emergency_feedback(machine);
#endif

    machine->main_hold_asserted = false;
    machine->clp.shared_ram_accessed = false;

#ifdef TNC155_FIRMWARE
    if (machine->firmware_quantum_remaining == 0u)
        machine->firmware_quantum_remaining = TNC155_CPU_QUANTUM;

    if (machine->firmware_next_cpu == 0u) {
        if (machine->firmware_skip_main_once) {
            machine->firmware_skip_main_once = false;
            firmware_forced_clp_for_hold = true;
            run_main = false;
        } else {
            run_main = true;
        }
    } else {
        run_main = false;
    }

    --machine->firmware_quantum_remaining;
    if (machine->firmware_quantum_remaining == 0u)
        machine->firmware_next_cpu ^= 1u;
#else
    run_main = machine->main.cpu.cycles <= machine->clp.cpu.cycles;
#endif

    cpu = run_main ? &machine->main.cpu : &machine->clp.cpu;
#ifdef TNC155_FIRMWARE
    machine->main.shared_ram_blocked = false;
    machine->main.shared_ram_stall = false;

    if (cpu->idle && cpu->pending_interrupts == 0u) {
        if (!firmware_forced_clp_for_hold) {
            tms9995 *other = run_main ? &machine->clp.cpu : &machine->main.cpu;
            if (!other->idle || other->pending_interrupts != 0u) {
                run_main = !run_main;
                cpu = other;
            } else {
                firmware_skip_cpu_step = true;
            }
        } else {
            firmware_skip_cpu_step = true;
        }
    }
#else
    before = cpu->cycles;
#endif
    instruction_pc = cpu->pc;

#ifdef TNC155_FIRMWARE
    if (!firmware_skip_cpu_step && run_main && instruction_pc == 0x039cu) {
#else
    if (run_main && instruction_pc == 0x039cu) {
#endif
        ++machine->main.fatal_entries;
        if (machine->main.fatal_entries == 1u) {
            machine->main.first_fatal_source_pc = machine->main.last_executed_pc;
            machine->main.first_fatal_r7 = tms9995_get_register(cpu, 7u);
        }
    }

#ifdef TNC155_FIRMWARE
    result = firmware_skip_cpu_step ? TMS9995_STEP_IDLE : tms9995_step(cpu);
#else
    result = tms9995_step(cpu);
    tms9995_advance_clock(cpu, (unsigned)(cpu->cycles - before));
#endif

    if (!run_main && machine->clp.shared_ram_accessed) {
        machine->main_hold_asserted = true;
#ifdef TNC155_FIRMWARE
        machine->firmware_skip_main_once = true;
#else
        ++machine->main_hold_steps;
        if (machine->main.cpu.cycles < machine->clp.cpu.cycles)
            machine->main.cpu.cycles = machine->clp.cpu.cycles;
#endif
    }

#ifdef TNC155_FIRMWARE
    if (machine->firmware_quantum_remaining == 0u &&
        machine->clp.gdc.fifo_count != 0u) {
        machine->clp.gdc.host_pc = machine->clp.cpu.pc;
        tnc155_upd7220_service(&machine->clp.gdc, TNC155_CPU_QUANTUM);
    }
#else
    machine->clp.gdc.host_pc = machine->clp.cpu.pc;
    tnc155_upd7220_service(&machine->clp.gdc, 1u);
    update_emergency_feedback(machine);
#endif

#ifdef TNC155_FIRMWARE
    if (firmware_skip_cpu_step) {
        if (stepped_main != NULL)
            *stepped_main = run_main;
        return result;
    }
#endif

    if (run_main) {
        if (machine->main.key_trace_remaining != 0u) {
            tnc155_key_trace_entry *entry = &machine->main.key_trace[
                machine->main.key_trace_total % TNC155_KEY_TRACE_SIZE];
            entry->key_number = machine->main.key_trace_key_number;
            entry->raw_code = machine->main.key_trace_raw_code;
            entry->step = (uint8_t)((machine->main.key_trace_raw_code == 0x65u ?
                         TNC155_ENTER_TRACE_INSTRUCTIONS :
                         TNC155_KEY_TRACE_INSTRUCTIONS) -
                         machine->main.key_trace_remaining);
            entry->pc = instruction_pc;
            entry->opcode = cpu->ir;
            entry->r0 = tms9995_get_register(cpu, 0u);
            entry->r1 = tms9995_get_register(cpu, 1u);
            entry->r2 = tms9995_get_register(cpu, 2u);
            entry->r3 = tms9995_get_register(cpu, 3u);
            ++machine->main.key_trace_total;
            --machine->main.key_trace_remaining;
        }
#ifndef TNC155_FIRMWARE
        ++machine->main_instructions;
#endif
        machine->main.last_executed_pc = instruction_pc;
    } else {
#ifndef TNC155_FIRMWARE
        ++machine->clp_instructions;
#endif
        if (cpu->pc == 0x45a8u) {
            ++machine->clp.fatal_entries;
            if (machine->clp.fatal_entries == 1u) {
                machine->clp.first_fatal_source_pc = instruction_pc;
                machine->clp.first_fatal_code = tms9995_get_register(cpu, 0u);
                machine->clp.first_fatal_r6 = tms9995_get_register(cpu, 6u);
                machine->clp.first_fatal_r7 = tms9995_get_register(cpu, 7u);
                machine->clp.first_fatal_r8 = tms9995_get_register(cpu, 8u);
            }
        }
    }

    if (stepped_main != NULL)
        *stepped_main = run_main;
    return result;
}

bool tnc155_machine_run(tnc155_machine *machine, uint64_t instruction_limit)
{
    uint64_t i;
    for (i = 0; i < instruction_limit; ++i) {
        tms9995_step_result result = tnc155_machine_step(machine, NULL);
        if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE)
            return false;
    }
    return true;
}

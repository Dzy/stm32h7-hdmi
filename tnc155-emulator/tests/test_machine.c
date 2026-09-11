#include "tnc155/machine.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_machine machine;

    assert(tnc155_machine_init(&machine));
    assert(machine.main.io_backplane == &machine.io_backplane);
    assert(machine.clp.shared_ram == &machine.main.ram[0x1800u]);
    assert(machine.clp.shared_ram_size == 0x0800u);

    /* The two boards see the same physical Q67 2 KiB RAM. */
    machine.clp.shared_ram_enabled = true;
    tnc155_clp_write_byte(&machine.clp, 0xf800u, 0x5au);
    tnc155_clp_write_byte(&machine.clp, 0xffffu, 0xa5u);
    assert(tnc155_mainboard_read_byte(&machine.main, 0xf800u,
                                      TMS9995_BUS_DATA_READ) == 0x5au);
    assert(tnc155_mainboard_read_byte(&machine.main, 0xffffu,
                                      TMS9995_BUS_DATA_READ) == 0xa5u);
    tnc155_mainboard_write_byte(&machine.main, 0xf801u, 0x3cu);
    tnc155_mainboard_write_byte(&machine.main, 0xfffeu, 0xc3u);
    assert(tnc155_clp_read_byte(&machine.clp, 0xf801u,
                                TMS9995_BUS_DATA_READ) == 0x3cu);
    assert(tnc155_clp_read_byte(&machine.clp, 0xfffeu,
                                TMS9995_BUS_DATA_READ) == 0xc3u);

    /* CRU >1000 is only the CLP shared-window gate.  Keeping the gate high
       must not HOLD MAIN by itself.  With equal clocks the normal scheduler
       chooses MAIN and executes it. */
    assert(tnc155_machine_init(&machine));
    machine.clp.shared_ram_enabled = true;
    machine.main.cpu.cycles = machine.clp.cpu.cycles;
    {
        bool stepped_main = false;
        tms9995_step_result result = tnc155_machine_step(&machine,
                                                         &stepped_main);
        assert(result == TMS9995_STEP_OK || result == TMS9995_STEP_IDLE);
        assert(stepped_main);
        assert(!machine.main_hold_asserted);
    }

    /* A denied CLP access does not claim the shared bus. */
    machine.clp.shared_ram_enabled = false;
    machine.clp.shared_ram_accessed = false;
    (void)tnc155_clp_read_byte(&machine.clp, 0xfa16u,
                               TMS9995_BUS_DATA_READ);
    assert(!machine.clp.shared_ram_accessed);

    /* Regression for the real HOLD rule.  Execute an actual CLP guest
       instruction from local fast RAM:

           >A000: C020 F800    MOV @>F800,R0

       The CRU gate is already high, but MAIN is held only because this CLP
       instruction really performs the Q67 access. */
    assert(tnc155_machine_init(&machine));
    machine.clp.shared_ram_enabled = true;
    machine.main.ram[0x1800u] = 0x12u;
    machine.main.ram[0x1801u] = 0x34u;
    machine.clp.fast_ram[0x0000u] = 0xc0u;
    machine.clp.fast_ram[0x0001u] = 0x20u;
    machine.clp.fast_ram[0x0002u] = 0xf8u;
    machine.clp.fast_ram[0x0003u] = 0x00u;
    machine.clp.cpu.pc = 0xa000u;
    machine.clp.cpu.cycles = 0u;
    machine.main.cpu.cycles = 1u;
    {
        bool stepped_main = true;
        tms9995_step_result result = tnc155_machine_step(&machine,
                                                         &stepped_main);
        assert(result == TMS9995_STEP_OK);
        assert(!stepped_main);
        assert(machine.clp.shared_ram_accessed);
        assert(machine.main_hold_asserted);
        assert(machine.main.cpu.cycles >= machine.clp.cpu.cycles);
        assert(tms9995_get_register(&machine.clp.cpu, 0u) == 0x1234u);
    }

    /* The former virtual-axis address range is passive gate-array state;
       CPU execution must not manufacture position/counter/index changes. */
    assert(tnc155_machine_init(&machine));
    tnc155_clp_write_byte(&machine.clp, 0xf400u, 0x5au);
    assert(tnc155_machine_run(&machine, 1000u));
    assert(tnc155_clp_read_byte(&machine.clp, 0xf400u,
                                TMS9995_BUS_DATA_READ) == 0x5au);

    /* Physical emergency feedback still follows the two watchdog outputs. */
    assert(tnc155_machine_init(&machine));
    tnc155_machine_set_control_voltage_enabled(&machine, true);
    assert(tnc155_io_backplane_emergency_stop_contact_closed(
        &machine.io_backplane));
    assert(tnc155_io_backplane_read_input_terminal(&machine.io_backplane, 8u));
    machine.main.emergency_monoflop_triggered = true;
    machine.main.emergency_monoflop_q = false;
    tnc155_machine_set_control_voltage_enabled(&machine, true);
    assert(!tnc155_io_backplane_emergency_stop_contact_closed(
        &machine.io_backplane));
    assert(!tnc155_io_backplane_read_input_terminal(&machine.io_backplane, 8u));

    /* Normal combined execution still advances both processors. */
    assert(tnc155_machine_init(&machine));
    assert(tnc155_machine_run(&machine, 10000u));
    assert(machine.main_instructions != 0u);
    assert(machine.clp_instructions != 0u);

    puts("combined MAIN/CLP machine and shared RAM arbitration: OK");
    return 0;
}

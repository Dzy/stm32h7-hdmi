#include "tnc155/tms9995.h"

#include <assert.h>
#include <stdio.h>

typedef struct test_bus {
    uint8_t memory[65536];
    unsigned data_reads;
    unsigned opcode_fetches;
    unsigned fast_opcode_fetches;
    uint16_t last_cru_address;
    bool last_cru_value;
    bool cru_bits[32768];
    uint16_t last_byte_write_address;
    uint8_t last_byte_write_value;
    unsigned byte_writes;
} test_bus;

static uint16_t read_word(void *opaque, uint16_t address,
                          tms9995_bus_cycle cycle)
{
    test_bus *bus = opaque;
    if (cycle == TMS9995_BUS_OPCODE_FETCH)
        ++bus->opcode_fetches;
    else
        ++bus->data_reads;
    return (uint16_t)((uint16_t)bus->memory[address] << 8 |
                      bus->memory[(uint16_t)(address + 1u)]);
}

static uint16_t read_opcode_word_fast(void *opaque, uint16_t address)
{
    test_bus *bus = opaque;
    ++bus->fast_opcode_fetches;
    return (uint16_t)((uint16_t)bus->memory[address] << 8 |
                      bus->memory[(uint16_t)(address + 1u)]);
}

static void write_word(void *opaque, uint16_t address, uint16_t value,
                       tms9995_bus_cycle cycle)
{
    test_bus *bus = opaque;
    assert(cycle == TMS9995_BUS_DATA_WRITE);
    bus->memory[address] = (uint8_t)(value >> 8);
    bus->memory[(uint16_t)(address + 1u)] = (uint8_t)value;
}

static void write_byte(void *opaque, uint16_t address, uint8_t value,
                       tms9995_bus_cycle cycle)
{
    test_bus *bus = opaque;
    assert(cycle == TMS9995_BUS_DATA_WRITE);
    bus->memory[address] = value;
    bus->last_byte_write_address = address;
    bus->last_byte_write_value = value;
    ++bus->byte_writes;
}

static void cru_write(void *opaque, uint16_t address, bool value)
{
    test_bus *bus = opaque;
    bus->last_cru_address = address;
    bus->last_cru_value = value;
    bus->cru_bits[address & 0x7fffu] = value;
}

static bool cru_read(void *opaque, uint16_t address)
{
    test_bus *bus = opaque;
    return bus->cru_bits[address & 0x7fffu];
}

int main(void)
{
    test_bus memory = {0};
    tms9995_bus bus = {
        .opaque = &memory,
        .read_word = read_word,
        .write_word = write_word,
        .cru_read_bit = cru_read,
        .cru_write_bit = cru_write
    };
    tms9995 cpu;

    memory.memory[0] = 0xf0;
    memory.memory[1] = 0x00;
    memory.memory[2] = 0x0a;
    memory.memory[3] = 0x22;
    /* BL @>0BA8; there: LI R12,>0100; SBO 3; JMP 0. */
    memory.memory[0x0a22] = 0x06;
    memory.memory[0x0a23] = 0xa0;
    memory.memory[0x0a24] = 0x0b;
    memory.memory[0x0a25] = 0xa8;
    memory.memory[0x0ba8] = 0x02;
    memory.memory[0x0ba9] = 0x0c;
    memory.memory[0x0baa] = 0x01;
    memory.memory[0x0bab] = 0x00;
    memory.memory[0x0bac] = 0x1d;
    memory.memory[0x0bad] = 0x03;
    memory.memory[0x0bae] = 0x10;
    memory.memory[0x0baf] = 0x00;

    tms9995_init(&cpu, &bus);
    assert(tms9995_reset(&cpu));
    assert(cpu.wp == 0xf000u);
    assert(cpu.pc == 0x0a22u);
    assert(memory.data_reads == 2u);
    assert(memory.opcode_fetches == 0u);

    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.pc == 0x0ba8u);
    assert(cpu.internal_ram[0x16] == 0x0a);
    assert(cpu.internal_ram[0x17] == 0x26);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.last_cru_address == 0x0083u);
    assert(memory.last_cru_value);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.pc == 0x0bb0u);
    assert(memory.opcode_fetches == 4u);
    assert(memory.data_reads >= 4u);

    /* Optional opcode callback bypasses the generic external read_word path. */
    cpu.bus.read_opcode_word = read_opcode_word_fast;
    memory.memory[0x0600] = 0x10u; /* JMP 0 */
    memory.memory[0x0601] = 0x00u;
    cpu.pc = 0x0600u;
    {
        unsigned generic_before = memory.opcode_fetches;
        assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
        assert(cpu.pc == 0x0602u);
        assert(memory.fast_opcode_fetches == 1u);
        assert(memory.opcode_fetches == generic_before);
    }

    /* Code in TMS9995 internal RAM must not escape through the board callback. */
    cpu.internal_ram[0] = 0x10u; /* JMP 0 */
    cpu.internal_ram[1] = 0x00u;
    cpu.pc = 0xf000u;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.pc == 0xf002u);
    assert(memory.fast_opcode_fetches == 1u);
    cpu.bus.read_opcode_word = NULL;

    /* BLWP saves the old context in R13-R15 of the new workspace; RTWP restores it. */
    cpu.pc = 0x0200u;
    cpu.wp = 0xf000u;
    cpu.st = 0xa005u;
    memory.memory[0x0200] = 0x04;
    memory.memory[0x0201] = 0x20; /* BLWP @>0300 */
    memory.memory[0x0202] = 0x03;
    memory.memory[0x0203] = 0x00;
    memory.memory[0x0300] = 0xe0;
    memory.memory[0x0301] = 0x00;
    memory.memory[0x0302] = 0x04;
    memory.memory[0x0303] = 0x00;
    memory.memory[0x0400] = 0x03;
    memory.memory[0x0401] = 0x80; /* RTWP */
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xe000u && cpu.pc == 0x0400u);
    assert(memory.memory[0xe01a] == 0xf0 && memory.memory[0xe01b] == 0x00);
    assert(memory.memory[0xe01c] == 0x02 && memory.memory[0xe01d] == 0x04);
    assert(memory.memory[0xe01e] == 0xa0 && memory.memory[0xe01f] == 0x05);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xf000u && cpu.pc == 0x0204u && cpu.st == 0xa005u);

    /* Internal decrementer requests level 3 and vectors through >000C. */
    memory.memory[0x000c] = 0xd0;
    memory.memory[0x000d] = 0x00;
    memory.memory[0x000e] = 0x05;
    memory.memory[0x000f] = 0x00;
    cpu.st = (uint16_t)((cpu.st & ~TMS9995_ST_IM) | 3u);
    cpu.decrementer_start = 1;
    cpu.decrementer_value = 1;
    cpu.flags = 2u; /* FLAG1 enables timer mode; FLAG0 remains zero. */
    tms9995_advance_clock(&cpu, 15u);
    assert(cpu.decrementer_value == 1u);
    assert((cpu.pending_interrupts & 0x04u) == 0u);
    tms9995_advance_clock(&cpu, 1u);
    assert(cpu.decrementer_value == 1u); /* reloads on expiry */
    assert((cpu.pending_interrupts & 0x04u) != 0u);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xd000u && cpu.pc == 0x0500u);
    assert((cpu.st & TMS9995_ST_IM) == 2u);

    /* Reprogramming the decrementer does not acknowledge an already
       latched level-3 request; only reset or the level-3 context switch
       clears that latch. */
    cpu.pending_interrupts = 0x04u;
    cpu.wp = 0xf000u;
    cpu.pc = 0x0500u;
    cpu.internal_ram[0x00] = 0x12u;
    cpu.internal_ram[0x01] = 0x34u;
    memory.memory[0x0500] = 0xc8u;
    memory.memory[0x0501] = 0x00u;
    memory.memory[0x0502] = 0xffu;
    memory.memory[0x0503] = 0xfau;
    cpu.interrupt_inhibit = 1u;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.decrementer_start == 0x1234u);
    assert(cpu.decrementer_value == 0x1234u);
    assert((cpu.pending_interrupts & 0x04u) != 0u);

    /* FLAG1 gates event counting. Disabled INT4/EC remains level 4. */
    cpu.pending_interrupts = 0u;
    cpu.flags = 1u;
    cpu.decrementer_start = 2u;
    cpu.decrementer_value = 2u;
    tms9995_set_interrupt_line(&cpu, TMS9995_INTERRUPT_LEVEL4, true);
    assert(cpu.decrementer_value == 2u);
    assert((cpu.pending_interrupts & 0x08u) != 0u);
    tms9995_set_interrupt_line(&cpu, TMS9995_INTERRUPT_LEVEL4, false);
    cpu.pending_interrupts = 0u;
    cpu.flags = 3u;
    tms9995_set_interrupt_line(&cpu, TMS9995_INTERRUPT_LEVEL4, true);
    assert(cpu.decrementer_value == 1u);
    assert((cpu.pending_interrupts & 0x08u) == 0u);
    tms9995_set_interrupt_line(&cpu, TMS9995_INTERRUPT_LEVEL4, false);

    /* IDLE advances internal time so the decrementer can wake level 3. */
    cpu.pending_interrupts = 0u;
    cpu.flags = 2u;
    cpu.decrementer_start = 1u;
    cpu.decrementer_value = 1u;
    cpu.decrementer_phase = 0u;
    cpu.idle = true;
    cpu.interrupt_inhibit = 0u;
    cpu.st = (uint16_t)((cpu.st & ~TMS9995_ST_IM) | 3u);
    {
        uint64_t before = cpu.cycles;
        assert(tms9995_step(&cpu) == TMS9995_STEP_IDLE);
        assert(cpu.cycles - before == 16u);
        tms9995_advance_clock(&cpu, (unsigned)(cpu.cycles - before));
    }
    assert((cpu.pending_interrupts & 0x04u) != 0u);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(!cpu.idle);
    assert(cpu.wp == 0xd000u && cpu.pc == 0x0500u);

    /* An MID request clears on vectoring while the software-visible flag remains. */
    memory.memory[0x0008] = 0xc0;
    memory.memory[0x0009] = 0x00;
    memory.memory[0x000a] = 0x06;
    memory.memory[0x000b] = 0x00;
    cpu.pc = 0x0700u;
    cpu.interrupt_inhibit = 0;
    memory.memory[0x0700] = 0x00;
    memory.memory[0x0701] = 0x00; /* MID opcode */
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.mid_flag);
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xc000u && cpu.pc == 0x0600u);
    assert(cpu.mid_flag);

    /* TNC P3 distinguishes MID from arithmetic overflow at their shared
       level-2 vector with R12=>1FDA / TB 0, i.e. CRU bit >0FED. */
    cpu.wp = 0xf000u;
    cpu.pc = 0x0710u;
    cpu.pending_interrupts = 0;
    cpu.interrupt_inhibit = 0;
    cpu.internal_ram[0x18] = 0x1f;
    cpu.internal_ram[0x19] = 0xda;
    memory.memory[0x0710] = 0x1f; /* TB 0 */
    memory.memory[0x0711] = 0x00;
    cpu.st &= (uint16_t)~TMS9995_ST_EQ;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert((cpu.st & TMS9995_ST_EQ) != 0u);

    /* LDCR and STCR transfer least-significant field bits in ascending order. */
    cpu.wp = 0xf000u;
    cpu.pc = 0x0800u;
    cpu.interrupt_inhibit = 0;
    cpu.pending_interrupts = 0;
    cpu.internal_ram[0x04] = 0x0a; /* R2=>0A00: addressed register byte is >0A. */
    cpu.internal_ram[0x05] = 0x00;
    cpu.internal_ram[0x18] = 0x02; /* R12=>0200: CRU hardware base >0100. */
    cpu.internal_ram[0x19] = 0x00;
    memory.memory[0x0800] = 0x31;  /* LDCR R2,4 */
    memory.memory[0x0801] = 0x02;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(!memory.cru_bits[0x100] && memory.cru_bits[0x101]);
    assert(!memory.cru_bits[0x102] && memory.cru_bits[0x103]);

    cpu.internal_ram[0x06] = 0x00;
    cpu.internal_ram[0x07] = 0x55;
    memory.memory[0x0802] = 0x35;  /* STCR R3,4 */
    memory.memory[0x0803] = 0x03;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.internal_ram[0x06] == 0x0a);
    assert(cpu.internal_ram[0x07] == 0x55);

    /* X obtains its opcode through a data read and consumes extensions at PC. */
    memory.memory[0x0804] = 0x04;  /* X @>0900 */
    memory.memory[0x0805] = 0xa0;
    memory.memory[0x0806] = 0x09;
    memory.memory[0x0807] = 0x00;
    memory.memory[0x0900] = 0x02;  /* LI R4,>CAFE */
    memory.memory[0x0901] = 0x04;
    memory.memory[0x0808] = 0xca;
    memory.memory[0x0809] = 0xfe;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.internal_ram[0x08] == 0xca && cpu.internal_ram[0x09] == 0xfe);
    assert(cpu.pc == 0x080au);

    /* Compare changes only LGT/AGT/EQ and distinguishes unsigned from signed. */
    cpu.pc = 0x0810u;
    cpu.internal_ram[0x0a] = 0xff; /* R5=>FFFF */
    cpu.internal_ram[0x0b] = 0xff;
    cpu.st = TMS9995_ST_C | TMS9995_ST_OV;
    memory.memory[0x0810] = 0x02; /* CI R5,>0001 */
    memory.memory[0x0811] = 0x85;
    memory.memory[0x0812] = 0x00;
    memory.memory[0x0813] = 0x01;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert((cpu.st & TMS9995_ST_LGT) != 0); /* >FFFF unsigned > >0001 */
    assert((cpu.st & TMS9995_ST_AGT) == 0); /* -1 signed is not > +1 */
    assert((cpu.st & (TMS9995_ST_C | TMS9995_ST_OV)) ==
           (TMS9995_ST_C | TMS9995_ST_OV));

    /* ABS stores the magnitude but derives LGT/AGT/EQ from the original
       operand.  TNC MAIN uses AGT while validating negative limit values. */
    cpu.pc = 0x0814u;
    cpu.internal_ram[0x0c] = 0xff; /* R6=>FFFF (-1) */
    cpu.internal_ram[0x0d] = 0xff;
    cpu.st = TMS9995_ST_C | TMS9995_ST_OV | TMS9995_ST_AGT |
             TMS9995_ST_EQ;
    memory.memory[0x0814] = 0x07; /* ABS R6 */
    memory.memory[0x0815] = 0x46;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.internal_ram[0x0c] == 0x00 && cpu.internal_ram[0x0d] == 0x01);
    assert((cpu.st & TMS9995_ST_LGT) != 0u);
    assert((cpu.st & TMS9995_ST_AGT) == 0u);
    assert((cpu.st & TMS9995_ST_EQ) == 0u);
    assert((cpu.st & TMS9995_ST_OV) == 0u);
    assert((cpu.st & TMS9995_ST_C) == 0u);

    cpu.pc = 0x0816u;
    cpu.internal_ram[0x0c] = 0x80; /* R6=>8000: unrepresentable magnitude. */
    cpu.internal_ram[0x0d] = 0x00;
    cpu.st = 0u;
    memory.memory[0x0816] = 0x07; /* ABS R6 */
    memory.memory[0x0817] = 0x46;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.internal_ram[0x0c] == 0x80 && cpu.internal_ram[0x0d] == 0x00);
    assert((cpu.st & TMS9995_ST_LGT) != 0u);
    assert((cpu.st & TMS9995_ST_AGT) == 0u);
    assert((cpu.st & TMS9995_ST_EQ) == 0u);
    assert((cpu.st & TMS9995_ST_OV) != 0u);

    /* Only SLA changes OV; the three other shift instructions preserve it. */
    cpu.pc = 0x0818u;
    cpu.internal_ram[0x0e] = 0x00; /* R7=>0001 */
    cpu.internal_ram[0x0f] = 0x01;
    cpu.st = TMS9995_ST_OV;
    memory.memory[0x0818] = 0x09; /* SRL R7,1 */
    memory.memory[0x0819] = 0x17;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert((cpu.st & TMS9995_ST_OV) != 0u);
    assert((cpu.st & TMS9995_ST_C) != 0u);

    /* TMS9995 forces workspace pointers even for LWPI and LWP. */
    cpu.pc = 0x081au;
    memory.memory[0x081a] = 0x02; /* LWPI >E001 */
    memory.memory[0x081b] = 0xe0;
    memory.memory[0x081c] = 0xe0;
    memory.memory[0x081d] = 0x01;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xe000u);
    memory.memory[0xe000] = 0xd0; /* R0=>D001 */
    memory.memory[0xe001] = 0x01;
    memory.memory[0x081e] = 0x00; /* LWP R0 */
    memory.memory[0x081f] = 0x90;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xd000u);

    /* MPY R15 writes its low result word after R15, never into R0. */
    cpu.wp = 0xe000u;
    cpu.pc = 0x0860u;
    memory.memory[0xe000] = 0x12; /* R0 sentinel */
    memory.memory[0xe001] = 0x34;
    memory.memory[0xe01e] = 0x00; /* R15=>0003 */
    memory.memory[0xe01f] = 0x03;
    memory.memory[0x0920] = 0x00; /* source=>0004 */
    memory.memory[0x0921] = 0x04;
    memory.memory[0x0860] = 0x3b; /* MPY @>0920,R15 */
    memory.memory[0x0861] = 0xe0;
    memory.memory[0x0862] = 0x09;
    memory.memory[0x0863] = 0x20;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.memory[0xe000] == 0x12 && memory.memory[0xe001] == 0x34);
    assert(memory.memory[0xe01e] == 0x00 && memory.memory[0xe01f] == 0x00);
    assert(memory.memory[0xe020] == 0x00 && memory.memory[0xe021] == 0x0c);
    cpu.wp = 0xf000u;

    /* XOP sets X without destroying arithmetic or overflow-enable state. */
    cpu.pc = 0x0870u;
    cpu.st = TMS9995_ST_C | TMS9995_ST_OV | 0x0020u | 7u;
    memory.memory[0x0040] = 0xe0; /* XOP 0 vector */
    memory.memory[0x0041] = 0x00;
    memory.memory[0x0042] = 0x09;
    memory.memory[0x0043] = 0x40;
    memory.memory[0x0870] = 0x2c; /* XOP R0,0 */
    memory.memory[0x0871] = 0x00;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(cpu.wp == 0xe000u && cpu.pc == 0x0940u);
    assert((cpu.st & (TMS9995_ST_X | TMS9995_ST_C | TMS9995_ST_OV |
                      0x0020u | TMS9995_ST_IM)) ==
           (TMS9995_ST_X | TMS9995_ST_C | TMS9995_ST_OV | 0x0020u | 7u));
    cpu.wp = 0xf000u;

    /* Byte autoincrement advances by one byte, not one word. */
    cpu.pc = 0x0820u;
    cpu.internal_ram[0x02] = 0x09; /* R1=>0900 */
    cpu.internal_ram[0x03] = 0x00;
    cpu.internal_ram[0x04] = 0x09; /* R2=>0910 */
    cpu.internal_ram[0x05] = 0x10;
    memory.memory[0x0900] = 0x12;
    memory.memory[0x0901] = 0x34;
    memory.memory[0x0820] = 0xdc; /* MOVB *R1+,*R2+ */
    memory.memory[0x0821] = 0xb1;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.memory[0x0910] == 0x12);
    assert(cpu.internal_ram[0x02] == 0x09 && cpu.internal_ram[0x03] == 0x01);
    assert(cpu.internal_ram[0x04] == 0x09 && cpu.internal_ram[0x05] == 0x11);

    /* TMS9900-family word cycles ignore A0.  Keep the odd value in the
       address register, but access the preceding even-addressed word. */
    cpu.pc = 0x0880u;
    cpu.internal_ram[0x02] = 0x09; /* R1=>0901 (deliberately odd) */
    cpu.internal_ram[0x03] = 0x01;
    memory.memory[0x0900] = 0x12;
    memory.memory[0x0901] = 0x34;
    memory.memory[0x0902] = 0x56;
    memory.memory[0x0880] = 0xc8; /* MOV *R1+,@>0A00 */
    memory.memory[0x0881] = 0x31;
    memory.memory[0x0882] = 0x0a;
    memory.memory[0x0883] = 0x00;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.memory[0x0a00] == 0x12 && memory.memory[0x0a01] == 0x34);
    /* Autoincrement is still two and therefore preserves odd parity. */
    assert(cpu.internal_ram[0x02] == 0x09 && cpu.internal_ram[0x03] == 0x03);

    cpu.pc = 0x0884u;
    cpu.internal_ram[0x02] = 0x09; /* R1=>0901, odd word destination */
    cpu.internal_ram[0x03] = 0x01;
    cpu.internal_ram[0x04] = 0xab; /* R2=>ABCD */
    cpu.internal_ram[0x05] = 0xcd;
    memory.memory[0x0900] = 0x00;
    memory.memory[0x0901] = 0x00;
    memory.memory[0x0902] = 0x5a;
    memory.memory[0x0884] = 0xc4; /* MOV R2,*R1 */
    memory.memory[0x0885] = 0x42;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.memory[0x0900] == 0xab && memory.memory[0x0901] == 0xcd);
    assert(memory.memory[0x0902] == 0x5a);

    /* A symbolic byte destination performs a write-only bus cycle. */
    cpu.bus.write_byte = write_byte;
    cpu.pending_interrupts = 0;
    cpu.flags = 0;
    cpu.decrementer_start = 0;
    cpu.interrupt_inhibit = 0;
    cpu.pc = 0x0830u;
    cpu.internal_ram[0x04] = 0x09; /* R2=>0900 */
    cpu.internal_ram[0x05] = 0x00;
    memory.memory[0x0900] = 0x5a;
    memory.memory[0x0830] = 0xd8; /* MOVB *R2+,@>E000 */
    memory.memory[0x0831] = 0x32;
    memory.memory[0x0832] = 0xe0;
    memory.memory[0x0833] = 0x00;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.byte_writes == 1u);
    assert(memory.last_byte_write_address == 0xe000u);
    assert(memory.last_byte_write_value == 0x5au);

    /* Register byte operands are the most-significant byte of the word. */
    cpu.pc = 0x0840u;
    cpu.internal_ram[0x04] = 0xa5; /* R2=>A55A */
    cpu.internal_ram[0x05] = 0x5a;
    memory.memory[0x0840] = 0xd8; /* MOVB R2,@>E002 */
    memory.memory[0x0841] = 0x02;
    memory.memory[0x0842] = 0xe0;
    memory.memory[0x0843] = 0x02;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert(memory.last_byte_write_address == 0xe002u);
    assert(memory.last_byte_write_value == 0xa5u);

    /* C/CB flags describe source relative to destination.  This exact CB
       ordering controls which CLP runtime payload variant is loaded. */
    cpu.pc = 0x0850u;
    memory.memory[0x0900] = 0x02;
    memory.memory[0x0910] = 0x01;
    memory.memory[0x0850] = 0x98; /* CB @>0900,@>0910 */
    memory.memory[0x0851] = 0x20;
    memory.memory[0x0852] = 0x09;
    memory.memory[0x0853] = 0x00;
    memory.memory[0x0854] = 0x09;
    memory.memory[0x0855] = 0x10;
    assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
    assert((cpu.st & TMS9995_ST_LGT) != 0u);
    assert((cpu.st & TMS9995_ST_EQ) == 0u);
    /* Internal workspace word accesses stay on-chip. */
    cpu.pc = 0x0860u;
    cpu.wp = 0xf000u;
    cpu.pending_interrupts = 0u;
    cpu.interrupt_inhibit = 0u;
    cpu.internal_ram[0x04] = 0xffu; /* R2=>FFFF (-1) */
    cpu.internal_ram[0x05] = 0xffu;
    memory.memory[0x0860] = 0x07u; /* ABS R2 */
    memory.memory[0x0861] = 0x42u;
    {
        unsigned data_reads_before = memory.data_reads;
        assert(tms9995_step(&cpu) == TMS9995_STEP_OK);
        assert(memory.data_reads == data_reads_before);
    }
    assert(cpu.internal_ram[0x04] == 0x00u &&
           cpu.internal_ram[0x05] == 0x01u);

    puts("TMS9995 reset and IAQ fetch: OK");
    return 0;
}

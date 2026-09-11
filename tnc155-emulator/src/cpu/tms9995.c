#include "tnc155/tms9995.h"
#include "tnc155/fast_mem.h"

#include <string.h>

#ifdef TNC155_FIRMWARE
#define TMS9995_ADD_CYCLES(cpu_, amount_) ((void)0)
#else
#define TMS9995_ADD_CYCLES(cpu_, amount_) \
    ((cpu_)->cycles += (uint64_t)(amount_))
#endif

enum {
    PENDING_NMI = 1u << 0,
    PENDING_LEVEL1 = 1u << 1,
    PENDING_LEVEL3 = 1u << 2,
    PENDING_LEVEL4 = 1u << 3,
    PENDING_MID = 1u << 4
};

static uint16_t read_reg(tms9995 *cpu, unsigned reg);
static void write_reg(tms9995 *cpu, unsigned reg, uint16_t value);

static int internal_ram_index(uint16_t address)
{
    if (address >= 0xf000u && address <= 0xf0fbu)
        return address - 0xf000u;
    if (address >= 0xfffcu)
        return 252 + address - 0xfffcu;
    return -1;
}

static uint8_t internal_read_byte(tms9995 *cpu, uint16_t address,
                                  tms9995_bus_cycle cycle)
{
    int index = internal_ram_index(address);
    if (index >= 0)
        return cpu->internal_ram[index];
    if (address == 0xfffau)
        return (uint8_t)(cpu->decrementer_value >> 8);
    if (address == 0xfffbu)
        return (uint8_t)cpu->decrementer_value;
    if (cpu->bus.read_byte != NULL)
        return cpu->bus.read_byte(cpu->bus.opaque, address, cycle);
    {
        uint16_t word = cpu->bus.read_word(cpu->bus.opaque,
                                           (uint16_t)(address & 0xfffeu), cycle);
        return (address & 1u) ? (uint8_t)word : (uint8_t)(word >> 8);
    }
}

static uint16_t read_word(tms9995 *cpu, uint16_t address,
                          tms9995_bus_cycle cycle)
{
    unsigned index;

    /* TMS9900-family word accesses ignore A0.  Align once here so every
       external board callback receives the electrically selected word. */
    address = (uint16_t)(address & 0xfffeu);

    if (address == 0xfffau)
        return cpu->decrementer_value;

    /* The two on-chip RAM windows are byte arrays in TMS big-endian
       order.  Word accesses never need the byte decoder: resolve the
       contiguous pair once and assemble it explicitly. */
    if (address >= 0xf000u && address <= 0xf0fau) {
        index = (unsigned)(address - 0xf000u);
        return tnc155_load_be16_aligned(&cpu->internal_ram[index]);
    }
    if (address >= 0xfffcu) {
        index = 252u + (unsigned)(address - 0xfffcu);
        return tnc155_load_be16_aligned(&cpu->internal_ram[index]);
    }

    return cpu->bus.read_word(cpu->bus.opaque, address, cycle);
}

static void write_word(tms9995 *cpu, uint16_t address, uint16_t value)
{
    unsigned index;

    /* Word writes use the same A0-ignored alignment as word reads. */
    address = (uint16_t)(address & 0xfffeu);

    if (address == 0xfffau) {
        cpu->decrementer_start = value;
        cpu->decrementer_value = value;
        return;
    }
    if (address >= 0xf000u && address <= 0xf0fau) {
        index = (unsigned)(address - 0xf000u);
        tnc155_store_be16_aligned(&cpu->internal_ram[index], value);
        return;
    }
    if (address >= 0xfffcu) {
        index = 252u + (unsigned)(address - 0xfffcu);
        tnc155_store_be16_aligned(&cpu->internal_ram[index], value);
        return;
    }

    cpu->bus.write_word(cpu->bus.opaque, address, value,
                        TMS9995_BUS_DATA_WRITE);
}

static uint8_t read_byte(tms9995 *cpu, uint16_t address)
{
    return internal_read_byte(cpu, address, TMS9995_BUS_DATA_READ);
}

static void write_byte(tms9995 *cpu, uint16_t address, uint8_t value)
{
    uint16_t aligned;
    uint16_t word;
    int index = internal_ram_index(address);
    if (index >= 0) {
        cpu->internal_ram[index] = value;
        return;
    }
    if (address == 0xfffau || address == 0xfffbu) {
        if (address & 1u)
            value = (uint8_t)value;
        word = cpu->decrementer_value;
        if (address & 1u)
            word = (uint16_t)((word & 0xff00u) | value);
        else
            word = (uint16_t)((word & 0x00ffu) | ((uint16_t)value << 8));
        cpu->decrementer_start = word;
        cpu->decrementer_value = word;
        return;
    }
    if (cpu->bus.write_byte != NULL) {
        cpu->bus.write_byte(cpu->bus.opaque, address, value,
                            TMS9995_BUS_DATA_WRITE);
        return;
    }
    aligned = (uint16_t)(address & 0xfffeu);
    word = read_word(cpu, aligned, TMS9995_BUS_DATA_READ);
    if (address & 1u)
        word = (uint16_t)((word & 0xff00u) | value);
    else
        word = (uint16_t)((word & 0x00ffu) | ((uint16_t)value << 8));
    write_word(cpu, aligned, word);
}

static bool cru_read(tms9995 *cpu, uint16_t address)
{
    if (address >= 0x0f70u && address <= 0x0f7fu)
        return (cpu->flags & (uint16_t)(1u << (address - 0x0f70u))) != 0;
    /* The MID flag is CRU bit >0FED.  Firmware addresses it with
       R12=>1FDA followed by TB/SBO/SBZ 0. */
    if (address == 0x0fedu)
        return cpu->mid_flag;
    return cpu->bus.cru_read_bit != NULL &&
           cpu->bus.cru_read_bit(cpu->bus.opaque, address);
}

static void cru_write(tms9995 *cpu, uint16_t address, bool value)
{
    if (address >= 0x0f70u && address <= 0x0f7fu) {
        unsigned flag = (unsigned)(address - 0x0f70u);
        uint16_t mask = (uint16_t)(1u << flag);

        /* FLAG2, FLAG3 and FLAG4 are the internal interrupt-request latch
           indications.  They are input-only from the CRU point of view. */
        if (flag >= 2u && flag <= 4u)
            return;

        if (value)
            cpu->flags |= mask;
        else
            cpu->flags &= (uint16_t)~mask;
        return;
    }
    if (address == 0x0fedu) {
        cpu->mid_flag = value;
        return;
    }
    if (cpu->bus.cru_write_bit != NULL)
        cpu->bus.cru_write_bit(cpu->bus.opaque, address, value);
}

static void context_switch(tms9995 *cpu, uint16_t vector, uint8_t new_mask)
{
    uint16_t old_wp = cpu->wp;
    uint16_t old_pc = cpu->pc;
    uint16_t old_st = cpu->st;
    cpu->wp = (uint16_t)(read_word(cpu, vector, TMS9995_BUS_DATA_READ) &
                         0xfffeu);
    cpu->pc = (uint16_t)(read_word(cpu, (uint16_t)(vector + 2u),
                                  TMS9995_BUS_DATA_READ) & 0xfffeu);
    write_reg(cpu, 13u, old_wp);
    write_reg(cpu, 14u, old_pc);
    write_reg(cpu, 15u, old_st);
    cpu->st &= (uint16_t)~0x01f0u;
    cpu->st = (uint16_t)((cpu->st & ~TMS9995_ST_IM) | (new_mask & 15u));
    cpu->idle = false;
    cpu->interrupt_inhibit = 1;
}

static void refresh_level_interrupts(tms9995 *cpu)
{
    /* INT1 and INT4 accept both pulses and levels.  The pulse-catching latch
       is represented by pending_interrupts/FLAG2 or FLAG4; an asserted level
       must also keep the request present after a context switch clears that
       latch. */
    if (cpu->int1_active) {
        cpu->flags |= (uint16_t)(1u << 2);
        cpu->pending_interrupts |= PENDING_LEVEL1;
    }

    /* INT4/EC is an event-counter input only while the decrementer is both
       enabled (FLAG1) and configured as an event counter (FLAG0).  If the
       decrementer is disabled, the pin retains its normal Level-4 role. */
    if ((cpu->flags & 3u) == 3u) {
        cpu->flags &= (uint16_t)~(1u << 4);
        cpu->pending_interrupts &= (uint8_t)~PENDING_LEVEL4;
    } else if (cpu->int4_active) {
        cpu->flags |= (uint16_t)(1u << 4);
        cpu->pending_interrupts |= PENDING_LEVEL4;
    }
}

static bool service_pending_interrupt(tms9995 *cpu)
{
    unsigned mask;

    refresh_level_interrupts(cpu);
    mask = cpu->st & TMS9995_ST_IM;
    if (cpu->pending_interrupts & PENDING_MID) {
        cpu->pending_interrupts &= (uint8_t)~PENDING_MID;
        context_switch(cpu, 0x0008u, 1);
        if (cpu->pending_interrupts & PENDING_NMI)
            cpu->interrupt_inhibit = 0;
    } else if (cpu->pending_interrupts & PENDING_NMI) {
        cpu->pending_interrupts &= (uint8_t)~PENDING_NMI;
        context_switch(cpu, 0xfffcu, 0);
    } else if ((cpu->pending_interrupts & PENDING_LEVEL1) && mask >= 1u) {
        cpu->pending_interrupts &= (uint8_t)~PENDING_LEVEL1;
        cpu->flags &= (uint16_t)~(1u << 2);
        context_switch(cpu, 0x0004u, 0);
    } else if ((cpu->pending_interrupts & PENDING_LEVEL3) && mask >= 3u) {
        cpu->pending_interrupts &= (uint8_t)~PENDING_LEVEL3;
        cpu->flags &= (uint16_t)~(1u << 3);
        context_switch(cpu, 0x000cu, 2);
    } else if ((cpu->pending_interrupts & PENDING_LEVEL4) && mask >= 4u) {
        cpu->pending_interrupts &= (uint8_t)~PENDING_LEVEL4;
        cpu->flags &= (uint16_t)~(1u << 4);
        context_switch(cpu, 0x0010u, 3);
    } else {
        return false;
    }
    TMS9995_ADD_CYCLES(cpu, 22u);
    return true;
}

static bool is_mid_opcode(uint16_t opcode)
{
    return opcode <= 0x007fu ||
           (opcode >= 0x00a0u && opcode <= 0x017fu) ||
           (opcode >= 0x0210u && opcode <= 0x021fu) ||
           (opcode >= 0x0230u && opcode <= 0x023fu) ||
           (opcode >= 0x0250u && opcode <= 0x025fu) ||
           (opcode >= 0x0270u && opcode <= 0x027fu) ||
           (opcode >= 0x0290u && opcode <= 0x029fu) ||
           (opcode >= 0x02b0u && opcode <= 0x02bfu) ||
           (opcode >= 0x02d0u && opcode <= 0x02dfu) ||
           (opcode >= 0x02e1u && opcode <= 0x02ffu) ||
           (opcode >= 0x0301u && opcode <= 0x033fu) ||
           (opcode >= 0x0341u && opcode <= 0x035fu) ||
           (opcode >= 0x0361u && opcode <= 0x037fu) ||
           (opcode >= 0x0381u && opcode <= 0x039fu) ||
           (opcode >= 0x03a1u && opcode <= 0x03bfu) ||
           (opcode >= 0x03c1u && opcode <= 0x03dfu) ||
           (opcode >= 0x03e1u && opcode <= 0x03ffu) ||
           (opcode >= 0x0780u && opcode <= 0x07ffu) ||
           (opcode >= 0x0c00u && opcode <= 0x0fffu);
}

static uint16_t fetch_opcode(tms9995 *cpu)
{
    uint16_t address = (uint16_t)(cpu->pc & 0xfffeu);
    uint16_t value;

    /* STM32 boards may provide a dedicated external opcode path.  Keep the
       TMS9995 on-chip RAM/decrementer windows inside the CPU core so code
       executed from internal RAM retains exactly the same semantics. */
    if (cpu->bus.read_opcode_word != NULL &&
        !((address >= 0xf000u && address <= 0xf0fau) ||
          address >= 0xfffau))
        value = cpu->bus.read_opcode_word(cpu->bus.opaque, address);
    else
        value = read_word(cpu, address, TMS9995_BUS_OPCODE_FETCH);

    cpu->pc = (uint16_t)(cpu->pc + 2u);
    return value;
}

static uint16_t fetch_argument(tms9995 *cpu)
{
    uint16_t value = read_word(cpu, cpu->pc, TMS9995_BUS_DATA_READ);
    cpu->pc = (uint16_t)(cpu->pc + 2u);
    return value;
}

static uint16_t read_reg(tms9995 *cpu, unsigned reg)
{
    return read_word(cpu, (uint16_t)(cpu->wp + 2u * (reg & 15u)),
                     TMS9995_BUS_DATA_READ);
}

uint16_t tms9995_get_register(tms9995 *cpu, unsigned reg)
{
    if (cpu == NULL || reg >= 16u)
        return 0;
    return read_reg(cpu, reg);
}

static void write_reg(tms9995 *cpu, unsigned reg, uint16_t value)
{
    write_word(cpu, (uint16_t)(cpu->wp + 2u * (reg & 15u)), value);
}

static uint16_t effective_address_sized(tms9995 *cpu, unsigned spec,
                                        bool byte_op)
{
    unsigned mode = (spec >> 4) & 3u;
    unsigned reg = spec & 15u;
    uint16_t address;

    switch (mode) {
    case 0: /* Register direct: workspace registers physically reside in RAM. */
        return (uint16_t)(cpu->wp + 2u * reg);
    case 1:
        return read_reg(cpu, reg);
    case 2:
        address = fetch_argument(cpu);
        if (reg != 0)
            address = (uint16_t)(address + read_reg(cpu, reg));
        return address;
    case 3:
        address = read_reg(cpu, reg);
        write_reg(cpu, reg, (uint16_t)(address + (byte_op ? 1u : 2u)));
        return address;
    default:
        return 0;
    }
}

static uint16_t effective_address(tms9995 *cpu, unsigned spec)
{
    return effective_address_sized(cpu, spec, false);
}

static uint16_t operand_read(tms9995 *cpu, uint16_t address, bool byte_op)
{
    /* TMS9900-family byte operands are left-justified internally.  A byte in
       a workspace register is its most-significant byte (bits 15..8). */
    return byte_op ? (uint16_t)((uint16_t)read_byte(cpu, address) << 8) :
                     read_word(cpu, address, TMS9995_BUS_DATA_READ);
}

static void operand_write(tms9995 *cpu, uint16_t address, uint16_t value,
                          bool byte_op)
{
    if (byte_op)
        write_byte(cpu, address, (uint8_t)(value >> 8));
    else
        write_word(cpu, address, value);
}

static void set_logic_flags(tms9995 *cpu, uint16_t value)
{
    cpu->st &= (uint16_t)~(TMS9995_ST_LGT | TMS9995_ST_AGT | TMS9995_ST_EQ);
    if (value != 0)
        cpu->st |= TMS9995_ST_LGT;
    if ((int16_t)value > 0)
        cpu->st |= TMS9995_ST_AGT;
    if (value == 0)
        cpu->st |= TMS9995_ST_EQ;
}

static bool odd_parity(uint8_t value)
{
    value ^= (uint8_t)(value >> 4);
    value ^= (uint8_t)(value >> 2);
    value ^= (uint8_t)(value >> 1);
    return (value & 1u) != 0;
}

static void set_byte_logic_flags(tms9995 *cpu, uint16_t value)
{
    uint8_t byte = (uint8_t)(value >> 8);
    cpu->st &= (uint16_t)~(TMS9995_ST_LGT | TMS9995_ST_AGT |
                           TMS9995_ST_EQ | TMS9995_ST_OP);
    if (byte != 0)
        cpu->st |= TMS9995_ST_LGT;
    if ((int8_t)byte > 0)
        cpu->st |= TMS9995_ST_AGT;
    if (byte == 0)
        cpu->st |= TMS9995_ST_EQ;
    if (odd_parity(byte))
        cpu->st |= TMS9995_ST_OP;
}

static void set_add_flags(tms9995 *cpu, uint16_t lhs, uint16_t rhs,
                          uint16_t result, bool byte_op, bool subtract)
{
    uint32_t mask = byte_op ? 0xffu : 0xffffu;
    uint32_t sign = byte_op ? 0x80u : 0x8000u;
    uint32_t a = byte_op ? lhs >> 8 : lhs;
    uint32_t b = byte_op ? rhs >> 8 : rhs;
    uint32_t r = byte_op ? result >> 8 : result;
    bool carry;
    bool overflow;

    if (subtract) {
        carry = a >= b;
        overflow = (((a ^ b) & (a ^ r) & sign) != 0);
    } else {
        carry = a + b > mask;
        overflow = (((~(a ^ b)) & (a ^ r) & sign) != 0);
    }
    cpu->st &= (uint16_t)~(TMS9995_ST_C | TMS9995_ST_OV);
    if (carry)
        cpu->st |= TMS9995_ST_C;
    if (overflow)
        cpu->st |= TMS9995_ST_OV;
    if (byte_op)
        set_byte_logic_flags(cpu, result);
    else
        set_logic_flags(cpu, result);
}

static void set_compare_flags(tms9995 *cpu, uint16_t destination,
                              uint16_t source, bool byte_op)
{
    cpu->st &= (uint16_t)~(TMS9995_ST_LGT | TMS9995_ST_AGT |
                           TMS9995_ST_EQ | (byte_op ? TMS9995_ST_OP : 0));
    if (byte_op) {
        uint8_t dst = (uint8_t)(destination >> 8);
        uint8_t src = (uint8_t)(source >> 8);
        if (dst > src)
            cpu->st |= TMS9995_ST_LGT;
        if ((int8_t)dst > (int8_t)src)
            cpu->st |= TMS9995_ST_AGT;
        if (dst == src)
            cpu->st |= TMS9995_ST_EQ;
        if (odd_parity(dst))
            cpu->st |= TMS9995_ST_OP;
    } else {
        if (destination > source)
            cpu->st |= TMS9995_ST_LGT;
        if ((int16_t)destination > (int16_t)source)
            cpu->st |= TMS9995_ST_AGT;
        if (destination == source)
            cpu->st |= TMS9995_ST_EQ;
    }
}

static bool condition_true(const tms9995 *cpu, unsigned operation)
{
    bool lgt = (cpu->st & TMS9995_ST_LGT) != 0;
    bool agt = (cpu->st & TMS9995_ST_AGT) != 0;
    bool eq = (cpu->st & TMS9995_ST_EQ) != 0;
    bool carry = (cpu->st & TMS9995_ST_C) != 0;
    bool overflow = (cpu->st & TMS9995_ST_OV) != 0;
    bool parity = (cpu->st & TMS9995_ST_OP) != 0;

    switch (operation) {
    case 0x10: return true;                    /* JMP */
    case 0x11: return !agt && !eq;             /* JLT */
    case 0x12: return !lgt || eq;              /* JLE */
    case 0x13: return eq;                      /* JEQ */
    case 0x14: return lgt || eq;               /* JHE */
    case 0x15: return agt;                     /* JGT */
    case 0x16: return !eq;                     /* JNE */
    case 0x17: return !carry;                  /* JNC */
    case 0x18: return carry;                   /* JOC */
    case 0x19: return !overflow;               /* JNO */
    case 0x1a: return !lgt && !eq;             /* JL */
    case 0x1b: return lgt && !eq;              /* JH */
    case 0x1c: return parity;                  /* JOP */
    default: return false;
    }
}

void tms9995_init(tms9995 *cpu, const tms9995_bus *bus)
{
    memset(cpu, 0, sizeof(*cpu));
    if (bus != NULL)
        cpu->bus = *bus;
}

bool tms9995_reset(tms9995 *cpu)
{
    if (cpu == NULL || cpu->bus.read_word == NULL)
        return false;

    cpu->wp = (uint16_t)(cpu->bus.read_word(cpu->bus.opaque, 0x0000u,
                                            TMS9995_BUS_DATA_READ) & 0xfffeu);
    cpu->pc = (uint16_t)(cpu->bus.read_word(cpu->bus.opaque, 0x0002u,
                                            TMS9995_BUS_DATA_READ) & 0xfffeu);
    cpu->st = 0;
    cpu->ir = 0;
    cpu->cycles = 0;
    cpu->flags = 0;
    cpu->decrementer_start = 0;
    cpu->decrementer_value = 0;
    cpu->decrementer_phase = 0;
    cpu->pending_interrupts = 0;
    cpu->interrupt_inhibit = 0;
    cpu->nmi_active = false;
    cpu->int1_active = false;
    cpu->int4_active = false;
    cpu->mid_flag = false;
    cpu->idle = false;
    return true;
}

tms9995_step_result tms9995_step(tms9995 *cpu)
{
    uint16_t opcode;
    uint16_t value;
    uint16_t address;
    unsigned operation;
    unsigned reg;
    int8_t displacement;
    tms9995_step_result nested_result;

    if (cpu == NULL || cpu->bus.read_word == NULL)
        return TMS9995_STEP_BUS_ERROR;
    if (!cpu->executing_x) {
        /* BLWP/XOP inhibit the maskable requests for the first instruction in
           the new context.  NMI is not maskable and must still be accepted. */
        if ((cpu->pending_interrupts & PENDING_NMI) != 0u) {
            if (service_pending_interrupt(cpu))
                return TMS9995_STEP_OK;
        } else if (cpu->interrupt_inhibit != 0) {
            --cpu->interrupt_inhibit;
        } else if (service_pending_interrupt(cpu)) {
            return TMS9995_STEP_OK;
        }
    }
    if (cpu->idle) {
        /* IDLE suppresses instruction execution, not the on-chip clock.
           Account one decrementer-clock period so the interval timer can
           reach zero and wake the processor through its level-3 request. */
        TMS9995_ADD_CYCLES(cpu, 16u);
        return TMS9995_STEP_IDLE;
    }

    if (cpu->execute_override_valid) {
        cpu->ir = cpu->execute_override;
        cpu->execute_override_valid = false;
    } else {
        cpu->ir = fetch_opcode(cpu);
    }
    opcode = cpu->ir;

    /* Immediate/register instructions. */
    operation = opcode & 0xffe0u;
    reg = opcode & 15u;
    switch (operation) {
    case 0x0200: /* LI */
        value = fetch_argument(cpu);
        write_reg(cpu, reg, value);
        set_logic_flags(cpu, value);
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    case 0x0240: /* ANDI */
        value = (uint16_t)(read_reg(cpu, reg) & fetch_argument(cpu));
        write_reg(cpu, reg, value);
        set_logic_flags(cpu, value);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    case 0x0260: /* ORI */
        value = (uint16_t)(read_reg(cpu, reg) | fetch_argument(cpu));
        write_reg(cpu, reg, value);
        set_logic_flags(cpu, value);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    case 0x02a0: /* STWP */
        write_reg(cpu, reg, cpu->wp);
        TMS9995_ADD_CYCLES(cpu, 8u);
        return TMS9995_STEP_OK;
    case 0x02c0: /* STST */
        write_reg(cpu, reg, cpu->st);
        TMS9995_ADD_CYCLES(cpu, 8u);
        return TMS9995_STEP_OK;
    case 0x02e0: /* LWPI */
        cpu->wp = (uint16_t)(fetch_argument(cpu) & 0xfffeu);
        TMS9995_ADD_CYCLES(cpu, 10u);
        return TMS9995_STEP_OK;
    case 0x0300: /* LIMI */
        cpu->st = (uint16_t)((cpu->st & ~TMS9995_ST_IM) |
                             (fetch_argument(cpu) & TMS9995_ST_IM));
        TMS9995_ADD_CYCLES(cpu, 16u);
        return TMS9995_STEP_OK;
    default:
        break;
    }

    if ((opcode & 0xfff0u) == 0x0080u) { /* LST Rn */
        cpu->st = read_reg(cpu, opcode & 15u);
        TMS9995_ADD_CYCLES(cpu, 8u);
        return TMS9995_STEP_OK;
    }
    if ((opcode & 0xfff0u) == 0x0090u) { /* LWP Rn */
        cpu->wp = (uint16_t)(read_reg(cpu, opcode & 15u) & 0xfffeu);
        TMS9995_ADD_CYCLES(cpu, 8u);
        return TMS9995_STEP_OK;
    }

    if (operation == 0x0220u) { /* AI */
        uint16_t old_value = read_reg(cpu, reg);
        uint16_t argument = fetch_argument(cpu);
        value = (uint16_t)(old_value + argument);
        write_reg(cpu, reg, value);
        set_add_flags(cpu, old_value, argument, value, false, false);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x0280u) { /* CI */
        set_compare_flags(cpu, read_reg(cpu, reg), fetch_argument(cpu), false);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    }

    switch (opcode) {
    case 0x0340: /* IDLE */
        cpu->idle = true;
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_IDLE;
    case 0x0360: /* RSET: external reset output is modeled by the board later. */
        cpu->st &= (uint16_t)~TMS9995_ST_IM;
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    case 0x0380: { /* RTWP */
        uint16_t old_wp = cpu->wp;
        cpu->st = read_word(cpu, (uint16_t)(old_wp + 30u),
                            TMS9995_BUS_DATA_READ);
        cpu->pc = (uint16_t)(read_word(cpu, (uint16_t)(old_wp + 28u),
                                      TMS9995_BUS_DATA_READ) & 0xfffeu);
        cpu->wp = (uint16_t)(read_word(cpu, (uint16_t)(old_wp + 26u),
                                      TMS9995_BUS_DATA_READ) & 0xfffeu);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    }
    case 0x03a0: /* CKON */
    case 0x03c0: /* CKOF */
    case 0x03e0: /* LREX */
        /* External control outputs are board callbacks still to be added. */
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    default:
        break;
    }

    /* Single-operand instructions use the low six bits as an operand spec. */
    operation = opcode & 0xffc0u;
    if (operation == 0x0680u) { /* BL */
        address = effective_address(cpu, opcode & 0x3fu);
        write_reg(cpu, 11u, cpu->pc);
        cpu->pc = address;
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x0440u) { /* B */
        cpu->pc = effective_address(cpu, opcode & 0x3fu);
        TMS9995_ADD_CYCLES(cpu, 8u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x0480u) { /* X */
        address = effective_address(cpu, opcode & 0x3fu);
        cpu->execute_override = read_word(cpu, address, TMS9995_BUS_DATA_READ);
        cpu->execute_override_valid = true;
        cpu->executing_x = true;
        nested_result = tms9995_step(cpu);
        cpu->executing_x = false;
        return nested_result;
    }

    if (operation == 0x04c0u) { /* CLR */
        operand_write(cpu, effective_address(cpu, opcode & 0x3fu), 0, false);
        TMS9995_ADD_CYCLES(cpu, 10u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x0400u) { /* BLWP */
        uint16_t vector = effective_address(cpu, opcode & 0x3fu);
        uint16_t old_wp = cpu->wp;
        uint16_t old_pc = cpu->pc;
        uint16_t old_st = cpu->st;
        uint16_t new_wp = read_word(cpu, vector, TMS9995_BUS_DATA_READ);
        uint16_t new_pc = read_word(cpu, (uint16_t)(vector + 2u),
                                    TMS9995_BUS_DATA_READ);
        cpu->wp = (uint16_t)(new_wp & 0xfffeu);
        cpu->pc = (uint16_t)(new_pc & 0xfffeu);
        write_reg(cpu, 13u, old_wp);
        write_reg(cpu, 14u, old_pc);
        write_reg(cpu, 15u, old_st);
        cpu->interrupt_inhibit = 1;
        TMS9995_ADD_CYCLES(cpu, 26u);
        return TMS9995_STEP_OK;
    }
    if (operation >= 0x0500u && operation <= 0x0740u) {
        address = effective_address(cpu, opcode & 0x3fu);
        value = operand_read(cpu, address, false);
        switch (operation) {
        case 0x0500: { /* NEG */
            uint16_t old_value = value;
            value = (uint16_t)(0u - value);
            operand_write(cpu, address, value, false);
            set_logic_flags(cpu, value);
            cpu->st &= (uint16_t)~(TMS9995_ST_C | TMS9995_ST_OV);
            /* NEG carry is the carry-out of 0 - operand. P2 relies on this
               while negating a 32-bit counter value one word at a time. */
            if (old_value == 0u)
                cpu->st |= TMS9995_ST_C;
            if (old_value == 0x8000u)
                cpu->st |= TMS9995_ST_OV;
            break;
        }
        case 0x0540: /* INV */
            value = (uint16_t)~value;
            operand_write(cpu, address, value, false);
            set_logic_flags(cpu, value);
            break;
        case 0x0580: { /* INC */
            uint16_t old_value = value;
            value = (uint16_t)(value + 1u);
            operand_write(cpu, address, value, false);
            set_add_flags(cpu, old_value, 1u, value, false, false);
            break;
        }
        case 0x05c0: { /* INCT */
            uint16_t old_value = value;
            value = (uint16_t)(value + 2u);
            operand_write(cpu, address, value, false);
            set_add_flags(cpu, old_value, 2u, value, false, false);
            break;
        }
        case 0x0600: { /* DEC */
            uint16_t old_value = value;
            value = (uint16_t)(value - 1u);
            operand_write(cpu, address, value, false);
            set_add_flags(cpu, old_value, 1u, value, false, true);
            break;
        }
        case 0x0640: { /* DECT */
            uint16_t old_value = value;
            value = (uint16_t)(value - 2u);
            operand_write(cpu, address, value, false);
            set_add_flags(cpu, old_value, 2u, value, false, true);
            break;
        }
        case 0x06c0: /* SWPB; status is unaffected. */
            value = (uint16_t)((value << 8) | (value >> 8));
            operand_write(cpu, address, value, false);
            break;
        case 0x0700: /* SETO; status is unaffected. */
            operand_write(cpu, address, 0xffffu, false);
            break;
        case 0x0740: { /* ABS */
            uint16_t old_value = value;
            cpu->st &= (uint16_t)~(TMS9995_ST_C | TMS9995_ST_OV);
            /* ABS sets LGT/AGT/EQ from the source operand, not from the
               absolute result.  In particular ABS >FFFF must leave AGT
               clear even though the stored result is >0001. */
            set_logic_flags(cpu, old_value);
            if ((int16_t)old_value < 0) {
                if (old_value == 0x8000u)
                    cpu->st |= TMS9995_ST_OV;
                value = (uint16_t)(0u - old_value);
                operand_write(cpu, address, value, false);
            }
            break;
        }
        default:
            return TMS9995_STEP_ILLEGAL_OPCODE;
        }
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    }

    /* Shifts. A zero encoded count uses the low nibble of R0; zero means 16. */
    operation = opcode & 0x0f00u;
    if ((opcode & 0xf000u) == 0u &&
        operation >= 0x0800u && operation <= 0x0b00u) {
        unsigned count = (opcode >> 4) & 15u;
        unsigned shift_reg = opcode & 15u;
        unsigned i;
        bool carry = false;
        bool overflow = false;
        value = read_reg(cpu, shift_reg);
        if (count == 0) {
            count = read_reg(cpu, 0u) & 15u;
            if (count == 0)
                count = 16u;
        }
        for (i = 0; i < count; ++i) {
            bool old_sign = (value & 0x8000u) != 0;
            switch (operation) {
            case 0x0800: /* SRA */
                carry = (value & 1u) != 0;
                value = (uint16_t)((value >> 1) | (value & 0x8000u));
                break;
            case 0x0900: /* SRL */
                carry = (value & 1u) != 0;
                value >>= 1;
                break;
            case 0x0a00: /* SLA */
                carry = old_sign;
                value <<= 1;
                if (((value & 0x8000u) != 0) != old_sign)
                    overflow = true;
                break;
            case 0x0b00: /* SRC */
                carry = (value & 1u) != 0;
                value = (uint16_t)((value >> 1) | (carry ? 0x8000u : 0));
                break;
            }
        }
        write_reg(cpu, shift_reg, value);
        set_logic_flags(cpu, value);
        cpu->st &= (uint16_t)~TMS9995_ST_C;
        if (carry)
            cpu->st |= TMS9995_ST_C;
        /* Only SLA defines OV. SRA, SRL and SRC leave it unchanged. */
        if (operation == 0x0a00u) {
            cpu->st &= (uint16_t)~TMS9995_ST_OV;
            if (overflow)
                cpu->st |= TMS9995_ST_OV;
        }
        TMS9995_ADD_CYCLES(cpu, 12u + 2u * count);
        return TMS9995_STEP_OK;
    }

    /* Format-3 instructions: source operand plus a workspace register. */
    operation = opcode & 0xfc00u;
    if (operation == 0x2c00u) { /* XOP */
        unsigned trap = (opcode >> 6) & 15u;
        uint16_t source_address = effective_address(cpu, opcode & 0x3fu);
        uint16_t old_wp = cpu->wp;
        uint16_t old_pc = cpu->pc;
        uint16_t old_st = cpu->st;
        uint16_t vector = (uint16_t)(0x0040u + 4u * trap);
        cpu->wp = (uint16_t)(read_word(cpu, vector, TMS9995_BUS_DATA_READ) &
                             0xfffeu);
        cpu->pc = (uint16_t)(read_word(cpu, (uint16_t)(vector + 2u),
                                      TMS9995_BUS_DATA_READ) & 0xfffeu);
        write_reg(cpu, 11u, source_address);
        write_reg(cpu, 13u, old_wp);
        write_reg(cpu, 14u, old_pc);
        write_reg(cpu, 15u, old_st);
        cpu->st |= TMS9995_ST_X;
        cpu->interrupt_inhibit = 1;
        TMS9995_ADD_CYCLES(cpu, 30u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x3000u || operation == 0x3400u) { /* LDCR/STCR */
        unsigned count = (opcode >> 6) & 15u;
        unsigned i;
        bool byte_op;
        uint16_t cru_base = (uint16_t)(read_reg(cpu, 12u) >> 1);
        uint16_t operand_address;
        uint16_t transferred = 0;
        if (count == 0)
            count = 16u;
        byte_op = count <= 8u;
        operand_address = effective_address_sized(cpu, opcode & 0x3fu,
                                                  byte_op);
        if (operation == 0x3000u) {
            transferred = operand_read(cpu, operand_address, byte_op);
            if (byte_op)
                transferred >>= 8;
            for (i = 0; i < count; ++i)
                cru_write(cpu, (uint16_t)(cru_base + i),
                          ((transferred >> i) & 1u) != 0);
        } else {
            for (i = 0; i < count; ++i) {
                if (cru_read(cpu, (uint16_t)(cru_base + i)))
                    transferred |= (uint16_t)(1u << i);
            }
            operand_write(cpu, operand_address,
                          byte_op ? (uint16_t)(transferred << 8) : transferred,
                          byte_op);
        }
        if (byte_op)
            set_byte_logic_flags(cpu, (uint16_t)(transferred << 8));
        else
            set_logic_flags(cpu, transferred);
        TMS9995_ADD_CYCLES(cpu, 20u + 2u * count);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x2000u || operation == 0x2400u ||
        operation == 0x2800u || operation == 0x3800u ||
        operation == 0x3c00u) {
        unsigned destination_reg = (opcode >> 6) & 15u;
        uint16_t source_address = effective_address(cpu, opcode & 0x3fu);
        uint16_t source = operand_read(cpu, source_address, false);
        uint16_t destination = read_reg(cpu, destination_reg);
        switch (operation) {
        case 0x2000: /* COC */
            if ((source & destination) == source)
                cpu->st |= TMS9995_ST_EQ;
            else
                cpu->st &= (uint16_t)~TMS9995_ST_EQ;
            break;
        case 0x2400: /* CZC */
            if ((source & destination) == 0)
                cpu->st |= TMS9995_ST_EQ;
            else
                cpu->st &= (uint16_t)~TMS9995_ST_EQ;
            break;
        case 0x2800: /* XOR */
            value = (uint16_t)(destination ^ source);
            write_reg(cpu, destination_reg, value);
            set_logic_flags(cpu, value);
            break;
        case 0x3800: { /* MPY: unsigned Rn:Rn+1 result */
            uint32_t product = (uint32_t)destination * source;
            write_reg(cpu, destination_reg, (uint16_t)(product >> 16));
            /* R15:R16 is legal electrically: the second word follows the
               workspace instead of wrapping around to R0. */
            write_word(cpu,
                       (uint16_t)(cpu->wp + 2u * (destination_reg + 1u)),
                       (uint16_t)product);
            break;
        }
        case 0x3c00: { /* DIV: unsigned Rn:Rn+1 dividend */
            uint32_t dividend;
            if (source <= destination) {
                cpu->st |= TMS9995_ST_OV;
                break;
            }
            cpu->st &= (uint16_t)~TMS9995_ST_OV;
            dividend = ((uint32_t)destination << 16) |
                       read_word(cpu,
                                 (uint16_t)(cpu->wp +
                                            2u * (destination_reg + 1u)),
                                 TMS9995_BUS_DATA_READ);
            write_reg(cpu, destination_reg, (uint16_t)(dividend / source));
            write_word(cpu,
                       (uint16_t)(cpu->wp + 2u * (destination_reg + 1u)),
                       (uint16_t)(dividend % source));
            break;
        }
        }
        TMS9995_ADD_CYCLES(cpu, 20u);
        return TMS9995_STEP_OK;
    }

    /* TMS9995 signed arithmetic extensions use R0:R1. */
    operation = opcode & 0xffc0u;
    if (operation == 0x01c0u) { /* MPYS */
        int32_t product;
        int16_t source = (int16_t)operand_read(
            cpu, effective_address(cpu, opcode & 0x3fu), false);
        product = (int32_t)(int16_t)read_reg(cpu, 0u) * source;
        write_reg(cpu, 0u, (uint16_t)((uint32_t)product >> 16));
        write_reg(cpu, 1u, (uint16_t)product);
        cpu->st &= (uint16_t)~(TMS9995_ST_LGT | TMS9995_ST_AGT |
                               TMS9995_ST_EQ);
        if (product != 0)
            cpu->st |= TMS9995_ST_LGT;
        if (product > 0)
            cpu->st |= TMS9995_ST_AGT;
        if (product == 0)
            cpu->st |= TMS9995_ST_EQ;
        TMS9995_ADD_CYCLES(cpu, 30u);
        return TMS9995_STEP_OK;
    }
    if (operation == 0x0180u) { /* DIVS */
        int16_t divisor = (int16_t)operand_read(
            cpu, effective_address(cpu, opcode & 0x3fu), false);
        int32_t dividend = (int32_t)(((uint32_t)read_reg(cpu, 0u) << 16) |
                                     read_reg(cpu, 1u));
        int64_t quotient;
        int64_t remainder;
        if (divisor == 0) {
            cpu->st |= TMS9995_ST_OV;
            return TMS9995_STEP_OK;
        }
        quotient = (int64_t)dividend / divisor;
        remainder = (int64_t)dividend % divisor;
        if (quotient < -32768 || quotient > 32767) {
            cpu->st |= TMS9995_ST_OV;
            return TMS9995_STEP_OK;
        }
        cpu->st &= (uint16_t)~TMS9995_ST_OV;
        write_reg(cpu, 0u, (uint16_t)(int16_t)quotient);
        write_reg(cpu, 1u, (uint16_t)(int16_t)remainder);
        set_logic_flags(cpu, (uint16_t)(int16_t)quotient);
        TMS9995_ADD_CYCLES(cpu, 39u);
        return TMS9995_STEP_OK;
    }

    /* Two-operand word and byte instructions. */
    operation = opcode >> 12;
    if (operation >= 4u) {
        bool byte_op = (operation & 1u) != 0;
        uint16_t source_address = effective_address_sized(
            cpu, opcode & 0x3fu, byte_op);
        uint16_t source = operand_read(cpu, source_address, byte_op);
        uint16_t destination_address = effective_address_sized(
            cpu, (opcode >> 6) & 0x3fu, byte_op);
        uint16_t destination = 0;
        uint16_t result = destination;
        uint16_t mask = byte_op ? 0xff00u : 0xffffu;

        /* MOV/MOVB only drive a write cycle at the destination.  Reading it
           first is observably wrong for memory-mapped devices such as the
           uPD7220 ports. */
        if (operation != 0xcu && operation != 0xdu) {
            destination = operand_read(cpu, destination_address, byte_op);
            result = destination;
        }

        switch (operation) {
        case 0x4: case 0x5: /* SZC/SZCB */
            result = (uint16_t)(destination & ~source & mask);
            operand_write(cpu, destination_address, result, byte_op);
            break;
        case 0x6: case 0x7: /* S/SB */
            result = (uint16_t)((destination - source) & mask);
            operand_write(cpu, destination_address, result, byte_op);
            set_add_flags(cpu, destination, source, result, byte_op, true);
            TMS9995_ADD_CYCLES(cpu, 14u);
            return TMS9995_STEP_OK;
        case 0x8: case 0x9: /* C/CB */
            /* TI defines the status relationship as source vs destination
               (unlike the subtraction-style operand order often assumed for
               other CPU families). */
            set_compare_flags(cpu, source, destination, byte_op);
            TMS9995_ADD_CYCLES(cpu, 14u);
            return TMS9995_STEP_OK;
        case 0xa: case 0xb: /* A/AB */
            result = (uint16_t)((destination + source) & mask);
            operand_write(cpu, destination_address, result, byte_op);
            set_add_flags(cpu, destination, source, result, byte_op, false);
            TMS9995_ADD_CYCLES(cpu, 14u);
            return TMS9995_STEP_OK;
        case 0xc: case 0xd: /* MOV/MOVB */
            result = source;
            operand_write(cpu, destination_address, result, byte_op);
            break;
        case 0xe: case 0xf: /* SOC/SOCB */
            result = (uint16_t)((destination | source) & mask);
            operand_write(cpu, destination_address, result, byte_op);
            break;
        default:
            return TMS9995_STEP_ILLEGAL_OPCODE;
        }
        if (byte_op)
            set_byte_logic_flags(cpu, result);
        else
            set_logic_flags(cpu, result);
        TMS9995_ADD_CYCLES(cpu, 14u);
        return TMS9995_STEP_OK;
    }

    /* Relative branches and CRU single-bit operations. */
    operation = opcode >> 8;
    displacement = (int8_t)(opcode & 0xffu);
    if (operation >= 0x10u && operation <= 0x1cu) {
        if (condition_true(cpu, operation))
            cpu->pc = (uint16_t)(cpu->pc + 2 * (int)displacement);
        TMS9995_ADD_CYCLES(cpu, 10u);
        return TMS9995_STEP_OK;
    }
    if (operation >= 0x1du && operation <= 0x1fu) {
        uint16_t cru_address = (uint16_t)((read_reg(cpu, 12u) >> 1) +
                                          (int)displacement);
        if (operation == 0x1fu) { /* TB */
            if (cru_read(cpu, cru_address))
                cpu->st |= TMS9995_ST_EQ;
            else
                cpu->st &= (uint16_t)~TMS9995_ST_EQ;
        } else {
            cru_write(cpu, cru_address, operation == 0x1du); /* SBO / SBZ */
        }
        TMS9995_ADD_CYCLES(cpu, 12u);
        return TMS9995_STEP_OK;
    }

    if (is_mid_opcode(opcode)) {
        if (cpu->mid_count == 0u) {
            cpu->first_mid_pc = (uint16_t)(cpu->pc - 2u);
            cpu->first_mid_opcode = opcode;
        }
        cpu->last_mid_pc = (uint16_t)(cpu->pc - 2u);
        cpu->last_mid_opcode = opcode;
        ++cpu->mid_count;
        cpu->pending_interrupts |= PENDING_MID;
        cpu->mid_flag = true;
        return TMS9995_STEP_OK;
    }
    return TMS9995_STEP_ILLEGAL_OPCODE;
}

void tms9995_set_interrupt_line(tms9995 *cpu, tms9995_interrupt_line line,
                                bool asserted)
{
    bool was_active;

    if (cpu == NULL)
        return;

    switch (line) {
    case TMS9995_INTERRUPT_NMI:
        was_active = cpu->nmi_active;
        cpu->nmi_active = asserted;
        if (asserted && !was_active)
            cpu->pending_interrupts |= PENDING_NMI;
        else if (!asserted)
            cpu->pending_interrupts &= (uint8_t)~PENDING_NMI;
        break;

    case TMS9995_INTERRUPT_LEVEL1:
        was_active = cpu->int1_active;
        cpu->int1_active = asserted;
        if (asserted && !was_active) {
            cpu->flags |= (uint16_t)(1u << 2);
            cpu->pending_interrupts |= PENDING_LEVEL1;
        }
        break;

    case TMS9995_INTERRUPT_LEVEL4:
        was_active = cpu->int4_active;
        cpu->int4_active = asserted;

        if ((cpu->flags & 3u) == 3u) {
            /* Enabled event-counter mode: INT4/EC is not a Level-4 input.
               Count exactly one decrement on each physical high-to-low edge,
               represented by false -> true in this semantic API. */
            cpu->flags &= (uint16_t)~(1u << 4);
            cpu->pending_interrupts &= (uint8_t)~PENDING_LEVEL4;
            if (asserted && !was_active)
                tms9995_decrementer_event(cpu);
        } else if (asserted && !was_active) {
            /* Timer mode, or decrementer disabled: INT4/EC is the normal
               pulse/level-sensitive Level-4 request input. */
            cpu->flags |= (uint16_t)(1u << 4);
            cpu->pending_interrupts |= PENDING_LEVEL4;
        }
        break;

    default:
        break;
    }
}

void tms9995_decrementer_event(tms9995 *cpu)
{
    if (cpu == NULL || (cpu->flags & 0x0002u) == 0u ||
        cpu->decrementer_value == 0u)
        return;
    if (--cpu->decrementer_value == 0u) {
        cpu->flags |= (uint16_t)(1u << 3);
        cpu->pending_interrupts |= PENDING_LEVEL3;
        cpu->decrementer_value = cpu->decrementer_start;
    }
}

void tms9995_advance_clock(tms9995 *cpu, unsigned input_clock_cycles)
{
    unsigned total;
    unsigned ticks;
    if (cpu == NULL || (cpu->flags & 0x0002u) == 0 ||
        (cpu->flags & 0x0001u) != 0)
        return;
    /* cpu.cycles is counted in TMS9995 input-clock periods.  CLKOUT is the
       input clock divided by four, and the internal decrementer advances
       once every four CLKOUT periods: one tick per 16 cpu.cycles. */
    total = cpu->decrementer_phase + input_clock_cycles;
    ticks = total / 16u;
    cpu->decrementer_phase = (uint8_t)(total & 15u);
    while (ticks-- != 0)
        tms9995_decrementer_event(cpu);
}

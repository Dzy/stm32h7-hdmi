#include "tnc155/machine.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_name(uint8_t mode)
{
    if (mode == TNC155_GDC_MODE_GRAPHICS) return "graphics";
    if (mode == TNC155_GDC_MODE_MIXED) return "mixed";
    return "character";
}

static void print_pram(const tnc155_upd7220 *gdc)
{
    unsigned area;
    fputs(" PRAM", stdout);
    for (area = 0u; area < 4u; ++area) {
        unsigned b = area * 4u;
        uint32_t sad = (uint32_t)gdc->parameter_ram[b] |
            ((uint32_t)gdc->parameter_ram[b + 1u] << 8) |
            ((uint32_t)(gdc->parameter_ram[b + 2u] & 3u) << 16);
        uint16_t len = (uint16_t)((gdc->parameter_ram[b + 2u] >> 4) |
            ((uint16_t)(gdc->parameter_ram[b + 3u] & 0x3fu) << 4));
        printf(" A%u:SAD=%05" PRIX32 "/LEN=%03X", area, sad, len);
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    tnc155_machine machine;
    uint64_t limit = 1000000u;
    char *end = NULL;
    bool ok;
    bool visualization_trace = false;
    bool force_visualization_context = false;
    unsigned trace_lines = 0u;
    unsigned trace_task_entries = 0u;
    int argi = 1;
    if (argi < argc && (strcmp(argv[argi], "--visualization-trace") == 0 ||
                        strcmp(argv[argi], "--visualization-trace-force") == 0)) {
        visualization_trace = true;
        force_visualization_context =
            strcmp(argv[argi], "--visualization-trace-force") == 0;
        ++argi;
    }
    if (argc - argi > 1) {
        fprintf(stderr, "usage: %s [--visualization-trace|--visualization-trace-force] [combined-instruction-limit]\n", argv[0]);
        return 2;
    }
    if (argi < argc) {
        errno = 0;
        limit = strtoull(argv[argi], &end, 0);
        if (errno || end == argv[argi] || *end != '\0') {
            fprintf(stderr, "invalid instruction limit: %s\n", argv[argi]);
            return 2;
        }
    }
    if (!tnc155_machine_init(&machine)) {
        fputs("TNC 155 machine reset failed\n", stderr);
        return 1;
    }
    printf("TNC 155 reset: MAIN WP=>%04X PC=>%04X; CLP WP=>%04X PC=>%04X\n",
           machine.main.cpu.wp, machine.main.cpu.pc,
           machine.clp.cpu.wp, machine.clp.cpu.pc);
    if (!visualization_trace) {
        ok = tnc155_machine_run(&machine, limit);
    } else {
        uint64_t step;
        bool tracing = false;
        ok = true;
        puts("VIS trace: waiting for P2 >5B4C graphics-task entry");
        for (step = 0u; step < limit; ++step) {
            uint32_t fifo_processed = machine.clp.gdc.fifo_entries_processed;
            uint32_t figure_draws = machine.clp.gdc.figure_draws;
            uint32_t character_draws = machine.clp.gdc.graphics_character_draws;
            uint32_t words_written = machine.clp.gdc.words_written;
            uint32_t cursor = machine.clp.gdc.cursor;
            uint32_t ic_reads = machine.clp.graphics_dram_reads;
            uint32_t ic_writes = machine.clp.graphics_dram_writes;
            uint32_t page_writes = machine.clp.graphics_dram_page_writes;
            bool stepped_main = false;
            if (force_visualization_context && step == 1000000u &&
                trace_task_entries == 0u) {
                /* Diagnostic-only entry through the exact ROM context record
                   at >3412: [WP=>F040, PC=>5B4C, ST=>0003].  Normal machine
                   execution never takes this path unless this explicit CLI
                   validation option is selected. */
                machine.clp.cpu.wp = 0xf040u;
                machine.clp.cpu.pc = 0x5b4cu;
                machine.clp.cpu.st = 0x0003u;
                machine.clp.cpu.idle = false;
                puts("VIS diagnostic: installed ROM context >3412 after warm-up");
            }
            if (machine.clp.cpu.pc == 0x5b4cu) {
                tracing = true;
                ++trace_task_entries;
                printf("VIS step=%" PRIu64 " P2-PC=>5B4C task-entry #%u\n",
                       step, trace_task_entries);
            }
            {
                tms9995_step_result result =
                    tnc155_machine_step(&machine, &stepped_main);
                if (result != TMS9995_STEP_OK && result != TMS9995_STEP_IDLE) {
                ok = false;
                break;
                }
            }
            if (!tracing || trace_lines >= 4000u)
                continue;
            if (machine.clp.graphics_dram_page_writes != page_writes) {
                printf("VIS P2-PC=>%04X IC31.1-BANK=>%02X\n",
                       machine.clp.graphics_dram_page_last_pc,
                       machine.clp.graphics_dram_page);
                ++trace_lines;
            }
            if (machine.clp.graphics_dram_reads != ic_reads ||
                machine.clp.graphics_dram_writes != ic_writes) {
                printf("VIS P2-PC=>%04X IC31.1 %c [%04X]=%02X bank=%02X\n",
                       machine.clp.graphics_dram_last_pc,
                       machine.clp.graphics_dram_last_write ? 'W' : 'R',
                       machine.clp.graphics_dram_last_address,
                       machine.clp.graphics_dram_last_value,
                       machine.clp.graphics_dram_page);
                ++trace_lines;
            }
            if (machine.clp.gdc.fifo_entries_processed != fifo_processed) {
                uint8_t data = machine.clp.gdc.last_fifo_data;
                if (machine.clp.gdc.last_fifo_tag != 0u) {
                    printf("VIS P2-PC=>%04X GDC CMD=%02X fifo=%u mode=%s EAD=%05" PRIX32
                           " MASK=%04X display=%s\n",
                           machine.clp.gdc.host_pc, data,
                           machine.clp.gdc.fifo_count,
                           mode_name(machine.clp.gdc.display_mode),
                           machine.clp.gdc.cursor, machine.clp.gdc.mask,
                           machine.clp.gdc.display_enabled ? "on" : "blank");
                    ++trace_lines;
                    if ((data & 0xf0u) == 0x70u)
                        print_pram(&machine.clp.gdc);
                } else if (machine.clp.gdc.command == 0x49u ||
                           machine.clp.gdc.command == 0x4au ||
                           machine.clp.gdc.command == 0x4cu ||
                           (machine.clp.gdc.command & 0xf0u) == 0x70u ||
                           machine.clp.gdc.command == 0x00u ||
                           (machine.clp.gdc.command & 0xfeu) == 0x0eu) {
                    printf("VIS P2-PC=>%04X GDC PAR[%u]=%02X cmd=%02X fifo=%u\n",
                           machine.clp.gdc.host_pc,
                           machine.clp.gdc.parameter_count - 1u, data,
                           machine.clp.gdc.command, machine.clp.gdc.fifo_count);
                    ++trace_lines;
                    if ((machine.clp.gdc.command & 0xf0u) == 0x70u)
                        print_pram(&machine.clp.gdc);
                    if (machine.clp.gdc.command == 0x4cu &&
                        machine.clp.gdc.parameter_count == 11u)
                        printf("VIS FIGS TYPE=%u DIR=%u DC=%04X D=%04X D1=%04X D2=%04X DM=%04X\n",
                               machine.clp.gdc.figure_type,
                               machine.clp.gdc.figure_direction,
                               machine.clp.gdc.figure_count,
                               machine.clp.gdc.figure_d,
                               machine.clp.gdc.figure_d1,
                               machine.clp.gdc.figure_d2,
                               machine.clp.gdc.figure_dm);
                }
            }
            if (machine.clp.gdc.figure_draws != figure_draws ||
                machine.clp.gdc.graphics_character_draws != character_draws) {
                printf("VIS DRAW=%s start-EAD=%05" PRIX32 " end-EAD=%05" PRIX32
                       " words=+%" PRIu32 " last[%05" PRIX32 "]=%04X\n",
                       machine.clp.gdc.figure_draws != figure_draws ? "FIGD" : "GCHRD",
                       cursor, machine.clp.gdc.cursor,
                       machine.clp.gdc.words_written - words_written,
                       machine.clp.gdc.last_vram_word,
                       machine.clp.gdc.last_vram_value);
                ++trace_lines;
            }
        }
        printf("VIS summary: task-entries=%u trace-lines=%u mode=%s active=%ux%u"
               " IC31.1 pages=%" PRIu32 " r/w=%" PRIu32 "/%" PRIu32
               " FIGD=%" PRIu32 " GCHRD=%" PRIu32 " VRAM-words=%" PRIu32
               " PRAM0=%02X%02X%02X%02X\n",
               trace_task_entries, trace_lines,
               mode_name(machine.clp.gdc.display_mode),
               machine.clp.gdc.active_words * 16u, machine.clp.gdc.active_lines,
               machine.clp.graphics_dram_page_writes,
               machine.clp.graphics_dram_reads, machine.clp.graphics_dram_writes,
               machine.clp.gdc.figure_draws,
               machine.clp.gdc.graphics_character_draws,
               machine.clp.gdc.words_written,
               machine.clp.gdc.parameter_ram[0], machine.clp.gdc.parameter_ram[1],
               machine.clp.gdc.parameter_ram[2], machine.clp.gdc.parameter_ram[3]);
    }
    printf("MAIN: instructions=%" PRIu64 " WP=>%04X PC=>%04X ST=>%04X"
           " cycles=%" PRIu64 " PLC-steps=%" PRIu32 " unmapped=%" PRIu32 "\n",
           machine.main_instructions, machine.main.cpu.wp, machine.main.cpu.pc,
           machine.main.cpu.st, machine.main.cpu.cycles, machine.main.plc_steps,
           machine.main.unmapped_reads);
    printf("CLP:  instructions=%" PRIu64 " WP=>%04X PC=>%04X ST=>%04X"
           " cycles=%" PRIu64 " GDC-r=%" PRIu32 " GDC-w=%" PRIu32
           " FIFO=%u high=%u processed=%" PRIu32 " overruns=%" PRIu32
           " unmapped=%" PRIu32,
           machine.clp_instructions, machine.clp.cpu.wp, machine.clp.cpu.pc,
           machine.clp.cpu.st, machine.clp.cpu.cycles, machine.clp.gdc_reads,
           machine.clp.gdc_writes, (unsigned)machine.clp.gdc.fifo_count,
           (unsigned)machine.clp.gdc.fifo_high_watermark,
           machine.clp.gdc.fifo_entries_processed,
           machine.clp.gdc.fifo_overruns, machine.clp.unmapped_reads);
    if (machine.clp.unmapped_reads)
        printf(" last=>%04X", machine.clp.last_unmapped_address);
    putchar('\n');
    if (visualization_trace)
        printf("VIS timer: decrementer start=>%04X value=>%04X pending=>%02X\n",
               machine.clp.cpu.decrementer_start,
               machine.clp.cpu.decrementer_value,
               machine.clp.cpu.pending_interrupts);
    return ok ? 0 : 3;
}

#include "tnc155/machine.h"
#include "tnc155/video.h"
#include "tnc155/panel_keys.h"
#include "tnc155/roms.h"
#include "tnc155/serial_keyboard.h"
#include "tnc155_default_user_ram.h"

#include <SDL.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_state(const tnc155_machine *m)
{
    printf("MAIN: instructions=%" PRIu64 " WP=>%04X PC=>%04X ST=>%04X cycles=%" PRIu64 "\n",
           m->main_instructions, m->main.cpu.wp, m->main.cpu.pc,
           m->main.cpu.st, m->main.cpu.cycles);
    printf("CLP:  instructions=%" PRIu64 " WP=>%04X PC=>%04X ST=>%04X cycles=%" PRIu64
           " GDC-r/w=%" PRIu32 "/%" PRIu32 " unmapped=%" PRIu32 "\n",
           m->clp_instructions, m->clp.cpu.wp, m->clp.cpu.pc, m->clp.cpu.st,
           m->clp.cpu.cycles, m->clp.gdc_reads, m->clp.gdc_writes,
           m->clp.unmapped_reads);
    printf("PLC: steps=%" PRIu32 " instructions=%" PRIu64 " PC=>%04X\n",
           m->main.plc_steps, m->main.plc.instructions, m->main.plc.pc);
    printf("DAC: %c%03X S/H X=%d Y=%d Z=%d IV=%d S=%d\n",
           m->clp.analog.dac_negative ? '-' : '+',
           (unsigned)m->clp.analog.dac_magnitude,
           (int)m->clp.analog.held_dac[0], (int)m->clp.analog.held_dac[1],
           (int)m->clp.analog.held_dac[2], (int)m->clp.analog.held_dac[3],
           (int)m->clp.analog.held_dac[4]);
    printf("MAIN HOLD: %s source=CLP-SHARED-RAM held-steps=%" PRIu64 "\n",
           m->main_hold_asserted ? "asserted" : "released",
           m->main_hold_steps);
    printf("SHARED: gate=%u accessed=%u FA16=>%04X denied-r/w=%" PRIu32 "/%" PRIu32 "\n",
           m->clp.shared_ram_enabled ? 1u : 0u,
           m->clp.shared_ram_accessed ? 1u : 0u,
           (unsigned)((uint16_t)m->main.ram[0x1a16u] << 8 |
                      m->main.ram[0x1a17u]),
           m->clp.shared_ram_denied_reads,
           m->clp.shared_ram_denied_writes);
    printf("EMERGENCY: +24V=%u CTRL=%u J1/8=%u E8=%u MAIN-Q=%u CLP-Q=%u\n",
           (unsigned)tnc155_io_backplane_control_voltage_24v(&m->io_backplane),
           (unsigned)tnc155_io_backplane_control_voltage_enabled(&m->io_backplane),
           (unsigned)tnc155_io_backplane_emergency_stop_contact_closed(&m->io_backplane),
           (unsigned)tnc155_io_backplane_read_input_terminal(&m->io_backplane, 8u),
           (unsigned)m->main.emergency_monoflop_q,
           (unsigned)m->clp.emergency_monoflop_q);
    printf("uPD7220: display=%s pitch=%u words=%" PRIu32
           " FIFO=%u processed=%" PRIu32 " overruns=%" PRIu32 "\n",
           m->clp.gdc.display_enabled ? "on" : "off",
           (unsigned)m->clp.gdc.pitch, m->clp.gdc.words_written,
           (unsigned)m->clp.gdc.fifo_count,
           m->clp.gdc.fifo_entries_processed, m->clp.gdc.fifo_overruns);
    printf("P8279: fifo=%u irq=%u keys=%" PRIu32 "/%" PRIu32
           " last=%02X/%02X\n",
           (unsigned)m->main.keyboard.fifo_count,
           (unsigned)m->main.keyboard.irq,
           m->main.keyboard.keys_accepted, m->main.keyboard.keys_read,
           (unsigned)m->main.keyboard.last_key_accepted,
           (unsigned)m->main.keyboard.last_key_read);
}

static bool inject_sdl_key(tnc155_machine *machine, SDL_Keycode key)
{
    uint8_t code;

    switch (key) {
    case SDLK_BACKSPACE:
    case SDLK_DELETE: code = TNC155_RAW_KEY_CE; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: code = TNC155_RAW_KEY_ENT; break;
    case SDLK_F12: code = TNC155_RAW_KEY_STOP; break;
    case SDLK_DOWN: code = TNC155_RAW_KEY_DOWN; break;
    case SDLK_UP: code = TNC155_RAW_KEY_UP; break;
    case SDLK_LEFT: code = TNC155_RAW_KEY_LEFT; break;
    case SDLK_RIGHT: code = TNC155_RAW_KEY_RIGHT; break;
    case SDLK_p: code = TNC155_RAW_KEY_P; break;
    case SDLK_x: code = TNC155_RAW_KEY_X; break;
    case SDLK_y: code = TNC155_RAW_KEY_Y; break;
    case SDLK_z: code = TNC155_RAW_KEY_Z; break;
    case SDLK_i: code = TNC155_RAW_KEY_IV; break;
    case SDLK_0:
    case SDLK_KP_0: code = TNC155_RAW_KEY_0; break;
    case SDLK_1:
    case SDLK_KP_1: code = TNC155_RAW_KEY_1; break;
    case SDLK_2:
    case SDLK_KP_2: code = TNC155_RAW_KEY_2; break;
    case SDLK_3:
    case SDLK_KP_3: code = TNC155_RAW_KEY_3; break;
    case SDLK_4:
    case SDLK_KP_4: code = TNC155_RAW_KEY_4; break;
    case SDLK_5:
    case SDLK_KP_5: code = TNC155_RAW_KEY_5; break;
    case SDLK_6:
    case SDLK_KP_6: code = TNC155_RAW_KEY_6; break;
    case SDLK_7:
    case SDLK_KP_7: code = TNC155_RAW_KEY_7; break;
    case SDLK_8:
    case SDLK_KP_8: code = TNC155_RAW_KEY_8; break;
    case SDLK_9:
    case SDLK_KP_9: code = TNC155_RAW_KEY_9; break;
    case SDLK_PERIOD:
    case SDLK_KP_PERIOD: code = TNC155_RAW_KEY_DECIMAL; break;
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
    case SDLK_MINUS:
    case SDLK_KP_MINUS: code = TNC155_RAW_KEY_PLUSMINUS; break;
    default: return false;
    }
    return tnc155_i8279_push_key(&machine->main.keyboard, code);
}

static int run_graphical(tnc155_machine *machine,
                         const char *serial_keyboard_device)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Window *debug_window = NULL;
    SDL_Renderer *debug_renderer = NULL;
    SDL_Texture *debug_texture = NULL;
    uint32_t *pixels = NULL;
    uint32_t *debug_pixels = NULL;
    bool running = true;
    bool debug_overlay = false;
    tnc155_serial_keyboard serial_keyboard;
    uint64_t host_start;
    uint64_t host_frequency;
    uint64_t emulated_start_cycles;
    Uint32 main_window_id = 0u;
    Uint32 debug_window_id = 0u;
    unsigned current_video_height = TNC155_VIDEO_TEXT_HEIGHT;
    int result = 0;

    tnc155_serial_keyboard_init(&serial_keyboard);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("HEIDENHAIN TNC 155 emulator",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              TNC155_VIDEO_WIDTH * 2,
                              TNC155_VIDEO_TEXT_HEIGHT * 2, 0);
    if (window != NULL)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED |
                                                  SDL_RENDERER_PRESENTVSYNC);
    if (window != NULL)
        main_window_id = SDL_GetWindowID(window);
    if (renderer == NULL && window != NULL)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer != NULL)
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    TNC155_VIDEO_WIDTH, TNC155_VIDEO_HEIGHT);
    pixels = malloc((size_t)TNC155_VIDEO_WIDTH * TNC155_VIDEO_HEIGHT *
                    sizeof(*pixels));

    debug_window = SDL_CreateWindow("TNC 155 diagnostics",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    TNC155_DEBUG_WIDTH, TNC155_DEBUG_HEIGHT,
                                    SDL_WINDOW_HIDDEN);
    if (debug_window != NULL)
        debug_renderer = SDL_CreateRenderer(debug_window, -1,
                                             SDL_RENDERER_ACCELERATED);
    if (debug_window != NULL)
        debug_window_id = SDL_GetWindowID(debug_window);
    if (debug_renderer == NULL && debug_window != NULL)
        debug_renderer = SDL_CreateRenderer(debug_window, -1,
                                             SDL_RENDERER_SOFTWARE);
    if (debug_renderer != NULL)
        debug_texture = SDL_CreateTexture(debug_renderer,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          TNC155_DEBUG_WIDTH,
                                          TNC155_DEBUG_HEIGHT);
    debug_pixels = malloc((size_t)TNC155_DEBUG_WIDTH * TNC155_DEBUG_HEIGHT *
                          sizeof(*debug_pixels));
    if (window == NULL || renderer == NULL || texture == NULL || pixels == NULL) {
        fprintf(stderr, "SDL video creation failed: %s\n", SDL_GetError());
        result = 1;
        goto done;
    }

    SDL_RenderSetLogicalSize(renderer, TNC155_VIDEO_WIDTH,
                             (int)current_video_height);
    host_frequency = SDL_GetPerformanceFrequency();
    host_start = SDL_GetPerformanceCounter();
    emulated_start_cycles = machine->main.cpu.cycles;
    (void)tnc155_serial_keyboard_open(&serial_keyboard,
                                      serial_keyboard_device);

    while (running) {
        SDL_Event event;
        uint64_t elapsed_ticks;
        uint64_t target_cycles;
        uint64_t slowest_cycles;

        tnc155_serial_keyboard_poll(&serial_keyboard,
                                    &machine->main.keyboard);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE) {
                if (event.window.windowID == main_window_id)
                    running = false;
                else if (event.window.windowID == debug_window_id) {
                    debug_overlay = false;
                    SDL_HideWindow(debug_window);
                }
            } else if (event.type == SDL_QUIT ||
                       (event.type == SDL_KEYDOWN &&
                        event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                       event.key.keysym.sym == SDLK_F1) {
                tnc155_machine_set_control_voltage_enabled(machine, true);
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                       event.key.keysym.sym == SDLK_F10) {
                debug_overlay = !debug_overlay;
                if (debug_window != NULL) {
                    if (debug_overlay)
                        SDL_ShowWindow(debug_window);
                    else
                        SDL_HideWindow(debug_window);
                }
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_F2) {
                tnc155_io_backplane_set_start_button(&machine->io_backplane,
                                                      true);
            } else if (event.type == SDL_KEYUP &&
                       event.key.keysym.sym == SDLK_F2) {
                tnc155_io_backplane_set_start_button(&machine->io_backplane,
                                                      false);
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_F3) {
                tnc155_io_backplane_set_stop_button(&machine->io_backplane,
                                                     true);
            } else if (event.type == SDL_KEYUP &&
                       event.key.keysym.sym == SDLK_F3) {
                tnc155_io_backplane_set_stop_button(&machine->io_backplane,
                                                     false);
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                (void)inject_sdl_key(machine, event.key.keysym.sym);
            }
        }

        elapsed_ticks = SDL_GetPerformanceCounter() - host_start;
        target_cycles = emulated_start_cycles +
            (elapsed_ticks * TNC155_MAIN_CPU_CLOCK_HZ) / host_frequency;
        slowest_cycles = machine->main.cpu.cycles < machine->clp.cpu.cycles ?
                         machine->main.cpu.cycles : machine->clp.cpu.cycles;
        if (target_cycles > slowest_cycles + TNC155_MAIN_CPU_CLOCK_HZ / 10u)
            target_cycles = slowest_cycles + TNC155_MAIN_CPU_CLOCK_HZ / 10u;
        while (running && (machine->main.cpu.cycles < target_cycles ||
                           machine->clp.cpu.cycles < target_cycles)) {
            if (!tnc155_machine_run(machine, 1u)) {
                running = false;
                result = 3;
            }
        }

        {
            unsigned active_height = tnc155_video_active_height(machine);
            SDL_Rect source = {0, 0, TNC155_VIDEO_WIDTH, (int)active_height};
            if (active_height != current_video_height) {
                current_video_height = active_height;
                SDL_SetWindowSize(window, TNC155_VIDEO_WIDTH * 2,
                                  (int)active_height * 2);
                SDL_RenderSetLogicalSize(renderer, TNC155_VIDEO_WIDTH,
                                         (int)active_height);
            }
            tnc155_video_render(machine, pixels, TNC155_VIDEO_WIDTH, false);
            SDL_UpdateTexture(texture, NULL, pixels,
                              TNC155_VIDEO_WIDTH * (int)sizeof(*pixels));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, &source, NULL);
            SDL_RenderPresent(renderer);
            if (debug_overlay && debug_renderer != NULL &&
                debug_texture != NULL && debug_pixels != NULL) {
                tnc155_video_render_debug(machine, debug_pixels,
                                          TNC155_DEBUG_WIDTH);
                SDL_UpdateTexture(debug_texture, NULL, debug_pixels,
                                  TNC155_DEBUG_WIDTH *
                                      (int)sizeof(*debug_pixels));
                SDL_RenderClear(debug_renderer);
                SDL_RenderCopy(debug_renderer, debug_texture, NULL, NULL);
                SDL_RenderPresent(debug_renderer);
            }
        }
    }

done:
    tnc155_serial_keyboard_close(&serial_keyboard);
    if (debug_overlay)
        print_state(machine);
    free(pixels);
    free(debug_pixels);
    if (debug_texture != NULL) SDL_DestroyTexture(debug_texture);
    if (debug_renderer != NULL) SDL_DestroyRenderer(debug_renderer);
    if (debug_window != NULL) SDL_DestroyWindow(debug_window);
    if (texture != NULL) SDL_DestroyTexture(texture);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
    if (window != NULL) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

int main(int argc, char **argv)
{
    char user_ram_path[4096];
    char *base_path;
    const uint8_t *default_user_ram;
    size_t default_user_ram_size;
    const char *romset_name;
    tnc155_machine machine;
    bool headless = false;
    bool headless_ce = false;
    bool blank_user_ram = false;
    const char *serial_keyboard_device = "auto";
    uint64_t limit = 1000000u;
    char *end = NULL;
    int argi;
    bool have_limit = false;

    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--headless") == 0 &&
            !headless && !headless_ce) {
            headless = true;
        } else if (strcmp(argv[argi], "--headless-ce") == 0 &&
                   !headless && !headless_ce) {
            headless_ce = true;
        } else if (strcmp(argv[argi], "--blank-user-ram") == 0 &&
                   !blank_user_ram) {
            blank_user_ram = true;
        } else if (strcmp(argv[argi], "--serial-keyboard") == 0 &&
                   argi + 1 < argc) {
            serial_keyboard_device = argv[++argi];
        } else if (strcmp(argv[argi], "--no-serial-keyboard") == 0) {
            serial_keyboard_device = "off";
        } else if ((headless || headless_ce) && !have_limit) {
            errno = 0;
            limit = strtoull(argv[argi], &end, 0);
            if (errno || end == argv[argi] || *end != '\0')
                goto usage;
            have_limit = true;
        } else {
usage:
            fprintf(stderr, "usage: %s [--blank-user-ram] "
                            "[--serial-keyboard auto|off|DEVICE] "
                            "[--no-serial-keyboard] "
                            "[--headless|--headless-ce "
                            "[instruction-limit]]\n", argv[0]);
            return 2;
        }
    }

    if (!tnc155_machine_init(&machine))
        return 1;

    romset_name = tnc155_rom_selected_name();
    if (tnc155_rom_selected() == TNC155_ROMSET_Q ||
        tnc155_rom_selected() == TNC155_ROMSET_FRANK) {
        default_user_ram = tnc155q_default_user_ram;
        default_user_ram_size = tnc155q_default_user_ram_size;
    } else {
        default_user_ram = tnc155b_default_user_ram;
        default_user_ram_size = tnc155b_default_user_ram_size;
    }

    base_path = SDL_GetBasePath();
    if (base_path == NULL ||
        snprintf(user_ram_path, sizeof(user_ram_path), "%s%s-user-ram.bin",
                 base_path != NULL ? base_path : "", romset_name) >=
            (int)sizeof(user_ram_path)) {
        SDL_free(base_path);
        return 5;
    }
    SDL_free(base_path);

    if (blank_user_ram) {
        memset(machine.main.user_ram, 0, sizeof(machine.main.user_ram));
        machine.main.user_ram_loaded = false;
        machine.main.user_ram_dirty = false;
        puts("User RAM: blank cold-boot override");
    } else if (!tnc155_mainboard_load_user_ram(&machine.main, user_ram_path)) {
        if (default_user_ram_size != sizeof(machine.main.user_ram))
            return 5;
        memcpy(machine.main.user_ram, default_user_ram,
               sizeof(machine.main.user_ram));
        machine.main.user_ram_loaded = true;
        machine.main.user_ram_dirty = true;
        if (!tnc155_mainboard_save_user_ram(&machine.main, user_ram_path))
            return 5;
        printf("Initialized user RAM: %s\n", user_ram_path);
    }

    printf("TNC 155 reset: MAIN WP=>%04X PC=>%04X; CLP WP=>%04X PC=>%04X\n",
           machine.main.cpu.wp, machine.main.cpu.pc,
           machine.clp.cpu.wp, machine.clp.cpu.pc);

    if (!headless && !headless_ce) {
        int result = run_graphical(&machine, serial_keyboard_device);
        if (!blank_user_ram && machine.main.user_ram_dirty &&
            !tnc155_mainboard_save_user_ram(&machine.main, user_ram_path))
            return result == 0 ? 4 : result;
        return result;
    }

    if (headless_ce) {
        uint64_t before = limit / 2u;
        if (!tnc155_machine_run(&machine, before))
            return 3;
        if (!tnc155_i8279_push_key(&machine.main.keyboard, 0x7fu))
            return 3;
        if (!tnc155_machine_run(&machine, limit - before))
            return 3;
        print_state(&machine);
        if (!blank_user_ram && machine.main.user_ram_dirty &&
            !tnc155_mainboard_save_user_ram(&machine.main, user_ram_path))
            return 4;
        return 0;
    }

    if (!tnc155_machine_run(&machine, limit))
        return 3;
    print_state(&machine);
    if (!blank_user_ram && machine.main.user_ram_dirty &&
        !tnc155_mainboard_save_user_ram(&machine.main, user_ram_path))
        return 4;
    return 0;
}

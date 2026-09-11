#include "tnc155/machine.h"
#include "tnc155/panel_keys.h"
#include "tnc155/video.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical MP0..MP236 values supplied for this emulated machine. */
static const char *const mp[237] = {
    "2500","2500","2500","2500","1000","1000","1000","1000",
    "1000","1000","1000","1000","1","1","1","1","0","0","0","0",
    "1","1","1","1","0","0","0","0","0","0","0","0",
    "0.730","0.730","0.730","0.730","+0.045","+0.012","+0.000","+0.000",
    "+0.000","+0.000","+0.000","+0.000","-6.000","-454.815","-6.000","-298.200",
    "+3.980","-123.120","+30000.000","-30000.000","9.000","0.500","0.200","0.200",
    "2.000","10.000","0.002","14","0","0","5","52901","0.050","0","0","0.000",
    "0","1","0.000","515","8","0.000","0","0","0","1","3000.000","0.000",
    "0.000","0.000","0.000","0.000","0.000","0.000","9.999","9.999","120","80",
    "0","10.000","1","1.300","0","0","0","0","0","0","0","0","0","0","0",
    "0","0","0","0","0","150","50","5","0","0","0","0","0","25",
    "0","2","3","5","0","0","0","+0.000","+0.000","+0.000","+0.000","+0.000",
    "+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000",
    "+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000",
    "+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","+0.000","0","0","1000","1000",
    "1000","1000","5000","5000","5000","5000","1","1.000","0.200","1","0","0","0",
    "20.000","10.000","1.000","1.000","1.000","1.000","1.000","100.000","100","100",
    "0.000","0.000","+0.000","+0.000","+0.000","+0.000","1","1","0.005","0","0","0",
    "0","0","0","0","0","0","0","0","0","0","0","0","0","0","0","0","0",
    "1","2","80","0.000","0","17736","16712","279","5382","197","1","4","0","1819",
    "17200","6977","2060","1290","6990","0","0","10.000","10.000","0"
};

static uint8_t raw_key(char c)
{
    static const uint8_t digit[10] = {
        TNC155_RAW_KEY_0, TNC155_RAW_KEY_1, TNC155_RAW_KEY_2,
        TNC155_RAW_KEY_3, TNC155_RAW_KEY_4, TNC155_RAW_KEY_5,
        TNC155_RAW_KEY_6, TNC155_RAW_KEY_7, TNC155_RAW_KEY_8,
        TNC155_RAW_KEY_9
    };
    if (isdigit((unsigned char)c))
        return digit[(unsigned)(c - '0')];
    if (c == '.')
        return TNC155_RAW_KEY_DECIMAL;
    if (c == '-')
        return TNC155_RAW_KEY_PLUSMINUS;
    return 0u; /* Leading '+' means the default positive sign. */
}

static int press_and_wait(tnc155_machine *machine, uint8_t key)
{
    uint32_t before = machine->main.keyboard.keys_read;
    uint64_t limit = 20000000u;
    if (!tnc155_i8279_push_key(&machine->main.keyboard, key))
        return 0;
    while (machine->main.keyboard.keys_read == before && limit-- != 0u) {
        bool stepped_main;
        if (tnc155_machine_step(machine, &stepped_main) != TMS9995_STEP_OK)
            return 0;
    }
    if (machine->main.keyboard.keys_read == before)
        return 0;
    return tnc155_machine_run(machine, 50000u);
}

static int enter_value(tnc155_machine *machine, const char *value)
{
    const char *p = value;
    if (*p == '+')
        ++p;
    for (; *p != '\0'; ++p) {
        uint8_t key = raw_key(*p);
        if (key == 0u || !press_and_wait(machine, key))
            return 0;
    }
    return press_and_wait(machine, TNC155_RAW_KEY_ENT);
}

static int save_screen(const tnc155_machine *machine, const char *path)
{
    uint32_t *pixels;
    FILE *file;
    unsigned x, y;

    pixels = malloc((size_t)TNC155_VIDEO_WIDTH * TNC155_VIDEO_HEIGHT *
                    sizeof(*pixels));
    if (pixels == NULL)
        return 0;
    tnc155_video_render(machine, pixels, TNC155_VIDEO_WIDTH, false);
    file = fopen(path, "wb");
    if (file == NULL) {
        free(pixels);
        return 0;
    }
    fprintf(file, "P6\n%d %d\n255\n", TNC155_VIDEO_WIDTH,
            TNC155_VIDEO_HEIGHT);
    for (y = 0u; y < TNC155_VIDEO_HEIGHT; ++y) {
        for (x = 0u; x < TNC155_VIDEO_WIDTH; ++x) {
            uint32_t pixel = pixels[(size_t)y * TNC155_VIDEO_WIDTH + x];
            fputc((int)((pixel >> 16) & 0xffu), file);
            fputc((int)((pixel >> 8) & 0xffu), file);
            fputc((int)(pixel & 0xffu), file);
        }
    }
    free(pixels);
    return fclose(file) == 0;
}

int main(int argc, char **argv)
{
    const char *path = argc == 2 ? argv[1] : "tnc155-user-ram.bin";
    tnc155_machine machine;
    unsigned i;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [nvram-image]\n", argv[0]);
        return 2;
    }
    if (!tnc155_machine_init(&machine))
        return 1;
    memset(machine.main.user_ram, 0, sizeof(machine.main.user_ram));
    machine.main.user_ram_loaded = false;
    machine.main.user_ram_dirty = false;

    if (!tnc155_machine_run(&machine, 20000000u))
        return 3;

    /* A zeroed battery RAM enters the two-stage recovery dialogue before
       MP0.  These are real P8279 ENT keypresses, not firmware state edits. */
    if (!press_and_wait(&machine, TNC155_RAW_KEY_ENT) ||
        !press_and_wait(&machine, TNC155_RAW_KEY_ENT)) {
        fprintf(stderr, "startup dialogue did not consume ENT\n");
        return 3;
    }

    /* MP237..MP263 come from the photographed two-page machine-parameter
       list.  The handwritten 180 at MP251 supersedes the printed 1000. */
    static const char *const mp_suffix[] = {
        "0", "1.000", "0", "0", "0", "0", "0", "0", "0", "1",
        "0", "0", "0", "0", "180", "0", "1", "2", "3", "4", "5",
        "0.000", "0", "0", "0", "0", "0"
    };

    for (i = 0u; i < 264u; ++i) {
        const char *value = i < 237u ? mp[i] : mp_suffix[i - 237u];
        if (!enter_value(&machine, value)) {
            fprintf(stderr, "MP%u input stalled at value %s\n", i, value);
            return 3;
        }
        printf("\rMP%u = %s", i, value);
        fflush(stdout);
    }
    putchar('\n');
    if (!tnc155_machine_run(&machine, 5000000u))
        return 3;
    machine.main.user_ram_dirty = true;
    if (!tnc155_mainboard_save_user_ram(&machine.main, path)) {
        fprintf(stderr, "cannot save %s\n", path);
        return 4;
    }
    printf("saved %s (%zu bytes)\n", path, sizeof(machine.main.user_ram));
    if (!save_screen(&machine, "/tmp/tnc155-mp-seed-final.ppm"))
        fprintf(stderr, "warning: could not save final screen\n");
    return 0;
}

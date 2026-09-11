#include "tnc155/serial_keyboard.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static bool configure_port(int fd)
{
    struct termios term;
    if (tcgetattr(fd, &term) != 0)
        return false;
    term.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                                INLCR | IGNCR | ICRNL | IXON);
    term.c_oflag &= (tcflag_t)~OPOST;
    term.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    term.c_cflag &= (tcflag_t)~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    term.c_cflag &= (tcflag_t)~CRTSCTS;
#endif
    term.c_cflag |= (tcflag_t)(CS8 | CLOCAL | CREAD);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    return cfsetispeed(&term, B115200) == 0 &&
           cfsetospeed(&term, B115200) == 0 &&
           tcsetattr(fd, TCSANOW, &term) == 0;
}

static void reset_transport_state(tnc155_serial_keyboard *keyboard)
{
    keyboard->fd = -1;
    keyboard->device_path[0] = '\0';
    keyboard->line_length = 0u;
    keyboard->line_overflow = false;
    keyboard->key_down = false;
    keyboard->active_kd_code = 0u;
}

static bool open_one(tnc155_serial_keyboard *keyboard, const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return false;
    if (!configure_port(fd)) {
        close(fd);
        return false;
    }
    keyboard->fd = fd;
    (void)snprintf(keyboard->device_path, sizeof(keyboard->device_path),
                   "%s", path);
    keyboard->line_length = 0u;
    keyboard->line_overflow = false;
    keyboard->key_down = false;
    keyboard->active_kd_code = 0u;
    return true;
}

bool tnc155_serial_keyboard_open(tnc155_serial_keyboard *keyboard,
                                 const char *requested_device)
{
    static const char *const automatic_devices[] = {
        "/dev/ttyACM0", "/dev/ttyACM1"
    };
    size_t i;

    if (keyboard == NULL)
        return false;
    tnc155_serial_keyboard_close(keyboard);
    if (requested_device != NULL && strcmp(requested_device, "off") == 0)
        return false;
    if (requested_device != NULL && strcmp(requested_device, "auto") != 0) {
        if (open_one(keyboard, requested_device)) {
            printf("Serial keyboard: %s (115200 8N1)\n",
                   keyboard->device_path);
            return true;
        }
        fprintf(stderr, "Serial keyboard: cannot open %s: %s\n",
                requested_device, strerror(errno));
        return false;
    }
    for (i = 0u; i < sizeof(automatic_devices) / sizeof(automatic_devices[0]);
         ++i) {
        if (open_one(keyboard, automatic_devices[i])) {
            printf("Serial keyboard: %s (115200 8N1)\n",
                   keyboard->device_path);
            return true;
        }
    }
    puts("Serial keyboard: /dev/ttyACM0 and /dev/ttyACM1 not available; "
         "SDL keyboard remains active");
    return false;
}

void tnc155_serial_keyboard_close(tnc155_serial_keyboard *keyboard)
{
    if (keyboard == NULL)
        return;
    if (keyboard->fd >= 0)
        close(keyboard->fd);
    reset_transport_state(keyboard);
}

void tnc155_serial_keyboard_poll(tnc155_serial_keyboard *keyboard,
                                 tnc155_i8279 *i8279)
{
    uint8_t buffer[256];

    if (keyboard == NULL || i8279 == NULL || keyboard->fd < 0)
        return;

    for (;;) {
        ssize_t count = read(keyboard->fd, buffer, sizeof(buffer));
        if (count > 0) {
            size_t i;
            fputs("Serial keyboard RAW: ", stdout);
            for (i = 0u; i < (size_t)count; ++i) {
                unsigned char byte = buffer[i];
                if (byte == '\0') fputs("<BREAK>", stdout);
                else if (byte == '\r') fputs("<CR>", stdout);
                else if (byte == '\n') fputs("<LF>", stdout);
                else if (isprint(byte)) fputc((int)byte, stdout);
                else printf("<%02X>", (unsigned)byte);
            }
            fputc('\n', stdout);
            fflush(stdout);
            tnc155_serial_keyboard_feed(keyboard, i8279, buffer,
                                        (size_t)count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (count == 0)
            return;
        fprintf(stderr, "Serial keyboard: read error on %s: %s\n",
                keyboard->device_path, strerror(errno));
        tnc155_serial_keyboard_close(keyboard);
        return;
    }
}

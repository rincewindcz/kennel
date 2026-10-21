// padinject: writes button presses into an evdev device for testing on the console.
// Usage: padinject [-d /dev/input/eventN] BUTTON[:HOLD_MS] ...   e.g. padinject right right a
// Buttons use the GA36-MB evdev codes (SDL "a" is BTN_EAST).
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct {
    const char* name;
    int code;
} buttons[] = {
    {"a", BTN_EAST},     {"b", BTN_SOUTH},    {"x", BTN_NORTH},       {"y", BTN_WEST},
    {"l1", BTN_TL},      {"r1", BTN_TR},      {"l2", BTN_TL2},        {"r2", BTN_TR2},
    {"select", BTN_SELECT}, {"start", BTN_START}, {"mode", BTN_MODE}, {"l3", BTN_THUMBL},
    {"r3", BTN_THUMBR},  {"up", BTN_DPAD_UP}, {"down", BTN_DPAD_DOWN}, {"left", BTN_DPAD_LEFT},
    {"right", BTN_DPAD_RIGHT},
};

static void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != sizeof ev) perror("write");
}

int main(int argc, char** argv) {
    const char* dev = "/dev/input/event2";
    int i = 1;
    if (argc > 2 && !strcmp(argv[1], "-d")) {
        dev = argv[2];
        i = 3;
    }
    int fd = open(dev, O_WRONLY);
    if (fd < 0) {
        perror(dev);
        return 1;
    }
    for (; i < argc; ++i) {
        char name[32];
        int hold = 80;
        snprintf(name, sizeof name, "%s", argv[i]);
        char* colon = strchr(name, ':');
        if (colon) {
            *colon = 0;
            hold = atoi(colon + 1);
        }
        if (!strcmp(name, "wait")) {
            usleep(hold * 1000);
            continue;
        }
        int code = -1;
        for (size_t b = 0; b < sizeof buttons / sizeof buttons[0]; ++b)
            if (!strcmp(buttons[b].name, name)) code = buttons[b].code;
        if (code < 0) {
            fprintf(stderr, "unknown button %s\n", name);
            return 2;
        }
        emit(fd, EV_KEY, code, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(hold * 1000);
        emit(fd, EV_KEY, code, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(150 * 1000);
    }
    close(fd);
    return 0;
}

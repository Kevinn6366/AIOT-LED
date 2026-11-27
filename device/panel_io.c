/* AXI GPIO channel 1 selects LED sources; channel 2 samples switches and LEDs. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int prepare_fifo(const char *path) {
    struct stat st;
    if (mkfifo(path, 0600) < 0 && errno != EEXIST) return -1;
    return lstat(path, &st) == 0 && S_ISFIFO(st.st_mode) ? 0 : -1;
}

/* One turn in 7.68 seconds. Both PWM channels receive the same frame. */
static uint32_t rainbow_color(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    unsigned hue = (ms / 5) % 1536;
    unsigned up = hue % 256, down = 255 - up;
    switch (hue / 256) {
        case 0: return 0xff0000 | (up << 8);
        case 1: return (down << 16) | 0xff00;
        case 2: return 0xff00 | up;
        case 3: return (down << 8) | 0xff;
        case 4: return (up << 16) | 0xff;
        default: return 0xff0000 | down;
    }
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s UIO COMMAND_FIFO STATUS_FIFO RGB_UIO\n", argv[0]);
        return EXIT_FAILURE;
    }
    int uio = open(argv[1], O_RDWR | O_SYNC);
    if (uio < 0) { perror("UIO"); return EXIT_FAILURE; }
    volatile uint32_t *regs = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, uio, 0);
    if (regs == MAP_FAILED) { perror("mmap"); return EXIT_FAILURE; }
    if ((regs[2] >> 14) != 21063u) {
        fprintf(stderr, "Unexpected PL design ID\n");
        return EXIT_FAILURE;
    }
    int rgb_fd = open(argv[4], O_RDWR | O_SYNC);
    if (rgb_fd < 0) { perror("RGB UIO"); return EXIT_FAILURE; }
    volatile uint32_t *rgb = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, rgb_fd, 0);
    if (rgb == MAP_FAILED) { perror("RGB mmap"); return EXIT_FAILURE; }
    uint32_t colors[2] = {0xffffff, 0xffffff};
    unsigned power = 0;
    rgb[0] = 0;
    rgb[2] = 0;
    if (prepare_fifo(argv[2]) || prepare_fifo(argv[3])) {
        fprintf(stderr, "Cannot prepare panel FIFOs\n");
        return EXIT_FAILURE;
    }
    int fd = open(argv[2], O_RDWR | O_NOFOLLOW);
    FILE *commands = fd < 0 ? NULL : fdopen(fd, "r");
    if (!commands) { perror("command FIFO"); return EXIT_FAILURE; }
    signal(SIGPIPE, SIG_IGN);
    uint32_t control = 0;  /* AXI GPIO output bits need a software shadow. */
    regs[0] = control;
    char line[128];
    setvbuf(commands, NULL, _IONBF, 0);
    struct pollfd input = {fd, POLLIN, 0};
    int synced = 0;
    uint32_t frame = 0;
    for (;;) {
        if (synced) {
            frame = rainbow_color();
            rgb[0] = frame;
            rgb[2] = frame;
        }
        int ready = poll(&input, 1, synced ? 20 : -1);
        if (!ready || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0 || !fgets(line, sizeof(line), commands)) break;
        char token[33], extra;
        int channel, mode;
        if (sscanf(line, "%32[0-9a-f] %d %d %c", token, &channel, &mode, &extra) != 3)
            continue;
        if (channel >= 0 && channel < 4 && mode >= -1 && mode <= 1) {
            uint32_t bit = 1u << channel;
            if (mode == -1) control &= ~(bit << 4);
            else {
                control |= bit << 4;
                if (mode) control |= bit;
                else control &= ~bit;
            }
            regs[0] = control;
        } else if (channel == 7 && (mode == 0 || mode == 1)) {
            synced = mode;
            if (synced) {
                frame = rainbow_color();
                rgb[0] = frame;
                rgb[2] = frame;
            } else {
                rgb[0] = (power & 1) ? colors[0] : 0;
                rgb[2] = (power & 2) ? colors[1] : 0;
            }
        } else if ((channel == 5 || channel == 6) &&
                   (mode == -3 || mode == -2 || (mode >= 0 && mode <= 0xffffff))) {
            if (synced) {
                synced = 0;
                rgb[0] = (power & 1) ? colors[0] : 0;
                rgb[2] = (power & 2) ? colors[1] : 0;
            }
            unsigned lamp = (unsigned)(channel - 5);
            unsigned bit = 1u << lamp;
            if (mode == -3) power |= bit;
            else if (mode == -2) power &= ~bit;
            else colors[lamp] = (uint32_t)mode;
            rgb[2 * lamp] = (power & bit) ? colors[lamp] : 0;
        } else if (channel != -1 || mode != -1) {
            continue;
        }
        uint32_t inputs = regs[2];
        int out = open(argv[3], O_WRONLY | O_NONBLOCK | O_NOFOLLOW);
        if (out >= 0) {
            char reply[96];
            int n = snprintf(reply, sizeof(reply), "%s %u %u %u %u %u %u %d\n", token,
                             inputs & 15u, (inputs >> 4) & 15u, (control >> 4) & 15u,
                             synced ? frame : colors[0], synced ? frame : colors[1],
                             synced ? 3u : power, synced);
            if (write(out, reply, (size_t)n) < 0 && errno != EPIPE) perror("reply");
            close(out);
        }
    }
    return EXIT_FAILURE;
}

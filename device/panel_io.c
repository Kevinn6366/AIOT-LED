/* AXI GPIO channel 1 selects LED sources; channel 2 samples switches and LEDs. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
    while (fgets(line, sizeof(line), commands)) {
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
        } else if ((channel == 5 || channel == 6) &&
                   (mode == -3 || mode == -2 || (mode >= 0 && mode <= 0xffffff))) {
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
            int n = snprintf(reply, sizeof(reply), "%s %u %u %u %u %u %u\n", token,
                             inputs & 15u, (inputs >> 4) & 15u, (control >> 4) & 15u,
                             colors[0], colors[1], power);
            if (write(out, reply, (size_t)n) < 0 && errno != EPIPE) perror("reply");
            close(out);
        }
    }
    return EXIT_FAILURE;
}

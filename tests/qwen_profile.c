/* Sample the b9ea48d diagnostic GPIO during representative resident-weight
 * launches. Low three bits are row-read, arithmetic and idle. Upper bits
 * retain the 50 MHz timebase. The normal application must be idle. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static volatile uint32_t *control, *clock_gpio;
static uint32_t rng = 71;

static void profile(const char *name, unsigned width, unsigned rows,
                    unsigned cols, unsigned format, int repeats) {
    uint64_t samples[8] = {0}, ticks = 0;
    for (int run = 0; run < repeats; ++run) {
        uint32_t start = *clock_gpio & ~7u;
        control[4] = 0x20000000 / 8;
        control[6] = 0x3a600000 / 8;
        control[8] = 0x3a700000 / 8;
        control[10] = width;
        control[12] = rows;
        control[14] = cols;
        control[16] = format;
        __sync_synchronize();
        control[0] = 1;
        uint32_t next = start;
        while (!(control[0] & 2)) {
            uint32_t value = *clock_gpio;
            if ((uint32_t)((value & ~7u) - start) > 1500000000u) exit(3);
            if ((int32_t)((value & ~7u) - next) >= 0) {
                ++samples[value & 7];
                rng = rng * 1664525u + 1013904223u;
                next = (value & ~7u) + 192 + (rng >> 23);
            }
        }
        ticks += (uint32_t)((*clock_gpio & ~7u) - start);
    }
    printf("%s %d %llu", name, repeats, (unsigned long long)ticks);
    for (int i=0; i<8; ++i) printf(" %llu", (unsigned long long)samples[i]);
    puts("");
    fflush(stdout);
}
/* Randomize sample spacing to avoid locking to the row loop or the Linux tick.
 * Trim idle samples before the first and after the last active observation. */
static int sample_request(void) {
    uint64_t bins[8] = {0}, trailing = 0;
    struct timespec ts;
    int started = 0;
    for (;;) {
        unsigned flags = *clock_gpio & 7;
        if (flags != 4) {
            started = 1;
            trailing = 0;
        } else if (started && ++trailing >= 2000) break;
        if (started) ++bins[flags];
        rng = rng * 1664525u + 1013904223u;
        ts.tv_sec = 0;
        ts.tv_nsec = 600000 + (rng >> 10);
        nanosleep(&ts, NULL);
    }
    bins[4] -= trailing - 1;
    printf("request");
    for (int i=0; i<8; ++i) printf(" %llu", (unsigned long long)bins[i]);
    puts("");
    return 0;
}

int main(int argc, char **argv) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return 1;
    control = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x43c10000);
    clock_gpio = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0x41220000);
    if (control == MAP_FAILED || clock_gpio == MAP_FAILED) return 1;
    close(fd);
    for (int i=0; i<128; ++i) if ((*clock_gpio & 7) != 4) return 2;
    if (argc==2 && strcmp(argv[1],"sample")==0) return sample_request();
    if (argc!=1) return 1;
    profile("empty", 0, 1, 1, 0, 1000);
    profile("Q4-query", 896, 896, 2, 0, 24);
    profile("Q4-up", 896, 4864, 2, 0, 12);
    profile("Q4-down", 4864, 896, 1, 0, 12);
    profile("Q8-vocab", 896, 151936, 1, 1, 2);
    profile("FP-attention", 64, 64, 1, 2, 200);
    return 0;
}

/* Generate ASCII characters using the ZyboGPT AXI-Lite register interface. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    FILE *command = fopen(argv[2], "rb");
    unsigned seed, count, length;
    unsigned char prompt[128];
    if (!command || fscanf(command, "%u %u %u", &seed, &count, &length) != 3 ||
        fgetc(command) != '\n' || length == 0 || length >= sizeof(prompt) ||
        count == 0 || count > 128 - length ||
        fread(prompt, 1, length, command) != length) return 1;
    fclose(command);
    int fd = open(argv[1], O_RDWR | O_SYNC);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) < 0) return 2;
    volatile uint32_t *reg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (reg == MAP_FAILED || reg[7] != 0x80800240u) return 3;
    reg[9] = seed;
    reg[8] = 170;  /* Q8.8 inverse temperature, T = 1.5. */
    unsigned token = 0;
    uint64_t cycles = 0;
    for (unsigned pos = 0; pos < length + count - 1; ++pos) {
        reg[2] = pos < length ? prompt[pos] : token;
        reg[4] = pos;
        reg[0] = 0;
        reg[0] = 1;
        reg[0] = 0;
        uint64_t deadline = monotonic_ms() + 250;
        while (!(reg[1] & 2u)) {
            if (monotonic_ms() >= deadline) return 4;
        }
        token = reg[3] & 127;
        cycles += reg[5];
        if (pos + 1 >= length) {
            printf("%u %llu\n", token, (unsigned long long)cycles);
            fflush(stdout);
        }
    }
    return 0;
}

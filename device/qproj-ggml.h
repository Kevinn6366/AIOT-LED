/* First-layer Q projection for the AIOT-LED bitstream, called on ggml thread 0. */
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <inttypes.h>

static uint32_t qproj_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return bits;
}

static volatile uint32_t *qproj_map(int fd, off_t addr, size_t bytes) {
    void *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr);
    GGML_ASSERT(map != MAP_FAILED);
    return map;
}

static void qproj_run(const struct ggml_tensor *weights, const void *inputs,
                      struct ggml_tensor *output, int columns) {
    static volatile uint32_t *mem, *ctrl, *clock_reg;
    static const void *packed_weights;
    static uint64_t calls, cycles;
    if (!mem) {
        int fd = open("/dev/mem", O_RDWR | O_SYNC);
        GGML_ASSERT(fd >= 0);
        mem = qproj_map(fd, 0x1e000000, 0x100000);
        ctrl = qproj_map(fd, 0x43c10000, 4096);
        clock_reg = qproj_map(fd, 0x41220000, 4096);
        close(fd);
    }
    const block_q4_0 *w = weights->data;
    if (packed_weights != w) {
        for (int b=0; b<896*28; ++b) {
            for (int j=0; j<4; ++j) {
                uint32_t word;
                memcpy(&word, w[b].qs + 4*j, 4);
                mem[b*5+j] = word;
            }
            mem[b*5+4] = qproj_bits(GGML_FP16_TO_FP32(w[b].d));
        }
        packed_weights = w;
    }
    for (int col=0; col<columns; ++col) {
        const block_q8_0 *x = (const block_q8_0 *)inputs + col*28;
        for (int b=0; b<28; ++b) {
            for (int j=0; j<8; ++j) {
                uint32_t word;
                memcpy(&word, x[b].qs + 4*j, 4);
                mem[0x80000/4+b*9+j] = word;
            }
            mem[0x80000/4+b*9+8] = qproj_bits(GGML_FP16_TO_FP32(x[b].d));
        }
        __sync_synchronize();
        const uint32_t start = clock_reg[0];
        ctrl[0] = 1;
        const int64_t deadline = ggml_time_ms() + 2000;
        while (!(ctrl[0] & 2)) {
            GGML_ASSERT(ggml_time_ms() < deadline);
            usleep(50);
        }
        cycles += (uint32_t)(clock_reg[0] - start);
        __sync_synchronize();
        float *row = (float *)((char *)output->data + col*output->nb[1]);
        for (int r=0; r<896; ++r) {
            uint32_t word = mem[0x90000/4+r];
            memcpy(&row[r], &word, 4);
            if (calls == 0) {
                float ref;
                ggml_vec_dot_q4_0_q8_0(896, &ref, 0, w+r*28, 0, x, 0, 1);
                GGML_ASSERT(isfinite(row[r]) && fabsf(row[r]-ref) <= 0.0002f*(1+fabsf(ref)));
            }
        }
        ++calls;
    }
    FILE *fp = fopen("/tmp/aiot-led/qproj.stats.new", "w");
    GGML_ASSERT(fp != NULL);
    fprintf(fp, "%" PRIu64 " %" PRIu64 "\n", calls, cycles);
    fclose(fp);
    GGML_ASSERT(rename("/tmp/aiot-led/qproj.stats.new", "/tmp/aiot-led/qproj.stats") == 0);
}

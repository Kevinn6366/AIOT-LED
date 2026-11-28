#include <ap_int.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

void qwen_qproj(volatile uint32_t *ddr, volatile ap_uint<1> &active);
union Bits { float f; uint32_t u; };

int main() {
    const unsigned base = 0x1e000000 / 4;
    std::vector<uint32_t> ddr(base + 0x100000 / 4);
    std::vector<float> expected(896);
    for (int b = 0; b < 28; ++b) {
        Bits xs;
        xs.f = (b % 5 + 1) / 16.0f;
        ddr[base + 0x80000 / 4 + b * 9 + 8] = xs.u;
        for (int r = 0; r < 896; ++r) {
            const unsigned w = base + (r * 28 + b) * 5;
            int dot = 0;
            for (int k = 0; k < 32; ++k) {
                int q = (r + b * 3 + k * 7) % 16 - 8;
                int x = (b * 13 + k * 11) % 256 - 128;
                dot += q * x;
                ddr[w + (k % 16) / 4] |= (uint32_t)(q + 8) << ((k % 4) * 8 + (k / 16) * 4);
                if (r == 0) ddr[base + 0x80000 / 4 + b * 9 + k / 4] |= (uint32_t)(uint8_t)x << ((k % 4) * 8);
            }
            Bits ws;
            ws.f = (r % 7 - 3) / 8.0f;
            ddr[w + 4] = ws.u;
            expected[r] += dot * (xs.f * ws.f);
        }
    }
    volatile ap_uint<1> active;
    qwen_qproj(ddr.data(), active);
    for (int r = 0; r < 896; ++r) {
        Bits result;
        result.u = ddr[base + 0x90000 / 4 + r];
        if (result.f != expected[r]) return 1;
    }
    std::puts("QPROJ PASS 896");
    return 0;
}

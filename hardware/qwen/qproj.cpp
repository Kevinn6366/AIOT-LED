#include <ap_int.h>
#include <stdint.h>

// The device tree reserves this 1 MiB window exclusively for the accelerator.
static const unsigned base = 0x1e000000u / 4;
static const unsigned blocks = 28;
union FloatWord { float f; uint32_t u; };

void qwen_qproj(volatile uint32_t *ddr, volatile ap_uint<1> &active) {
#pragma HLS INTERFACE m_axi port=ddr offset=off bundle=hp depth=262144
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS INTERFACE ap_none port=active
    active = 1;
    ap_int<8> x[28][32];
#pragma HLS ARRAY_PARTITION variable=x complete dim=2
    float xs[28];
    for (unsigned b=0; b<blocks; ++b) {
        for (unsigned j=0; j<8; ++j) {
            ap_uint<32> word = ddr[base + 0x80000/4 + b*9 + j];
            for (unsigned k=0; k<4; ++k) {
#pragma HLS UNROLL
                x[b][j*4+k] = word.range(k*8+7,k*8);
            }
        }
        FloatWord s; s.u = ddr[base + 0x80000/4 + b*9+8]; xs[b] = s.f;
    }
    for (unsigned r=0; r<896; ++r) {
        float sum = 0;
        for (unsigned b=0; b<blocks; ++b) {
            ap_int<18> dot = 0;
            for (unsigned j=0; j<4; ++j) {
                ap_uint<32> word = ddr[base+(r*blocks+b)*5+j];
                for (unsigned k=0; k<4; ++k) {
#pragma HLS UNROLL
                    ap_int<5> lo = (int)word.range(k*8+3,k*8).to_uint()-8;
                    ap_int<5> hi = (int)word.range(k*8+7,k*8+4).to_uint()-8;
                    dot += lo*x[b][j*4+k] + hi*x[b][16+j*4+k];
                }
            }
            FloatWord s; s.u=ddr[base+(r*blocks+b)*5+4];
            sum += (s.f*xs[b])*dot.to_int();
        }
        FloatWord result; result.f=sum; ddr[base+0x90000/4+r]=result.u;
    }
    active = 0;
}

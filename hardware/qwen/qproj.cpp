#include <ap_int.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t Word;
union FloatWord { float f; uint32_t u; };
static float unpack(uint32_t bits) { FloatWord v; v.u=bits; return v.f; }
static uint32_t pack(float v) { FloatWord b; b.f=v; return b.u; }

// Addresses are physical 64-bit word indices in the reserved HP0 window.
// Formats 0 and 1 use Q4_0 or Q8_0 weights and Q8_0 activations.
// Format 2 consumes packed pairs of FP32 values for attention score/value products.
void qwen_qproj(Word *mem, unsigned weight, unsigned input, unsigned output,
                unsigned width, unsigned rows, unsigned cols, unsigned format) {
#pragma HLS INTERFACE m_axi port=mem offset=off bundle=hp depth=134217728 max_read_burst_length=128
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS INTERFACE s_axilite port=weight bundle=control offset=0x10
#pragma HLS INTERFACE s_axilite port=input bundle=control offset=0x18
#pragma HLS INTERFACE s_axilite port=output bundle=control offset=0x20
#pragma HLS INTERFACE s_axilite port=width bundle=control offset=0x28
#pragma HLS INTERFACE s_axilite port=rows bundle=control offset=0x30
#pragma HLS INTERFACE s_axilite port=cols bundle=control offset=0x38
#pragma HLS INTERFACE s_axilite port=format bundle=control offset=0x40
    if (!width || width>4864 || !rows || rows>151936 || !cols || cols>2 || format>2) return;
    const unsigned blocks=width/32;
    const unsigned stride=format==0 ? blocks*3 : format==1 ? blocks*5 : width/2;
    const unsigned xs=format==2 ? width/2 : blocks*5;
    Word x[2][760], w[760];
#pragma HLS ARRAY_PARTITION variable=x complete dim=1
#ifndef __SYNTHESIS__
    if (cols==1) memset(x[1], 0, sizeof(x[1])); // unused physical lane
#endif
    if (!stride || stride>760 || xs>760) return;
    for (unsigned c=0;c<cols;++c) memcpy(x[c], mem+input+c*xs, xs*sizeof(Word));
    Word result[64];
    for (unsigned first=0;first<rows;first+=64) {
        const unsigned n=rows-first<64 ? rows-first : 64;
        for (unsigned r=0;r<n;++r) {
            memcpy(w, mem+weight+(first+r)*stride, stride*sizeof(Word));
            float acc[2][8]={};
#pragma HLS ARRAY_PARTITION variable=acc complete dim=0
            if (format==2) {
                for (unsigned j=0;j<stride;++j) {
#pragma HLS PIPELINE II=1
                    for (int c=0;c<2;++c) {
#pragma HLS UNROLL
                        if (c<cols) acc[c][j%8]+=unpack(w[j])*unpack(x[c][j])+unpack(w[j]>>32)*unpack(x[c][j]>>32);
                    }
                }
            } else {
                for (unsigned b=0;b<blocks;++b) {
                    const unsigned wi=b*(format==0?3:5);
                    const float scale=unpack(w[wi+(format==0?2:4)]);
                    int dots[2]={0,0};
                    for (unsigned part=0;part<4;++part) {
#pragma HLS PIPELINE II=1
                        ap_uint<64> weights=w[wi+(format==0 ? part%2 : part)];
                        for (unsigned c=0;c<2;++c) {
#pragma HLS UNROLL
                            ap_uint<64> act=x[c][b*5+part];
                            int partial=0;
                            for (int k=0;k<8;++k) {
#pragma HLS UNROLL
                                ap_int<8> v=weights.range(k*8+7,k*8);
                                ap_uint<8> raw=weights.range(k*8+7,k*8);
                                if (!format) v=(int)(part<2 ? raw.range(3,0).to_uint() : raw.range(7,4).to_uint())-8;
                                ap_int<8> a=act.range(k*8+7,k*8);
                                partial+=v*a;
                            }
                            dots[c]+=partial;
                        }
                    }
                    for (int c=0;c<2;++c) {
#pragma HLS UNROLL
                        if (c<cols) acc[c][b%8]+=dots[c]*(scale*unpack(x[c][b*5+4]));
                    }
                }
            }
            float sums[2]={};
            for (int c=0;c<2;++c) for (int k=0;k<8;++k) sums[c]+=acc[c][k];
            result[r]=((Word)pack(sums[1])<<32)|pack(sums[0]);
        }
        memcpy(mem+output+first,result,n*sizeof(Word));
    }
}

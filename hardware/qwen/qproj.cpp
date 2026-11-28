#include <ap_int.h>
#include <hls_stream.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t Word;
union FloatWord { float f; uint32_t u; };
static float unpack(uint32_t bits) { FloatWord v; v.u=bits; return v.f; }
static uint32_t pack(float v) { FloatWord b; b.f=v; return b.u; }

struct WeightBlock {
    Word payload[4];
    uint32_t scale;
};

static void fetch(const Word *weights, unsigned count, hls::stream<Word>& words) {
    for (unsigned i=0; i<count; ++i) {
#pragma HLS PIPELINE II=1
        words.write(weights[i]);
    }
}

// Expand the original GGUF scale exactly, including binary16 subnormals.
static uint32_t scale_bits(ap_uint<16> half) {
#pragma HLS INLINE
    unsigned exponent=half.range(14,10);
    ap_uint<10> fraction=half.range(9,0);
    uint32_t sign=(uint32_t)half[15]<<31;
    if (exponent==31) return sign|0x7f800000u|((uint32_t)fraction<<13);
    if (exponent) return sign|((exponent+112)<<23)|((uint32_t)fraction<<13);
    if (!fraction) return sign;
    unsigned shift=fraction.countLeadingZeros()+1;
    return sign|((113-shift)<<23)|(((uint32_t)fraction<<shift & 1023)<<13);
}

// Four original FP16 scales occupy one header word. Its Q4/Q8 payloads
// follow consecutively so HP0 can burst across row boundaries.
static void assemble(hls::stream<Word>& words, hls::stream<WeightBlock>& blocks,
                     unsigned count, bool wide) {
    Word previous[3]={};
#pragma HLS ARRAY_PARTITION variable=previous complete
    Word scales=0;
    bool header=true;
    unsigned part=0, block=0;
    for (unsigned i=0; i<count; ++i) {
#pragma HLS PIPELINE II=1
        Word word=words.read();
        if (header) {
            scales=word;
            header=false;
        } else if (part==(wide ? 3u : 1u)) {
            WeightBlock w;
            w.payload[0]=previous[0];
            w.payload[1]=wide ? previous[1] : word;
            w.payload[2]=previous[2];
            w.payload[3]=word;
            w.scale=scale_bits(scales);
            blocks.write(w);
            scales>>=16;
            header=block==3;
            block=(block+1)&3;
            part=0;
        } else {
            previous[part++]=word;
        }
    }
}

static int lut_product(ap_int<8> w, ap_int<8> a) {
#pragma HLS INLINE
    ap_int<16> product=w*a;
#pragma HLS RESOURCE variable=product core=Mul_LUT
    return product;
}

static float sum8(const float values[8]) {
#pragma HLS INLINE
    float sum=0;
    for (int k=0; k<8; ++k) {
#pragma HLS UNROLL
        sum+=values[k];
    }
    return sum;
}

static void quant_rows(hls::stream<WeightBlock>& stream, const Word x[4][760],
                       Word result[128], unsigned blocks, unsigned rows, bool wide, bool four) {
    for (unsigned row=0; row<rows; ++row) {
        float acc[4][8]={};
#pragma HLS ARRAY_PARTITION variable=acc complete dim=1
#pragma HLS RESOURCE variable=acc core=RAM_2P_LUTRAM
        for (unsigned b=0; b<blocks; ++b) {
#pragma HLS PIPELINE II=1
            #pragma HLS DEPENDENCE variable=acc inter distance=8 true
            WeightBlock w=stream.read();
            for (int c=0; c<4; ++c) {
#pragma HLS UNROLL
                int dot=0;
                for (unsigned p=0; p<4; ++p) {
#pragma HLS UNROLL
                    ap_uint<64> payload=w.payload[wide ? p : p%2];
                    ap_uint<64> activation=x[c][5*b+p];
                    for (unsigned k=0; k<8; ++k) {
#pragma HLS UNROLL
                        ap_uint<8> raw=payload.range(8*k+7,8*k);
                        ap_int<8> v=raw;
                        if (!wide) v=(int)(p<2 ? raw.range(3,0).to_uint() : raw.range(7,4).to_uint())-8;
                        ap_int<8> a=activation.range(8*k+7,8*k);
                        dot+=(k>=6 ? lut_product(v,a) : (int)(v*a));
                    }
                }
                acc[c][b%8]+=dot*(unpack(w.scale)*unpack(x[c][5*b+4]));
            }
        }
        unsigned dst=four ? 2*row : row;
        if (four) result[dst+1]=((Word)pack(sum8(acc[3]))<<32)|pack(sum8(acc[2]));
        result[dst]=((Word)pack(sum8(acc[1]))<<32)|pack(sum8(acc[0]));
    }
}

static void quant_tile(const Word *weights, const Word x[4][760], Word result[128],
                       unsigned blocks, unsigned rows, bool wide, bool four) {
#pragma HLS DATAFLOW
    hls::stream<Word> words;
    hls::stream<WeightBlock> packed;
#pragma HLS STREAM variable=words depth=64
#pragma HLS STREAM variable=packed depth=32
    unsigned count=rows*(blocks/4)*(wide ? 17 : 9);
    fetch(weights,count,words);
    assemble(words,packed,count,wide);
    quant_rows(packed,x,result,blocks,rows,wide,four);
}

static void attention_tile(const Word *weights, const Word x[4][760],
                           Word result[128], unsigned stride, unsigned rows) {
    Word w[256];
    for (unsigned r=0; r<rows; ++r) {
        memcpy(w,weights+r*stride,stride*sizeof(Word));
        float acc[2][8]={};
#pragma HLS ARRAY_PARTITION variable=acc complete dim=0
        for (unsigned j=0; j<stride; ++j) {
#pragma HLS PIPELINE II=1
            for (int c=0; c<2; ++c) {
#pragma HLS UNROLL
                acc[c][j%8]+=unpack(w[j])*unpack(x[c][j])+unpack(w[j]>>32)*unpack(x[c][j]>>32);
            }
        }
        result[r]=((Word)pack(sum8(acc[1]))<<32)|pack(sum8(acc[0]));
    }
}

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
    if (!width || width>4864 || !rows || rows>151936 || !cols || cols>4 || (format==2 && cols>2) || format>2) return;
    const unsigned blocks=width/32;
    const unsigned stride=format==2 ? width/2 : (blocks/4)*(format==0 ? 9 : 17);
    const unsigned xs=format==2 ? width/2 : blocks*5;
    Word x[4][760];
#pragma HLS ARRAY_PARTITION variable=x complete dim=1
#pragma HLS ARRAY_PARTITION variable=x cyclic factor=5 dim=2
    // Physical unused lanes are discarded by the caller. Define them for C simulation.
#ifndef __SYNTHESIS__
    memset(x,0,sizeof(x));
#endif
    if ((format!=2 && width%128) || !stride || stride>760 || xs>760 || (format==2 && stride>256)) return;
    bool four=cols>2;
    for (unsigned c=0;c<(format==2 ? 2u : 4u);++c) {
        // Fill unused lanes from column zero so partial batches have defined inputs.
        unsigned base=input+c*xs;
        if (c>=cols) continue;
        for (unsigned i=0;i<xs;++i) {
#pragma HLS PIPELINE II=1
            x[c][i]=mem[base+i];
        }
    }
    Word result[128];
    for (unsigned first=0;first<rows;first+=64) {
        unsigned n=rows-first<64 ? rows-first : 64;
        const Word *tile=mem+weight+first*stride;
        if (format==2) attention_tile(tile,x,result,stride,n);
        else quant_tile(tile,x,result,blocks,n,format==1,four);
        memcpy(mem+output+first*(four ? 2 : 1),result,n*(four ? 2 : 1)*sizeof(Word));
    }
}

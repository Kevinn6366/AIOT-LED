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

static void assemble(hls::stream<Word>& words, hls::stream<WeightBlock>& blocks,
                     unsigned count, bool wide) {
    Word history[4]={};
#pragma HLS ARRAY_PARTITION variable=history complete
    unsigned phase=0;
    for (unsigned i=0; i<count; ++i) {
#pragma HLS PIPELINE II=1
        Word word=words.read();
        if (phase==(wide ? 4u : 2u)) {
            WeightBlock b;
            b.payload[0]=wide ? history[0] : history[2];
            b.payload[1]=wide ? history[1] : history[3];
            b.payload[2]=history[2];
            b.payload[3]=history[3];
            b.scale=word;
            blocks.write(b);
            phase=0;
        } else {
            for (int k=0; k<3; ++k) {
#pragma HLS UNROLL
                history[k]=history[k+1];
            }
            history[3]=word;
            ++phase;
        }
    }
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

static void quant_rows(hls::stream<WeightBlock>& stream, const Word x[2][760],
                       Word result[64], unsigned blocks, unsigned rows, bool wide) {
    for (unsigned row=0; row<rows; ++row) {
        float acc[2][8]={};
#pragma HLS ARRAY_PARTITION variable=acc complete dim=0
        for (unsigned b=0; b<blocks; ++b) {
#pragma HLS PIPELINE II=1
            WeightBlock w=stream.read();
            for (int c=0; c<2; ++c) {
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
                        dot+=v*a;
                    }
                }
                acc[c][b%8]+=dot*(unpack(w.scale)*unpack(x[c][5*b+4]));
            }
        }
        result[row]=((Word)pack(sum8(acc[1]))<<32)|pack(sum8(acc[0]));
    }
}

static void quant_tile(const Word *weights, const Word x[2][760], Word result[64],
                       unsigned blocks, unsigned rows, bool wide) {
#pragma HLS DATAFLOW
    hls::stream<Word> words;
    hls::stream<WeightBlock> packed;
#pragma HLS STREAM variable=words depth=64
#pragma HLS STREAM variable=packed depth=32
    unsigned count=rows*blocks*(wide ? 5 : 3);
    fetch(weights,count,words);
    assemble(words,packed,count,wide);
    quant_rows(packed,x,result,blocks,rows,wide);
}

static void attention_tile(const Word *weights, const Word x[2][760],
                           Word result[64], unsigned stride, unsigned rows) {
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
    if (!width || width>4864 || !rows || rows>151936 || !cols || cols>2 || format>2) return;
    const unsigned blocks=width/32;
    const unsigned stride=format==0 ? blocks*3 : format==1 ? blocks*5 : width/2;
    const unsigned xs=format==2 ? width/2 : blocks*5;
    Word x[2][760];
#pragma HLS ARRAY_PARTITION variable=x complete dim=1
#pragma HLS ARRAY_PARTITION variable=x cyclic factor=5 dim=2
    if (!stride || stride>760 || xs>760 || (format==2 && stride>256)) return;
    for (unsigned c=0;c<2;++c) {
        // Duplicate column zero for a one-column tail. Both physical lanes are defined.
        unsigned base=input+(c<cols ? c*xs : 0);
        for (unsigned i=0;i<xs;++i) {
#pragma HLS PIPELINE II=1
            x[c][i]=mem[base+i];
        }
    }
    Word result[64];
    for (unsigned first=0;first<rows;first+=64) {
        unsigned n=rows-first<64 ? rows-first : 64;
        const Word *tile=mem+weight+first*stride;
        if (format==2) attention_tile(tile,x,result,stride,n);
        else quant_tile(tile,x,result,blocks,n,format==1);
        memcpy(mem+output+first,result,n*sizeof(Word));
    }
}

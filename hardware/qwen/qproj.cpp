#include <ap_int.h>
#include <hls_stream.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t Word;
union FloatWord { float f; uint32_t u; };
static float unpack(uint32_t bits) { FloatWord v; v.u=bits; return v.f; }
static uint32_t pack(float v) { FloatWord b; b.f=v; return b.u; }

typedef ap_uint<288> WeightBlock;
typedef ap_uint<128> PartialSums;

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
            w.range(63,0)=previous[0];
            w.range(127,64)=wide ? previous[1] : word;
            w.range(191,128)=previous[2];
            w.range(255,192)=word;
            w.range(287,256)=scale_bits(scales);
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

// A single decode vector uses all four lanes for four independent output rows.
// Two columns use row pairs from HP0 and HP2.
// For three/four columns, both lane pairs share the HP0 row.
static void quant_rows(hls::stream<WeightBlock>& first, hls::stream<WeightBlock>& second,
                       hls::stream<WeightBlock>& hp1, hls::stream<WeightBlock>& hp3,
                       const Word x[4][760], hls::stream<PartialSums>& tails,
                       unsigned blocks, unsigned rows, bool wide, bool split, bool quad) {
    float acc[4][8];
#pragma HLS ARRAY_PARTITION variable=acc complete dim=1
#pragma HLS RESOURCE variable=acc core=RAM_2P_LUTRAM
    unsigned b=0;
    const unsigned count=blocks*rows;
    for (unsigned step=0; step<count; ++step) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=acc inter distance=8 true
        WeightBlock left=first.read();
        WeightBlock right=split ? second.read() : left;
        WeightBlock lane1=quad ? hp1.read() : left;
        WeightBlock lane3=quad ? hp3.read() : right;
        PartialSums partial;
        for (unsigned c=0;c<4;++c) {
#pragma HLS UNROLL
            WeightBlock w=c==0 ? left : c==1 ? lane1 : c==2 ? right : lane3;
            int dot=0;
            for (unsigned p=0;p<4;++p) {
#pragma HLS UNROLL
                unsigned q=wide ? p : p%2;
                ap_uint<64> payload=w.range(64*q+63,64*q);
                ap_uint<64> activation=x[c][5*b+p];
                for (unsigned k=0;k<8;++k) {
#pragma HLS UNROLL
                    ap_uint<8> raw=payload.range(8*k+7,8*k);
                    ap_int<8> v=raw;
                    if (!wide) v=(int)(p<2 ? raw.range(3,0).to_uint() : raw.range(7,4).to_uint())-8;
                    ap_int<8> a=activation.range(8*k+7,8*k);
                    dot+=(k>=6 ? lut_product(v,a) : (int)(v*a));
                }
            }
            float last=b<8 ? 0.f : acc[c][b%8];
            float value=last+dot*(unpack(w.range(287,256))*unpack(x[c][5*b+4]));
            acc[c][b%8]=value;
            partial.range(32*c+31,32*c)=pack(value);
        }
        if (b+8>=blocks) tails.write(partial);
        b=(b+1==blocks) ? 0 : b+1;
    }
}

static void finish_rows(hls::stream<PartialSums>& tails, Word result[128],
                        unsigned blocks, unsigned rows, bool four, bool split, bool quad) {
    for (unsigned row=0;row<rows;++row) {
        float lane[4][8]={};
#pragma HLS ARRAY_PARTITION variable=lane complete dim=1
        unsigned count=blocks<8 ? blocks : 8;
        for (unsigned k=0;k<count;++k) {
#pragma HLS PIPELINE II=1
            PartialSums sums=tails.read();
            for (int c=0;c<4;++c) {
#pragma HLS UNROLL
                lane[c][(blocks-count+k)%8]=unpack(sums.range(32*c+31,32*c));
            }
        }
        float sums[4]={};
#pragma HLS ARRAY_PARTITION variable=sums complete
        for (int k=0;k<8;++k) {
            for (int c=0;c<4;++c) {
#pragma HLS UNROLL
                sums[c]+=lane[c][k];
            }
        }
        if (quad) {
            for (unsigned c=0;c<4;++c) result[row+c*rows]=pack(sums[c]);
        } else {
            unsigned dst=four ? 2*row : row;
            result[dst]=((Word)pack(sums[1])<<32)|pack(sums[0]);
            if (four || split) result[four ? dst+1 : dst+rows]=((Word)pack(sums[3])<<32)|pack(sums[2]);
        }
    }
}

static void quant_tile(const Word *weights, const Word *other, const Word *port1, const Word *port3,
                       const Word x[4][760],
                       Word result[128], unsigned blocks, unsigned groups, bool wide,
                       bool four, bool split, bool quad) {
#pragma HLS DATAFLOW
    hls::stream<Word> words0,words2,words1,words3;
    hls::stream<WeightBlock> packed0,packed2,packed1,packed3;
    hls::stream<PartialSums> tails;
#pragma HLS STREAM variable=words0 depth=64
#pragma HLS STREAM variable=words2 depth=64
#pragma HLS STREAM variable=packed0 depth=16
#pragma HLS STREAM variable=packed2 depth=16
#pragma HLS STREAM variable=words1 depth=64
#pragma HLS STREAM variable=words3 depth=64
#pragma HLS STREAM variable=packed1 depth=16
#pragma HLS STREAM variable=packed3 depth=16
#pragma HLS STREAM variable=tails depth=32
    unsigned count=groups*(blocks/4)*(wide ? 17 : 9);
    unsigned second=split ? count : 0;
    unsigned extra=quad ? count : 0;
    fetch(weights,count,words0);
    fetch(other,second,words2);
    fetch(port1,extra,words1);
    fetch(port3,extra,words3);
    assemble(words1,packed1,extra,wide);
    assemble(words3,packed3,extra,wide);
    assemble(words0,packed0,count,wide);
    assemble(words2,packed2,second,wide);
    quant_rows(packed0,packed2,packed1,packed3,x,tails,blocks,groups,wide,split,quad);
    finish_rows(tails,result,blocks,groups,four,split,quad);
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
void qwen_qproj(Word *mem, const Word *mem2, const Word *mem1, const Word *mem3,
                unsigned weight, unsigned input, unsigned output,
                unsigned width, unsigned rows, unsigned cols, unsigned format) {
#pragma HLS INTERFACE m_axi port=mem offset=off bundle=hp depth=134217728 max_read_burst_length=16 num_read_outstanding=32
#pragma HLS INTERFACE m_axi port=mem2 offset=off bundle=hp2 depth=134217728 max_read_burst_length=16 num_read_outstanding=32
#pragma HLS INTERFACE m_axi port=mem1 offset=off bundle=hp1 depth=134217728 max_read_burst_length=16 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=mem3 offset=off bundle=hp3 depth=134217728 max_read_burst_length=16 num_read_outstanding=16
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
    for (unsigned c=0;c<cols;++c) {
        for (unsigned i=0;i<xs;++i) {
#pragma HLS PIPELINE II=1
            Word v=mem[input+c*xs+i];
            x[c][i]=v;
            if (!four && c<2) x[c+2][i]=v;
            if (cols==1) {x[1][i]=v; x[3][i]=v;}
        }
    }
    Word result[128];
    for (unsigned first=0;first<rows;first+=64) {
        unsigned n=rows-first<64 ? rows-first : 64;
        const Word *tile=mem+weight+first*stride;
        if (format==2) attention_tile(tile,x,result,stride,n);
        else {
            bool split=!four && !(n&1);
            bool quad=cols==1 && !(n&3);
            unsigned groups=quad ? n/4 : split ? n/2 : n;
            unsigned offset1=weight+(first+(quad ? groups : 0))*stride;
            unsigned offset2=weight+(first+(quad ? 2*groups : split ? groups : 0))*stride;
            unsigned offset3=weight+(first+(quad ? 3*groups : 0))*stride;
            quant_tile(tile,mem2+offset2,mem1+offset1,mem3+offset3,x,result,blocks,groups,format==1,four,split,quad);
        }
        memcpy(mem+output+first*(four ? 2 : 1),result,n*(four ? 2 : 1)*sizeof(Word));
    }
}

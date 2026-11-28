#include <stdint.h>
#include <cmath>
#include <cstdio>
#include <vector>

void qwen_qproj(uint64_t *, const uint64_t *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
union FloatWord { uint32_t u; float f; };
static uint32_t bits(float value) {FloatWord v; v.f=value; return v.u;}
static float value(uint32_t bits) {FloatWord v; v.u=bits; return v.f;}

static bool run(unsigned width, unsigned rows, unsigned cols, unsigned fmt) {
    unsigned blocks=width/32, ws=fmt==2 ? width/2 : (blocks/4)*(fmt?17:9);
    unsigned xs=fmt==2 ? width/2 : blocks*5;
    unsigned input=rows*ws, output=input+cols*xs;
    unsigned pairs=(cols+1)/2;
    std::vector<uint64_t> mem(output+rows*pairs+1);
    for (unsigned r=0;r<rows;++r) {
        for (unsigned k=0;k<width;++k) {
            int q=(int)((r*3+k*11)% (fmt ? 255 : 16))-(fmt ? 127 : 8);
            if (fmt==2) mem[r*ws+k/2] |= (uint64_t)bits(q/8.f) << ((k%2)*32);
            else {
                unsigned i=k%32;
                unsigned header=r*ws+(k/128)*(fmt?17:9);
                unsigned pos=header+1+((k/32)%4)*(fmt?4:2)+(fmt?i/8:(i%16)/8);
                unsigned shift=(i%8)*8+(fmt?0:(i/16)*4);
                mem[pos] |= (uint64_t)(fmt ? (uint8_t)q : q+8) << shift;
                const uint16_t scales[]={0xb400,0xb000,0,0x3000,0x3400};
                mem[header]|=(uint64_t)scales[(r+k/32)%5] << (16*((k/32)%4));
            }
        }
    }
    for (unsigned c=0;c<cols;++c) for (unsigned k=0;k<width;++k) {
        int a=(int)((c*7+k*5)%253)-126;
        if (fmt==2) mem[input+c*xs+k/2] |= (uint64_t)bits(a/16.f)<<((k%2)*32);
        else {
            mem[input+c*xs+(k/32)*5+(k%32)/8] |= (uint64_t)(uint8_t)a<<((k%8)*8);
            mem[input+c*xs+(k/32)*5+4]=bits(((k/32)%3+1)/16.f);
        }
    }
    qwen_qproj(mem.data(),mem.data(),0,input,output,width,rows,cols,fmt);
    for (unsigned r=0;r<rows;++r) for (unsigned c=0;c<cols;++c) {
        double ref=0;
        for (unsigned k=0;k<width;++k) {
            int q=(int)((r*3+k*11)%(fmt?255:16))-(fmt?127:8);
            int a=(int)((c*7+k*5)%253)-126;
            ref+=fmt==2 ? q*a/128. : q*a*((int)((r+k/32)%5)-2)/8.*((k/32)%3+1)/16.;
        }
        float got=value(mem[output+r*pairs+c/2]>>(32*(c%2)));
        if (!std::isfinite(got) || std::fabs(ref-got)>0.001*(1+std::fabs(ref))) {
            std::printf("mismatch fmt=%u width=%u row=%u col=%u ref=%g got=%g\n",fmt,width,r,c,ref,got);
            return false;
        }
    }
    return mem[output+rows*pairs]==0;
}
// One nonzero product in each block exercises all four FP16 header lanes.
static bool half_scales() {
    const unsigned input=9, output=input+20;
    std::vector<uint64_t> mem(output+1,0);
    mem[0]=0x3c00800104000001ULL;
    for (unsigned b=0;b<4;++b) {
        mem[1+2*b]=0x8888888888888889ULL;
        mem[2+2*b]=0x8888888888888888ULL;
        mem[input+b*5+4]=bits(1.f);
    }
    const float expected[]={std::ldexp(1.f,-24),std::ldexp(1.f,-14),
                            -std::ldexp(1.f,-24),1.f};
    for (unsigned b=0;b<4;++b) {
        mem[input+b*5]=1;
        qwen_qproj(mem.data(),mem.data(),0,input,output,128,1,1,0);
        if (value(mem[output])!=expected[b]) return false;
        mem[input+b*5]=0;
    }
    return true;
}

int main() {
    if (!half_scales()) return 1;
    for (unsigned c=1;c<=2;++c)
        if (!run(896,65,c,0)||!run(4864,7,c,0)||!run(896,67,c,1)||!run(512,64,c,2)) return 1;
    for (unsigned c=3;c<=4;++c)
        if (!run(896,65,c,0)||!run(4864,7,c,0)||!run(896,67,c,1)) return 1;
    std::puts("PASS Q4 Q8 FP32; one to four columns; tile tails and 4864-wide FFN");
}

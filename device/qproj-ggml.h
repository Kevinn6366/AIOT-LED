/* Board-specific backend for ggml MUL_MAT. Only ggml thread zero owns HP0. */
#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    QBASE = 0x20000000, QWINDOW = 0x1b000000,
    QDYNAMIC = 0x1a400000, QINPUT = 0x1a600000, QOUTPUT = 0x1a700000,
    QLAYERS = 24, QKINDS = 10, QWEIGHTS = QLAYERS * 7 + 1
};
static const char *const qnames[] = {"attn_q", "attn_k", "attn_v", "attn_output",
                                     "ffn_gate", "ffn_up", "ffn_down"};
static struct {const void *source; unsigned offset;} qcache[QWEIGHTS];
int aiot_use_ps;  /* Set by the single-slot scheduler before dispatching the graph. */
static unsigned qcount, qnext;
static volatile uint32_t *qmem, *qctrl, *qclock;
static uint64_t qcalls, qcycles, qlayer[QLAYERS], qkind[QKINDS];

static uint32_t qbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return bits;
}

static volatile uint32_t *qmap(int fd, off_t addr, size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr);
    GGML_ASSERT(p != MAP_FAILED);
    return p;
}

static void qinit(void) {
    if (qmem) return;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    GGML_ASSERT(fd >= 0);
    qmem = qmap(fd, QBASE, QWINDOW);
    qctrl = qmap(fd, 0x43c10000, 4096);
    qclock = qmap(fd, 0x41220000, 4096);
    close(fd);
}

static void qpack(volatile uint32_t *to, const unsigned char *from,
                  int blocks, bool wide) {
    const int words = wide ? 8 : 4;
    for (int b=0; b<blocks; ++b) {
        ggml_fp16_t scale;
        memcpy(&scale, from, 2);
        for (int j=0; j<words; ++j) {
            uint32_t word;
            memcpy(&word, from+2+4*j, 4);
            to[j] = word;
        }
        to[words] = qbits(GGML_FP16_TO_FP32(scale));
        to[words+1] = 0;
        to += words+2;
        from += 2+words*4;
    }
}

static unsigned qresident(const struct ggml_tensor *w, bool wide) {
    for (unsigned i=0;i<qcount;++i)
        if (qcache[i].source == w->data) return qcache[i].offset;
    int blocks = w->ne[0]/32 * w->ne[1];
    unsigned size = blocks*(wide ? 40u : 24u);
    GGML_ASSERT(qcount < QWEIGHTS && size <= QDYNAMIC-qnext);
    unsigned offset = qnext;
    qpack(qmem+offset/4, w->data, blocks, wide);
    qcache[qcount].source=w->data;
    qcache[qcount++].offset=offset;
    qnext += size;
    return offset;
}

static void qstats(void) {
    FILE *f=fopen("/tmp/aiot-led/qproj.stats.new","w");
    GGML_ASSERT(f);
    fprintf(f,"%" PRIu64 " %" PRIu64, qcalls,qcycles);
    for (int i=0;i<QLAYERS;++i) fprintf(f," %" PRIu64,qlayer[i]);
    for (int i=0;i<QKINDS;++i) fprintf(f," %" PRIu64,qkind[i]);
    fputc('\n',f);
    GGML_ASSERT(fclose(f)==0 && rename("/tmp/aiot-led/qproj.stats.new","/tmp/aiot-led/qproj.stats")==0);
}

static void qlaunch(unsigned offset,int width,int rows,int columns,int format) {
    qctrl[4]=(QBASE+offset)/8;
    qctrl[6]=(QBASE+QINPUT)/8;
    qctrl[8]=(QBASE+QOUTPUT)/8;
    qctrl[10]=width;
    qctrl[12]=rows;
    qctrl[14]=columns;
    qctrl[16]=format;
    __sync_synchronize();
    uint32_t start=qclock[0];
    qctrl[0]=1;
    int64_t deadline=ggml_time_ms()+30000;
    while (!(qctrl[0]&2)) {
        GGML_ASSERT(ggml_time_ms()<deadline);
        if (rows>512) usleep(50);
    }
    qcycles+=(uint32_t)(qclock[0]-start);
    ++qcalls;
    __sync_synchronize();
}

static void qall_matmul(const struct ggml_tensor *w, const void *x,
                        struct ggml_tensor *out, size_t xs, size_t xhead) {
    qinit();
    const int width=w->ne[0], rows=w->ne[1], heads=out->ne[2], columns=out->ne[1];
    int layer=-1,kind=-1;
    const bool fp=w->type==GGML_TYPE_F16;
    const bool wide=w->type==GGML_TYPE_Q8_0;
    GGML_ASSERT(w->ne[3]==1 && out->ne[3]==1 && width%2==0 && width<=4864 &&
                rows<=151936 && heads%w->ne[2]==0);
    if (fp) {
        if (sscanf(out->name,"kqv-%d",&layer)==1) kind=8;
        else if (sscanf(out->name,"kq-%d",&layer)==1) kind=7;
        GGML_ASSERT(kind>=0 && width<=512 && rows<=512);
    } else if (wide && !strcmp(w->name,"output.weight")) {
        kind=9;
    } else {
        char name[32];
        GGML_ASSERT(w->type==GGML_TYPE_Q4_0 && sscanf(w->name,"blk.%d.%31[^.]",&layer,name)==2);
        for (int i=0;i<7;++i) if (!strcmp(name,qnames[i])) kind=i;
        GGML_ASSERT(kind>=0);
    }
    GGML_ASSERT(kind==9 || (layer>=0 && layer<QLAYERS));
    unsigned offset=fp ? QDYNAMIC : qresident(w,wide);
    bool verify=qkind[kind]==0;
    for (int h=0; h<heads; ++h) {
        int wh=h/(heads/w->ne[2]);
        const char *src=(const char *)w->data+wh*w->nb[2];
        if (fp && h%(heads/w->ne[2])==0) {
            for (int r=0;r<rows;++r) {
                const ggml_fp16_t *row=(const ggml_fp16_t *)(src+r*w->nb[1]);
                for (int k=0;k<width;++k) qmem[QDYNAMIC/4+r*width+k]=qbits(GGML_FP16_TO_FP32(row[k]));
            }
        }
        for (int c=0;c<columns;c+=2) {
            const int group=columns-c<2 ? columns-c : 2;
            for (int j=0;j<group;++j) {
                const unsigned char *v=(const unsigned char *)x+h*xhead+(c+j)*xs;
                if (fp) {
                    const ggml_fp16_t *half=(const ggml_fp16_t *)v;
                    for (int k=0;k<width;++k) qmem[QINPUT/4+j*width+k]=qbits(GGML_FP16_TO_FP32(half[k]));
                } else qpack(qmem+QINPUT/4+j*(width/32)*10,v,width/32,true);
            }
            qlaunch(offset,width,rows,group,fp ? 2 : wide ? 1 : 0);
            for (int j=0;j<group;++j) {
                float *y=(float *)((char *)out->data+h*out->nb[2]+(c+j)*out->nb[1]);
                for (int r=0;r<rows;++r) {
                    uint32_t word=qmem[QOUTPUT/4+r*2+j];
                    memcpy(y+r,&word,4);
                    if (verify && (r<8 || r==rows-1)) {
                        float ref;
                        const char *v=(const char *)x+h*xhead+(c+j)*xs;
                        type_traits[w->type].vec_dot(width,&ref,0,src+r*w->nb[1],0,v,0,1);
                        GGML_ASSERT(isfinite(y[r]) && fabsf(ref-y[r])<0.001f*(1+fabsf(ref)));
                    }
                }
            }
            ++qkind[kind];
            if (layer>=0) ++qlayer[layer];
        }
    }
    if (kind==9) qstats();
}

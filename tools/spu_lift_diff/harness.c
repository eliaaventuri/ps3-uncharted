/* Banco differenziale del lifter SPU (s.44). Esegue ogni ingresso registrato
 * di un modulo di job liftato da stati casuali deterministici (LS con
 * l'immagine a 0x4000, gpr casuali) e stampa per ogni caso un hash dello
 * stato finale (gpr, LS, pc, canali). Due lift dello stesso modulo che devono
 * essere equivalenti (es. con e senza --reg-cache) danno righe identiche.
 *   <exe> <immagine.bin> <casi> [bench [caso da stampare per intero]]
 * Variabili: GEN_FLOAT=1 dati "da gioco" (float piccoli, niente overflow);
 * EVERY=n un'uscita al trampolino ogni n archi all'indietro (0 = mai, prima
 * del limite); TB/HB limiti di archi e di salti (20000).
 * I casi finiscono a stop/halt, salto a LS 0, pc non registrato o limite:
 * sempre a un salto del trampolino, dove ctx->gpr e' completo in tutte e due
 * le versioni. Limite noto: i NaN delle operazioni double (spu_dfm...) hanno
 * il payload scelto dall'ordine degli operandi, che il compilatore puo'
 * scambiare -- divergenze solo in 0091dc40, sparite con NaN canonici. */
#include "hdefs.h"
#include "spu_context.h"
#include "spu_helpers.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;
int g_spu_drain_mode = 1;
int g_wws_read_probe = 0, g_wws_code_probe = 0;

static jmp_buf g_end; static int g_reason;
volatile int g_h_one = 1, g_h_zero = 0;
unsigned long long g_h_ticks, g_h_budget; unsigned g_h_every, g_h_count;
#define g_ticks g_h_ticks
#define g_tick_budget g_h_budget
#define g_every g_h_every
static uint64_t g_hops, g_hop_budget;
static uint64_t g_chan_h, g_rd_n;
static void end_case(int why) { g_reason = why; longjmp(g_end, 1); }

#define MAXF 8192
static uint32_t f_addr[MAXF]; static void (*f_fn[MAXF])(spu_context*); static int nf;
void spu_register_function(uint32_t a, void (*f)(spu_context*)) { if (nf < MAXF) { f_addr[nf] = a; f_fn[nf] = f; nf++; } }
static void (*lookup(uint32_t a))(spu_context*) { for (int i = 0; i < nf; i++) if (f_addr[i] == a) return f_fn[i]; return 0; }

void spu_indirect_branch(spu_context* ctx) {
    void (*f)(spu_context*) = lookup((uint32_t)ctx->pc & SPU_LS_MASK);
    if (!f) end_case(2);
    f(ctx);
}
void spu_indirect_branch_mt(spu_context* ctx) { spu_indirect_branch(ctx); }
int spu_retlink_enabled(void) { return 1; }
int spu_tailret_enabled(void) { return 0; }
void spu_drain_fast(spu_context* ctx) {
    void (*tf)(spu_context*);
    while ((tf = g_spu_trampoline_fn) != 0) {
        if (++g_hops > g_hop_budget) end_case(3);
        if (ctx->host_depth > 0 && ctx->ret_link && ((uint32_t)ctx->pc & SPU_LS_MASK) == ctx->ret_link) {
            g_spu_trampoline_fn = 0; return; }
        g_spu_trampoline_fn = 0;
        if ((ctx->pc & SPU_LS_MASK) == 0) end_case(4);
        tf(ctx);
    }
}
void spu_drain_slow(spu_context* ctx) { spu_drain_fast(ctx); }
void spu_depth_guard(spu_context* ctx) { (void)ctx; end_case(5); }
void spu_img_restore(spu_context* ctx, int32_t s) { ctx->image_id = s; }
void spu_stop(spu_context* ctx) { (void)ctx; end_case(6); }
void spu_stop_at(spu_context* ctx, uint32_t a) { (void)ctx; (void)a; end_case(7); }
void spu_halt(spu_context* ctx) { (void)ctx; end_case(8); }
static uint64_t mix(uint64_t h, uint64_t v) { h ^= v; h *= 0x100000001B3ull; return h ^ (h >> 29); }
u128 spu_rdch(spu_context* ctx, uint32_t ch) {
    (void)ctx; g_rd_n++; u128 r; memset(&r, 0, sizeof r);
    uint64_t x = mix(g_rd_n * 0x9E3779B97F4A7C15ull, ch);
    r._u32[0] = (x & 1) ? (uint32_t)(x >> 8) & 0x3FFF0 : (uint32_t)(x >> 16) & 0xFF;
    g_chan_h = mix(g_chan_h, 0x1000 + ch); return r; }
uint32_t spu_rchcnt(spu_context* ctx, uint32_t ch) { (void)ctx; g_chan_h = mix(g_chan_h, 0x2000 + ch); return 1; }
void spu_wrch(spu_context* ctx, uint32_t ch, u128 v) {
    (void)ctx; g_chan_h = mix(g_chan_h, 0x3000 + ch); g_chan_h = mix(g_chan_h, v._u32[0]); }
/* altri agganci del runtime che il codice liftato puo' nominare */
void yz_lockstep_tick(spu_context* c) { (void)c; }
void spu_task_launch_check(spu_context* c, void* f) { (void)c; (void)f; }
void (*spu_take_interrupt(spu_context* c, void (*tf)(spu_context*)))(spu_context*) { (void)c; return tf; }

void REGFN(void);
static uint64_t rs;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int g_gen_float = -1;
static uint32_t rword(void) {
    uint64_t x = rnd();
    if (g_gen_float < 0) g_gen_float = getenv("GEN_FLOAT") != 0;
    if (g_gen_float) switch (x & 7) {   /* dati "da gioco": niente overflow nei float */
    case 0: case 1: case 2: case 3: { float f = (float)((int64_t)(x >> 20) % 8000) / 2000.0f; uint32_t w; memcpy(&w, &f, 4); return w; }
    case 4: case 5: return (uint32_t)(x >> 8) & 0x3FFF0;
    default: return (uint32_t)(x >> 8) & 0xFF;
    }
    switch (x & 7) {
    case 0: case 1: { float f = (float)((int64_t)(x >> 20) % 20000) / 1000.0f; uint32_t w; memcpy(&w, &f, 4); return w; }
    case 2: case 3: return (uint32_t)(x >> 8) & 0x3FFF0;   /* indirizzo LS */
    case 4: return (uint32_t)(x >> 8) & 0xFF;
    case 5: return 0;
    default: return (uint32_t)(x >> 32);
    }
}
static spu_context* ctx; static uint8_t* image; static size_t image_n;
static void init_state(uint64_t seed) {
    rs = seed * 0x9E3779B97F4A7C15ull + 1;
    for (uint32_t i = 0; i < SPU_LS_SIZE; i += 4) { uint32_t w = rword(); memcpy(&ctx->ls[i], &w, 4); }
    if (image) memcpy(&ctx->ls[0x4000], image, image_n);
    for (int r = 0; r < 128; r++) for (int j = 0; j < 4; j++) ctx->gpr[r]._u32[j] = rword();
    ctx->gpr[1]._u32[0] = 0x3F000 + ((rnd() & 0x3F) << 4);   /* stack */
    ctx->gpr[0]._u32[0] = 0;
}
static uint64_t hash_state(void) {
    uint64_t h = 1469598103934665603ull;
    const uint64_t* g = (const uint64_t*)ctx->gpr; for (int i = 0; i < 256; i++) h = mix(h, g[i]);
    const uint64_t* l = (const uint64_t*)ctx->ls; for (int i = 0; i < SPU_LS_SIZE / 8; i++) h = mix(h, l[i]);
    return mix(mix(h, (uint32_t)ctx->pc & SPU_LS_MASK), g_chan_h);
}
static int run_one(uint32_t addr) {
    g_ticks = 0; g_h_count = g_every; g_hops = 0; g_chan_h = 0; g_rd_n = 0; g_reason = 0;
    ctx->host_depth = 0; ctx->ret_link = 0; ctx->int_enable = 0; ctx->event_status = 0; ctx->event_mask = 0;
    ctx->pc = addr; g_spu_trampoline_fn = spu_indirect_branch;
    if (!setjmp(g_end)) { spu_drain_fast(ctx); g_reason = 1; }
    g_spu_trampoline_fn = 0;
    return g_reason;
}
static int g_argc; static char** g_argv;
static void* body(void* unused) {
    (void)unused;
    int cases = atoi(g_argv[2]); int bench = g_argc > 3 && !strcmp(g_argv[3], "bench");
    ctx = aligned_alloc(64, (sizeof(spu_context) + 63) & ~(size_t)63); spu_context_init(ctx, 0); ctx->image_id = 200;
    REGFN();
    double t = 0;
    for (int c = 0; c < cases; c++) {
        uint32_t addr = f_addr[c % nf];
        g_every = getenv("EVERY") ? (unsigned)atoi(getenv("EVERY")) : (unsigned[]){1, 3, 16, 256, 0}[(c / nf) % 5];
        g_tick_budget = getenv("TB") ? atoll(getenv("TB")) : 20000; g_hop_budget = getenv("HB") ? atoll(getenv("HB")) : 20000;
        init_state((uint64_t)c);
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        int why = run_one(addr);
        clock_gettime(CLOCK_MONOTONIC, &b);
        t += (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
        if (g_argc > 4 && atoi(g_argv[4]) == c) {
            for (int r = 0; r < 128; r++) printf("r%d %08x %08x %08x %08x\n", r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3]);
            for (int pg = 0; pg < SPU_LS_SIZE; pg += 4096) { uint64_t h = 0; for (int i = 0; i < 4096; i += 8) h = mix(h, *(uint64_t*)&ctx->ls[pg + i]); printf("ls %05X %016llx\n", pg, (unsigned long long)h); }
            printf("pc %05X chan %016llx depth %u\n", (uint32_t)ctx->pc & SPU_LS_MASK, (unsigned long long)g_chan_h, ctx->host_depth);
        }
        if (!bench) printf("%d %05X e%u r%d t%llu h%llu %016llx\n", c, addr, g_every, why,
                           (unsigned long long)g_ticks, (unsigned long long)g_hops, (unsigned long long)hash_state());
    }
    fprintf(stdout, "# %d casi, %d funzioni, %.3f s dentro il codice liftato\n", cases, nf, t);
    return 0;
}
int main(int argc, char** argv) {
    g_argc = argc; g_argv = argv;
    if (argc < 3) { fprintf(stderr, "uso: %s <immagine.bin|-> <casi> [bench]\n", argv[0]); return 2; }
    if (strcmp(argv[1], "-")) { FILE* f = fopen(argv[1], "rb"); if (f) { image = malloc(0x3C000); image_n = fread(image, 1, 0x3C000, f); fclose(f); } }
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1u << 30);
    pthread_t th; pthread_create(&th, &at, body, 0); pthread_join(th, 0);
    return 0;
}

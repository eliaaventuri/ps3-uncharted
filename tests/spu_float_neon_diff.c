/* Differenziale delle vie veloci NEON dei float SPU (s.49, spu_helpers.h)
 * contro le vie esatte *_slow: fma/fms/fnms/fa/fs/fm col controllo del
 * risultato che lascia passare il massimo normale, e spu_frsqest in double a
 * due corsie, con l'FPCR delle lane (FZ + verso lo zero, spu_host_fp_enter;
 * senza FZ le vie veloci non valgono gia' da prima: denormali). Ingressi con molti
 * estesi, zeri, denormali e valori grandi (overflow). Compilare e lanciare:
 *
 *   clang -O2 -I runtime/spu -I runtime -o /tmp/spu_fnd tests/spu_float_neon_diff.c && /tmp/spu_fnd
 *
 * Non fa parte della build. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "spu_helpers.h"

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 16); }
static uint32_t word(void) {
    uint32_t r = rnd(), sg = rnd() & 0x80000000u;
    switch (r % 10) {
    case 0: return sg | 0x7F800000u | (rnd() & 0x7FFFFFu);   /* esteso */
    case 1: return sg | 0x7FFF0000u;
    case 2: return sg;                                        /* +-0 */
    case 3: return sg | (rnd() & 0x7FFFFFu);                  /* denormale */
    case 4: return sg | 0x7F7FFFFFu;
    case 5: case 6: return sg | ((0xF0 + rnd() % 15) << 23) | (rnd() & 0x7FFFFFu);   /* grandi: overflow */
    default: return sg | ((64 + rnd() % 127) << 23) | (rnd() & 0x7FFFFFu); } }
static u128 vec(void) { u128 v; for (int i = 0; i < 4; i++) v._u32[i] = word(); return v; }

static long n, bad;
static void chk(const char* op, u128 g, u128 r, u128 a) {
    n++; if (memcmp(&g, &r, 16) && bad++ < 10)
        printf("%s a0=%08X -> %08X %08X %08X %08X / %08X %08X %08X %08X\n", op, a._u32[0],
               g._u32[0], g._u32[1], g._u32[2], g._u32[3], r._u32[0], r._u32[1], r._u32[2], r._u32[3]); }

int main(void) {
    uint64_t old; __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    for (int mode = 0; mode < 1; mode++) {
        uint64_t f = mode ? old : (old | (1ull << 24) | (3ull << 22));
        __asm__ volatile("msr fpcr, %0" :: "r"(f));
        for (long k = 0; k < 5000000; k++) {
            u128 a = vec(), b = vec(), c = vec();
            chk("fma",  spu_fma(a, b, c),  spu_fma_slow(a, b, c), a);
            chk("fms",  spu_fms(a, b, c),  spu_fms_slow(a, b, c), a);
            chk("fnms", spu_fnms(a, b, c), spu_fnms_slow(a, b, c), a);
            chk("fa",   spu_fa(a, b),      spu_fa_slow(a, b), a);
            chk("fs",   spu_fs(a, b),      spu_fs_slow(a, b), a);
            chk("fm",   spu_fm(a, b),      spu_fm_slow(a, b), a);
            chk("frsqest", spu_frsqest(a), spu_frsqest_slow(a), a);
        }
        printf("FPCR %s: %ld confronti, %ld differenze\n", mode ? "default" : "lane (FZ+RZ)", n, bad);
    }
    return bad != 0;
}

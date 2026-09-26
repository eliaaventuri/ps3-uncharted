/* Differenziale delle operazioni SPU per corsia in NEON (s.47,
 * runtime/spu/spu_helpers_neon_arith.h) contro le versioni scalari di
 * spu_helpers.h: `spu_a(x, y)` passa dalla macro NEON, `(spu_a)(x, y)` chiama
 * la funzione scalare. Ingressi casuali con valori di bordo mescolati,
 * immediati su tutto l'intervallo. Compilare e lanciare:
 *
 *   clang -O2 -I runtime/spu -I runtime -o /tmp/spu_nad tests/spu_neon_arith_diff.c && /tmp/spu_nad
 *
 * Non fa parte della build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "spu_helpers.h"

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 16); }
static const uint32_t edge[] = { 0, 1, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0xFFFF, 0x10000, 0x8000, 0x7FFF,
                                 0xFFFF8000u, 0x80, 0x7F, 0xFF, 0x80808080u, 0x7F7F7F7Fu };
static uint32_t word(void) { uint32_t r = rnd(); return (r & 7) == 0 ? edge[rnd() % (sizeof edge / sizeof edge[0])] : rnd(); }
static u128 vec(void) { u128 v; for (int i = 0; i < 4; i++) v._u32[i] = word(); return v; }
static int32_t imm(void) {
    switch (rnd() % 4) {
    case 0: return (int32_t)(rnd() % 128) - 64;         /* scorrimenti, anche fuori corsia */
    case 1: return (int32_t)(rnd() & 0x3FF) - 0x200;    /* i10 */
    case 2: return (int32_t)(int16_t)rnd();             /* i16 */
    default: return (int32_t)word(); } }

static long n, bad;
static void check(const char* name, u128 got, u128 ref, u128 a, u128 b, int32_t k) {
    n++;
    if (memcmp(&got, &ref, 16) == 0) return;
    if (++bad <= 20)
        printf("DIFF %-8s a=%08X %08X %08X %08X b=%08X %08X %08X %08X k=%d\n  neon %08X %08X %08X %08X\n  ref  %08X %08X %08X %08X\n",
               name, a._u32[0], a._u32[1], a._u32[2], a._u32[3], b._u32[0], b._u32[1], b._u32[2], b._u32[3], k,
               got._u32[0], got._u32[1], got._u32[2], got._u32[3], ref._u32[0], ref._u32[1], ref._u32[2], ref._u32[3]);
}
#define B2(f)  check(#f, f(a, b), (f)(a, b), a, b, 0)
#define B3(f)  check(#f, f(a, b, c), (f)(a, b, c), a, b, 0)
#define I1(f)  check(#f, f(a, k), (f)(a, k), a, b, k)
#define U1(f)  check(#f, f(a), (f)(a), a, b, 0)

int main(void) {
    for (int it = 0; it < 200000; it++) {
        u128 a = vec(), b = vec(), c = vec(); int32_t k = imm();
        B2(spu_a); B2(spu_sf); B2(spu_ah); B2(spu_sfh);
        I1(spu_ai); I1(spu_ahi); I1(spu_sfi); I1(spu_sfhi);
        B2(spu_and); B2(spu_or); B2(spu_xor); B2(spu_nand); B2(spu_nor); B2(spu_andc); B2(spu_orc); B2(spu_eqv);
        B3(spu_selb);
        I1(spu_andi); I1(spu_ori); I1(spu_xori); I1(spu_andhi); I1(spu_orhi); I1(spu_xorhi);
        I1(spu_andbi); I1(spu_orbi); I1(spu_xorbi);
        check("spu_iohl", spu_iohl(a, (uint16_t)k), (spu_iohl)(a, (uint16_t)k), a, b, k);
        I1(spu_ceqi); I1(spu_cgti); I1(spu_clgti); I1(spu_ceqhi); I1(spu_cgthi); I1(spu_clgthi);
        I1(spu_ceqbi); I1(spu_cgtbi); I1(spu_clgtbi);
        I1(spu_shli); I1(spu_shlhi); I1(spu_rotmi); I1(spu_rotmai); I1(spu_rotmhi); I1(spu_rothmi); I1(spu_rotmahi);
        I1(spu_roti); I1(spu_rothi); I1(spu_rotqbyi);
        B2(spu_mpy); B3(spu_mpya); B2(spu_mpyu); I1(spu_mpyi); I1(spu_mpyui); B2(spu_mpyh); B2(spu_mpyhh);
        U1(spu_xshw); U1(spu_xsbh);
        /* uguaglianze con un immediato estratto dall'ingresso: senza, ceqi & co. danno quasi sempre 0 */
        k = (int32_t)a._u32[rnd() & 3];
        I1(spu_ceqi); I1(spu_ceqhi); I1(spu_ceqbi);
    }
    printf("%ld confronti, %ld differenze\n", n, bad);
    return bad != 0;
}

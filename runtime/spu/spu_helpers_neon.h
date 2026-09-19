/* Helper SPU in NEON (sessione 36). Incluso da spu_helpers.h DOPO u128/SPU_W e
 * PRIMA delle versioni scalari, che restano sotto `#ifndef SPU_NEON_xxx` per
 * gli host senza NEON. Misurato con `sample` a -fno-inline sui job di
 * geometria e skinning di Uncharted: cntb (ciclo bit a bit su 16 byte),
 * fcgt/fcmgt (via double per corsia), rotqby/shlqby/rotqmby (cicli di byte
 * con SPU_W) e i generatori cwd/chd erano fra gli helper piu' caldi. */
#ifndef SPU_HELPERS_NEON_H
#define SPU_HELPERS_NEON_H
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#include "spu_neon_tables.h"

static inline u128 spu__from_u8(uint8x16_t v) { u128 r; vst1q_u8(r._u8, v); return r; }
static inline u128 spu__from_u32(uint32x4_t v) { u128 r; vst1q_u32(r._u32, v); return r; }
static inline u128 spu__tbl(u128 a, const uint8_t* idx) {
    return spu__from_u8(vqtbl1q_u8(vld1q_u8(a._u8), vld1q_u8(idx))); }

/* ---- byte shift/rotate del quadword (tabelle di indici, vedi spu_neon_tables.h) ---- */
#define SPU_NEON_BYTESHIFT 1
static inline u128 spu_rotqby(u128 a, u128 b)   { return spu__tbl(a, spu__rotqby_tbl[b._u32[0] & 0x0F]); }
static inline u128 spu_rotqbybi(u128 a, u128 b) { return spu__tbl(a, spu__rotqby_tbl[(b._u32[0] >> 3) & 0x0F]); }
static inline u128 spu_shlqby(u128 a, u128 b)   { unsigned sh = b._u32[0] & 0x1F; return spu__tbl(a, spu__shlqby_tbl[sh > 16 ? 16 : sh]); }
static inline u128 spu_shlqbybi(u128 a, u128 b) { unsigned sh = (b._u32[0] >> 3) & 0x1F; return spu__tbl(a, spu__shlqby_tbl[sh > 16 ? 16 : sh]); }
static inline u128 spu_shlqbyi(u128 a, int sh)  { unsigned s = (unsigned)sh & 0x1F; return spu__tbl(a, spu__shlqby_tbl[s > 16 ? 16 : s]); }
static inline u128 spu_rotqmby(u128 a, u128 b)   { unsigned sh = (0u - b._u32[0]) & 0x1F; return spu__tbl(a, spu__rotqmby_tbl[sh > 16 ? 16 : sh]); }
static inline u128 spu_rotqmbybi(u128 a, u128 b) { unsigned sh = (0u - (b._u32[0] >> 3)) & 0x1F; return spu__tbl(a, spu__rotqmby_tbl[sh > 16 ? 16 : sh]); }
static inline u128 spu_rotqmbyi(u128 a, int i7)  { unsigned sh = (0u - (unsigned)i7) & 0x1F; return spu__tbl(a, spu__rotqmby_tbl[sh > 16 ? 16 : sh]); }

/* ---- generatori dei controlli di inserimento (cbd/chd/cwd/cdd, cbx/...) ---- */
#define SPU_NEON_GEN 1
static inline u128 spu_cbd_pos(int pos) { u128 r; memcpy(r._u8, spu__cbd_tbl[pos & 0xF], 16); return r; }
static inline u128 spu_chd_pos(int pos) { u128 r; memcpy(r._u8, spu__chd_tbl[pos & 0xF], 16); return r; }
static inline u128 spu_cwd_pos(int pos) { u128 r; memcpy(r._u8, spu__cwd_tbl[pos & 0xF], 16); return r; }
static inline u128 spu_cdd_pos(int pos) { u128 r; memcpy(r._u8, spu__cdd_tbl[pos & 0xF], 16); return r; }

/* ---- cntb: popcount per byte ---- */
#define SPU_NEON_CNTB 1
static inline u128 spu_cntb(u128 a) { return spu__from_u8(vcntq_u8(vld1q_u8(a._u8))); }

/* ---- confronti in virgola mobile: esatti come la via scalare in double
 * (i valori estesi, esponente 255, sono numeri finiti PIU' GRANDI di ogni
 * normale, non NaN; i denormali valgono zero; -0 == +0). Per i pattern
 * senza NaN l'ordine dei float e' l'ordine degli interi segno-modulo: chiave
 * = w ^ (segno ? 0x7FFFFFFF : 0) e confronto con segno. RPCS3 "abbassa" gli
 * estesi al massimo normale con la mantissa intatta: per ext vs 0x7F7FFFFF
 * risponde al contrario del ferro (verificato col differenziale). ---- */
#define SPU_NEON_FCMP 1
static inline int32x4_t spu__fkey(uint32x4_t w) {
    const uint32x4_t absm = vdupq_n_u32(0x7FFFFFFFu);
    w = vbicq_u32(w, vceqq_u32(vandq_u32(w, vdupq_n_u32(0x7F800000u)), vdupq_n_u32(0)));  /* denormale/zero -> +0 */
    uint32x4_t sgn = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(w), 31));
    return vreinterpretq_s32_u32(veorq_u32(w, vandq_u32(sgn, absm))); }
static inline u128 spu_fcgt(u128 a, u128 b) {
    return spu__from_u32(vcgtq_s32(spu__fkey(vld1q_u32(a._u32)), spu__fkey(vld1q_u32(b._u32)))); }
static inline u128 spu_fcmgt(u128 a, u128 b) {
    const uint32x4_t absm = vdupq_n_u32(0x7FFFFFFFu);
    uint32x4_t x = vandq_u32(vld1q_u32(a._u32), absm), y = vandq_u32(vld1q_u32(b._u32), absm);
    x = vbicq_u32(x, vceqq_u32(vandq_u32(x, vdupq_n_u32(0x7F800000u)), vdupq_n_u32(0)));
    y = vbicq_u32(y, vceqq_u32(vandq_u32(y, vdupq_n_u32(0x7F800000u)), vdupq_n_u32(0)));
    return spu__from_u32(vcgtq_u32(x, y)); }

/* ---- conversioni float<->int con scala (CBEA: cflts/cfltu x 2^(173-i8),
 * csflt/cuflt x 2^(i8-155)). vcvtq_s32_f32 tronca e satura come la SPU; gli
 * ingressi estesi (per il ferro NaN/inf) saturano secondo il segno; i
 * denormali contano zero (FZ nell'FPCR, vedi spu_host_fp_enter). ---- */
#define SPU_NEON_CVT 1
static inline float32x4_t spu__scale4(int k) {        /* 2^k come float, k in [-126,127] */
    if (k > 127) k = 127;
    if (k < -126) return vdupq_n_f32(ldexpf(1.0f, k)); /* denormale: raro, via libm */
    uint32_t w = (uint32_t)(k + 127) << 23; float f; memcpy(&f, &w, 4); return vdupq_n_f32(f); }
static inline u128 spu_cflts(u128 a, int i8) {
    uint32x4_t w = vld1q_u32(a._u32);
    const uint32x4_t expm = vdupq_n_u32(0x7F800000u);
    uint32x4_t ext = vceqq_u32(vandq_u32(w, expm), expm);
    int32x4_t v = vcvtq_s32_f32(vmulq_f32(vreinterpretq_f32_u32(w), spu__scale4(173 - i8)));
    /* esteso: +0x7FFFFFFF o -0x80000000 secondo il segno */
    int32x4_t sat = veorq_s32(vdupq_n_s32(0x7FFFFFFF), vshrq_n_s32(vreinterpretq_s32_u32(w), 31));
    return spu__from_u32(vreinterpretq_u32_s32(vbslq_s32(ext, sat, v))); }
static inline u128 spu_cfltu(u128 a, int i8) {
    uint32x4_t w = vld1q_u32(a._u32);
    const uint32x4_t expm = vdupq_n_u32(0x7F800000u);
    uint32x4_t ext = vceqq_u32(vandq_u32(w, expm), expm);
    uint32x4_t v = vcvtq_u32_f32(vmulq_f32(vreinterpretq_f32_u32(w), spu__scale4(173 - i8)));  /* negativi -> 0 */
    uint32x4_t sat = vbicq_u32(vdupq_n_u32(0xFFFFFFFFu), vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(w), 31)));
    return spu__from_u32(vbslq_u32(ext, sat, v)); }
static inline u128 spu_csflt(u128 a, int i8) {
    return spu__from_u32(vreinterpretq_u32_f32(vmulq_f32(vcvtq_f32_s32(vld1q_s32(a._s32)), spu__scale4(i8 - 155)))); }
static inline u128 spu_cuflt(u128 a, int i8) {
    return spu__from_u32(vreinterpretq_u32_f32(vmulq_f32(vcvtq_f32_u32(vld1q_u32(a._u32)), spu__scale4(i8 - 155)))); }

/* ---- confronti interi, differenza assoluta, somme di byte ---- */
#define SPU_NEON_ICMP 1
static inline u128 spu_ceq(u128 a, u128 b)   { return spu__from_u32(vceqq_u32(vld1q_u32(a._u32), vld1q_u32(b._u32))); }
static inline u128 spu_ceqh(u128 a, u128 b)  { return spu__from_u8(vreinterpretq_u8_u16(vceqq_u16(vld1q_u16(a._u16), vld1q_u16(b._u16)))); }
static inline u128 spu_ceqb(u128 a, u128 b)  { return spu__from_u8(vceqq_u8(vld1q_u8(a._u8), vld1q_u8(b._u8))); }
static inline u128 spu_cgt(u128 a, u128 b)   { return spu__from_u32(vcgtq_s32(vld1q_s32(a._s32), vld1q_s32(b._s32))); }
static inline u128 spu_cgth(u128 a, u128 b)  { return spu__from_u8(vreinterpretq_u8_u16(vcgtq_s16(vld1q_s16(a._s16), vld1q_s16(b._s16)))); }
static inline u128 spu_cgtb(u128 a, u128 b)  { return spu__from_u8(vcgtq_s8(vld1q_s8(a._s8), vld1q_s8(b._s8))); }
static inline u128 spu_clgt(u128 a, u128 b)  { return spu__from_u32(vcgtq_u32(vld1q_u32(a._u32), vld1q_u32(b._u32))); }
static inline u128 spu_clgth(u128 a, u128 b) { return spu__from_u8(vreinterpretq_u8_u16(vcgtq_u16(vld1q_u16(a._u16), vld1q_u16(b._u16)))); }
static inline u128 spu_clgtb(u128 a, u128 b) { return spu__from_u8(vcgtq_u8(vld1q_u8(a._u8), vld1q_u8(b._u8))); }
static inline u128 spu_absdb(u128 a, u128 b) { return spu__from_u8(vabdq_u8(vld1q_u8(a._u8), vld1q_u8(b._u8))); }
static inline u128 spu_avgb(u128 a, u128 b)  { return spu__from_u8(vrhaddq_u8(vld1q_u8(a._u8), vld1q_u8(b._u8))); }
/* sumb: parola i = (somma dei 4 byte di b) << 16 | (somma dei 4 byte di a);
 * la somma non dipende dall'ordine dei byte dentro la parola */
static inline u128 spu_sumb(u128 a, u128 b) {
    uint32x4_t sa = vpaddlq_u16(vpaddlq_u8(vld1q_u8(a._u8)));
    uint32x4_t sb = vpaddlq_u16(vpaddlq_u8(vld1q_u8(b._u8)));
    return spu__from_u32(vorrq_u32(sa, vshlq_n_u32(sb, 16))); }

/* ---- scorrimenti e rotazioni per corsia con quantita' in un registro.
 * vshlq scorre a sinistra per quantita' positive e a destra per negative;
 * oltre la larghezza della corsia da' 0 (logico) o il segno (aritmetico),
 * che e' esattamente la semantica SPU di shl/rotm/rotma (quantita' & 0x3F,
 * > 31 -> 0 / segno). ---- */
#define SPU_NEON_VSHIFT 1
static inline u128 spu_shl(u128 a, u128 b) {
    int32x4_t sh = vreinterpretq_s32_u32(vandq_u32(vld1q_u32(b._u32), vdupq_n_u32(0x3F)));
    return spu__from_u32(vshlq_u32(vld1q_u32(a._u32), sh)); }
static inline u128 spu_shlh(u128 a, u128 b) {
    int16x8_t sh = vreinterpretq_s16_u16(vandq_u16(vld1q_u16(b._u16), vdupq_n_u16(0x1F)));
    return spu__from_u8(vreinterpretq_u8_u16(vshlq_u16(vld1q_u16(a._u16), sh))); }
static inline u128 spu_rotm(u128 a, u128 b) {
    int32x4_t sh = vnegq_s32(vreinterpretq_s32_u32(vandq_u32(vnegq_s32(vreinterpretq_s32_u32(vld1q_u32(b._u32))) , vdupq_n_u32(0x3F))));
    return spu__from_u32(vshlq_u32(vld1q_u32(a._u32), sh)); }
static inline u128 spu_rotma(u128 a, u128 b) {
    int32x4_t sh = vnegq_s32(vreinterpretq_s32_u32(vandq_u32(vnegq_s32(vreinterpretq_s32_u32(vld1q_u32(b._u32))) , vdupq_n_u32(0x3F))));
    return spu__from_u32(vreinterpretq_u32_s32(vshlq_s32(vld1q_s32(a._s32), sh))); }
static inline u128 spu_rothm(u128 a, u128 b) {
    int16x8_t sh = vnegq_s16(vreinterpretq_s16_u16(vandq_u16(vnegq_s16(vreinterpretq_s16_u16(vld1q_u16(b._u16))), vdupq_n_u16(0x1F))));
    return spu__from_u8(vreinterpretq_u8_u16(vshlq_u16(vld1q_u16(a._u16), sh))); }
static inline u128 spu_rothma(u128 a, u128 b) {
    int16x8_t sh = vnegq_s16(vreinterpretq_s16_u16(vandq_u16(vnegq_s16(vreinterpretq_s16_u16(vld1q_u16(b._u16))), vdupq_n_u16(0x1F))));
    return spu__from_u8(vreinterpretq_u8_s16(vshlq_s16(vld1q_s16(a._s16), sh))); }
/* rot/roth: rotazione a sinistra di (b & 31) / (b & 15): (a << s) | (a >> (32-s)),
 * con s = 0 il secondo termine e' uno scorrimento di 32 = 0 */
static inline u128 spu_rot(u128 a, u128 b) {
    int32x4_t s = vreinterpretq_s32_u32(vandq_u32(vld1q_u32(b._u32), vdupq_n_u32(31)));
    uint32x4_t x = vld1q_u32(a._u32);
    return spu__from_u32(vorrq_u32(vshlq_u32(x, s), vshlq_u32(x, vsubq_s32(s, vdupq_n_s32(32))))); }
static inline u128 spu_roth(u128 a, u128 b) {
    int16x8_t s = vreinterpretq_s16_u16(vandq_u16(vld1q_u16(b._u16), vdupq_n_u16(15)));
    uint16x8_t x = vld1q_u16(a._u16);
    return spu__from_u8(vreinterpretq_u8_u16(vorrq_u16(vshlq_u16(x, s), vshlq_u16(x, vsubq_s16(s, vdupq_n_s16(16)))))); }

/* ---- fsmb/fsmbi: maschera per byte da 16 bit (bit 15-P -> byte SPU P ->
 * byte host SPU_W(P)): la parola replicata per byte, AND con il bit di
 * ciascun byte, confronto con zero ---- */
#define SPU_NEON_FSMB 1
static const uint8_t spu__fsmb_bit_hi[16] = { 0x10,0x20,0x40,0x80, 0x01,0x02,0x04,0x08, 0,0,0,0, 0,0,0,0 };
static const uint8_t spu__fsmb_bit_lo[16] = { 0,0,0,0, 0,0,0,0, 0x10,0x20,0x40,0x80, 0x01,0x02,0x04,0x08 };
static inline u128 spu__fsmb_u32(uint32_t v) {
    uint8x16_t hi = vandq_u8(vdupq_n_u8((uint8_t)(v >> 8)), vld1q_u8(spu__fsmb_bit_hi));
    uint8x16_t lo = vandq_u8(vdupq_n_u8((uint8_t)v), vld1q_u8(spu__fsmb_bit_lo));
    return spu__from_u8(vmvnq_u8(vceqq_u8(vorrq_u8(hi, lo), vdupq_n_u8(0)))); }
static inline u128 spu_fsmb(u128 a)     { return spu__fsmb_u32(a._u32[0]); }
static inline u128 spu_fsmbi(int32_t imm) { return spu__fsmb_u32((uint32_t)imm); }

#endif /* aarch64 && NEON */
#endif /* SPU_HELPERS_NEON_H */

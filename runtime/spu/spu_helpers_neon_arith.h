/* Operazioni SPU per corsia in NEON (sessione 47). Incluso in FONDO a
 * spu_helpers.h: le versioni scalari restano definite e le sostituisce una
 * macro con lo stesso nome, quindi `(spu_a)(x, y)` chiama ancora la scalare
 * (e' cosi' che le confronta tests/spu_neon_arith_diff.c).
 * Perche': clang traduceva i cicli `for(i<4) r._u32[i] = a._u32[i] + ...`
 * del codice SPU liftato in aritmetica scalare a coppie di parole su registri
 * x con maschere 0xffffffff00000000 (~12 istruzioni per un `add.4s`); nel job
 * di geometria (jobmod_008dc900, spu_func_00010300: 16% del tempo di CPU)
 * c'erano 1.198 add e 992 and scalari su 15.690 istruzioni. Qui ogni
 * operazione e' una o due istruzioni NEON. Spento con -DSPU_NEON_ARITH_OFF.
 * Le corsie a 16/8 bit non dipendono dall'ordine dei byte (u128 tiene ogni
 * parola SPU come valore host); i prodotti usano gli stessi indici delle
 * scalari (_s16[2i] = meta' bassa della parola = halfword destra SPU). */
#ifndef SPU_HELPERS_NEON_ARITH_H
#define SPU_HELPERS_NEON_ARITH_H
#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(SPU_NEON_ARITH_OFF)
#include <arm_neon.h>

#define SPU__U32(x) vld1q_u32((x)._u32)
#define SPU__U16(x) vld1q_u16((x)._u16)
#define SPU__U8(x)  vld1q_u8((x)._u8)
#define SPU__S32(x) vld1q_s32((x)._s32)
static inline u128 spu__n16(uint16x8_t v) { u128 r; vst1q_u16(r._u16, v); return r; }
static inline u128 spu__ns32(int32x4_t v) { u128 r; vst1q_s32(r._s32, v); return r; }
static inline int32x4_t spu__lo16s(u128 x) { return vshrq_n_s32(vshlq_n_s32(SPU__S32(x), 16), 16); }
static inline int32x4_t spu__hi16s(u128 x) { return vshrq_n_s32(SPU__S32(x), 16); }

/* ---- somme e differenze ---- */
static inline u128 spu__n_a(u128 a, u128 b)          { return spu__from_u32(vaddq_u32(SPU__U32(a), SPU__U32(b))); }
static inline u128 spu__n_sf(u128 a, u128 b)         { return spu__from_u32(vsubq_u32(SPU__U32(b), SPU__U32(a))); }
static inline u128 spu__n_ah(u128 a, u128 b)         { return spu__n16(vaddq_u16(SPU__U16(a), SPU__U16(b))); }
static inline u128 spu__n_sfh(u128 a, u128 b)        { return spu__n16(vsubq_u16(SPU__U16(b), SPU__U16(a))); }
static inline u128 spu__n_ai(u128 a, int32_t imm)    { return spu__from_u32(vaddq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }
static inline u128 spu__n_ahi(u128 a, int32_t imm)   { return spu__n16(vaddq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_sfi(u128 a, int32_t imm)   { return spu__from_u32(vsubq_u32(vdupq_n_u32((uint32_t)imm), SPU__U32(a))); }
static inline u128 spu__n_sfhi(u128 a, int32_t imm)  { return spu__n16(vsubq_u16(vdupq_n_u16((uint16_t)imm), SPU__U16(a))); }

/* ---- logiche ---- */
static inline u128 spu__n_and(u128 a, u128 b)  { return spu__from_u8(vandq_u8(SPU__U8(a), SPU__U8(b))); }
static inline u128 spu__n_or(u128 a, u128 b)   { return spu__from_u8(vorrq_u8(SPU__U8(a), SPU__U8(b))); }
static inline u128 spu__n_xor(u128 a, u128 b)  { return spu__from_u8(veorq_u8(SPU__U8(a), SPU__U8(b))); }
static inline u128 spu__n_nand(u128 a, u128 b) { return spu__from_u8(vmvnq_u8(vandq_u8(SPU__U8(a), SPU__U8(b)))); }
static inline u128 spu__n_nor(u128 a, u128 b)  { return spu__from_u8(vmvnq_u8(vorrq_u8(SPU__U8(a), SPU__U8(b)))); }
static inline u128 spu__n_andc(u128 a, u128 b) { return spu__from_u8(vbicq_u8(SPU__U8(a), SPU__U8(b))); }
static inline u128 spu__n_orc(u128 a, u128 b)  { return spu__from_u8(vornq_u8(SPU__U8(a), SPU__U8(b))); }
static inline u128 spu__n_eqv(u128 a, u128 b)  { return spu__from_u8(vmvnq_u8(veorq_u8(SPU__U8(a), SPU__U8(b)))); }
static inline u128 spu__n_selb(u128 a, u128 b, u128 c) { return spu__from_u8(vbslq_u8(SPU__U8(c), SPU__U8(b), SPU__U8(a))); }
static inline u128 spu__n_andi(u128 a, int32_t imm)  { return spu__from_u32(vandq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }
static inline u128 spu__n_ori(u128 a, int32_t imm)   { return spu__from_u32(vorrq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }
static inline u128 spu__n_xori(u128 a, int32_t imm)  { return spu__from_u32(veorq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }
static inline u128 spu__n_andhi(u128 a, int32_t imm) { return spu__n16(vandq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_orhi(u128 a, int32_t imm)  { return spu__n16(vorrq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_xorhi(u128 a, int32_t imm) { return spu__n16(veorq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_andbi(u128 a, int32_t imm) { return spu__from_u8(vandq_u8(SPU__U8(a), vdupq_n_u8((uint8_t)imm))); }
static inline u128 spu__n_orbi(u128 a, int32_t imm)  { return spu__from_u8(vorrq_u8(SPU__U8(a), vdupq_n_u8((uint8_t)imm))); }
static inline u128 spu__n_xorbi(u128 a, int32_t imm) { return spu__from_u8(veorq_u8(SPU__U8(a), vdupq_n_u8((uint8_t)imm))); }
static inline u128 spu__n_iohl(u128 a, uint16_t imm) { return spu__from_u32(vorrq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }

/* ---- confronti con immediato ---- */
static inline u128 spu__n_ceqi(u128 a, int32_t imm)  { return spu__from_u32(vceqq_s32(SPU__S32(a), vdupq_n_s32(imm))); }
static inline u128 spu__n_cgti(u128 a, int32_t imm)  { return spu__from_u32(vcgtq_s32(SPU__S32(a), vdupq_n_s32(imm))); }
static inline u128 spu__n_clgti(u128 a, int32_t imm) { return spu__from_u32(vcgtq_u32(SPU__U32(a), vdupq_n_u32((uint32_t)imm))); }
static inline u128 spu__n_ceqhi(u128 a, int32_t imm) { return spu__n16(vceqq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_cgthi(u128 a, int32_t imm) { return spu__n16(vcgtq_s16(vld1q_s16(a._s16), vdupq_n_s16((int16_t)imm))); }
static inline u128 spu__n_clgthi(u128 a, int32_t imm){ return spu__n16(vcgtq_u16(SPU__U16(a), vdupq_n_u16((uint16_t)imm))); }
static inline u128 spu__n_ceqbi(u128 a, int32_t imm) { return spu__from_u8(vceqq_u8(SPU__U8(a), vdupq_n_u8((uint8_t)imm))); }
static inline u128 spu__n_cgtbi(u128 a, int32_t imm) { return spu__from_u8(vcgtq_s8(vld1q_s8(a._s8), vdupq_n_s8((int8_t)imm))); }
static inline u128 spu__n_clgtbi(u128 a, int32_t imm){ return spu__from_u8(vcgtq_u8(SPU__U8(a), vdupq_n_u8((uint8_t)imm))); }

/* ---- scorrimenti e rotazioni con immediato: vshlq con quantita' negativa
 * scorre a destra, oltre la corsia da' 0 (logico) o il segno (aritmetico),
 * come le regole SPU (vedi SPU_NEON_VSHIFT in spu_helpers_neon.h) ---- */
static inline u128 spu__n_shli(u128 a, int sh)    { return spu__from_u32(vshlq_u32(SPU__U32(a), vdupq_n_s32(sh & 0x3F))); }
static inline u128 spu__n_shlhi(u128 a, int sh)   { return spu__n16(vshlq_u16(SPU__U16(a), vdupq_n_s16((int16_t)(sh & 0x1F)))); }
static inline u128 spu__n_rotmi(u128 a, int i7)   { return spu__from_u32(vshlq_u32(SPU__U32(a), vdupq_n_s32(-((0 - i7) & 0x3F)))); }
static inline u128 spu__n_rotmai(u128 a, int i7)  { return spu__ns32(vshlq_s32(SPU__S32(a), vdupq_n_s32(-((0 - i7) & 0x3F)))); }
static inline u128 spu__n_rotmhi(u128 a, int i7)  { return spu__n16(vshlq_u16(SPU__U16(a), vdupq_n_s16((int16_t)-((0 - i7) & 0x1F)))); }
static inline u128 spu__n_rotmahi(u128 a, int i7) { return spu__n16(vreinterpretq_u16_s16(vshlq_s16(vld1q_s16(a._s16), vdupq_n_s16((int16_t)-((0 - i7) & 0x1F))))); }
static inline u128 spu__n_roti(u128 a, int sh) {
    int s = sh & 31; uint32x4_t x = SPU__U32(a);
    return spu__from_u32(vorrq_u32(vshlq_u32(x, vdupq_n_s32(s)), vshlq_u32(x, vdupq_n_s32(s - 32)))); }
static inline u128 spu__n_rothi(u128 a, int sh) {
    int s = sh & 15; uint16x8_t x = SPU__U16(a);
    return spu__n16(vorrq_u16(vshlq_u16(x, vdupq_n_s16((int16_t)s)), vshlq_u16(x, vdupq_n_s16((int16_t)(s - 16))))); }
static inline u128 spu__n_rotqbyi(u128 a, int sh) { return spu__tbl(a, spu__rotqby_tbl[sh & 0x0F]); }

/* ---- prodotti a 16 bit ed estensioni di segno ---- */
static inline u128 spu__n_mpy(u128 a, u128 b)   { return spu__ns32(vmulq_s32(spu__lo16s(a), spu__lo16s(b))); }
static inline u128 spu__n_mpya(u128 a, u128 b, u128 c) { return spu__ns32(vmlaq_s32(SPU__S32(c), spu__lo16s(a), spu__lo16s(b))); }
static inline u128 spu__n_mpyu(u128 a, u128 b)  {
    uint32x4_t m = vdupq_n_u32(0xFFFF);
    return spu__from_u32(vmulq_u32(vandq_u32(SPU__U32(a), m), vandq_u32(SPU__U32(b), m))); }
static inline u128 spu__n_mpyi(u128 a, int32_t imm)  { return spu__ns32(vmulq_s32(spu__lo16s(a), vdupq_n_s32((int16_t)imm))); }
static inline u128 spu__n_mpyui(u128 a, int32_t imm) { return spu__from_u32(vmulq_u32(vandq_u32(SPU__U32(a), vdupq_n_u32(0xFFFF)), vdupq_n_u32((uint16_t)imm))); }
static inline u128 spu__n_mpyh(u128 a, u128 b)  { return spu__from_u32(vshlq_n_u32(vreinterpretq_u32_s32(vmulq_s32(spu__hi16s(a), spu__lo16s(b))), 16)); }
static inline u128 spu__n_mpyhh(u128 a, u128 b) { return spu__ns32(vmulq_s32(spu__hi16s(a), spu__hi16s(b))); }
static inline u128 spu__n_xshw(u128 a) { return spu__ns32(spu__lo16s(a)); }
static inline u128 spu__n_xsbh(u128 a) { return spu__n16(vreinterpretq_u16_s16(vshrq_n_s16(vshlq_n_s16(vld1q_s16(a._s16), 8), 8))); }

#define spu_a(a, b)          spu__n_a(a, b)
#define spu_sf(a, b)         spu__n_sf(a, b)
#define spu_ah(a, b)         spu__n_ah(a, b)
#define spu_sfh(a, b)        spu__n_sfh(a, b)
#define spu_ai(a, i)         spu__n_ai(a, i)
#define spu_ahi(a, i)        spu__n_ahi(a, i)
#define spu_sfi(a, i)        spu__n_sfi(a, i)
#define spu_sfhi(a, i)       spu__n_sfhi(a, i)
#define spu_and(a, b)        spu__n_and(a, b)
#define spu_or(a, b)         spu__n_or(a, b)
#define spu_xor(a, b)        spu__n_xor(a, b)
#define spu_nand(a, b)       spu__n_nand(a, b)
#define spu_nor(a, b)        spu__n_nor(a, b)
#define spu_andc(a, b)       spu__n_andc(a, b)
#define spu_orc(a, b)        spu__n_orc(a, b)
#define spu_eqv(a, b)        spu__n_eqv(a, b)
#define spu_selb(a, b, c)    spu__n_selb(a, b, c)
#define spu_andi(a, i)       spu__n_andi(a, i)
#define spu_ori(a, i)        spu__n_ori(a, i)
#define spu_xori(a, i)       spu__n_xori(a, i)
#define spu_andhi(a, i)      spu__n_andhi(a, i)
#define spu_orhi(a, i)       spu__n_orhi(a, i)
#define spu_xorhi(a, i)      spu__n_xorhi(a, i)
#define spu_andbi(a, i)      spu__n_andbi(a, i)
#define spu_orbi(a, i)       spu__n_orbi(a, i)
#define spu_xorbi(a, i)      spu__n_xorbi(a, i)
#define spu_iohl(a, i)       spu__n_iohl(a, i)
#define spu_ceqi(a, i)       spu__n_ceqi(a, i)
#define spu_cgti(a, i)       spu__n_cgti(a, i)
#define spu_clgti(a, i)      spu__n_clgti(a, i)
#define spu_ceqhi(a, i)      spu__n_ceqhi(a, i)
#define spu_cgthi(a, i)      spu__n_cgthi(a, i)
#define spu_clgthi(a, i)     spu__n_clgthi(a, i)
#define spu_ceqbi(a, i)      spu__n_ceqbi(a, i)
#define spu_cgtbi(a, i)      spu__n_cgtbi(a, i)
#define spu_clgtbi(a, i)     spu__n_clgtbi(a, i)
#define spu_shli(a, s)       spu__n_shli(a, s)
#define spu_shlhi(a, s)      spu__n_shlhi(a, s)
#define spu_rotmi(a, s)      spu__n_rotmi(a, s)
#define spu_rotmai(a, s)     spu__n_rotmai(a, s)
#define spu_rotmhi(a, s)     spu__n_rotmhi(a, s)
#define spu_rothmi(a, s)     spu__n_rotmhi(a, s)
#define spu_rotmahi(a, s)    spu__n_rotmahi(a, s)
#define spu_roti(a, s)       spu__n_roti(a, s)
#define spu_rothi(a, s)      spu__n_rothi(a, s)
#define spu_rotqbyi(a, s)    spu__n_rotqbyi(a, s)
#define spu_mpy(a, b)        spu__n_mpy(a, b)
#define spu_mpya(a, b, c)    spu__n_mpya(a, b, c)
#define spu_mpyu(a, b)       spu__n_mpyu(a, b)
#define spu_mpyi(a, i)       spu__n_mpyi(a, i)
#define spu_mpyui(a, i)      spu__n_mpyui(a, i)
#define spu_mpyh(a, b)       spu__n_mpyh(a, b)
#define spu_mpyhh(a, b)      spu__n_mpyhh(a, b)
#define spu_xshw(a)          spu__n_xshw(a)
#define spu_xsbh(a)          spu__n_xsbh(a)

#endif /* aarch64 && NEON && !SPU_NEON_ARITH_OFF */
#endif /* SPU_HELPERS_NEON_ARITH_H */

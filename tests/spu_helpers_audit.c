/* Audit degli helper SPU (s.32): ogni helper di runtime/spu/spu_helpers.h
 * usato dai job di Uncharted contro un'implementazione INDIPENDENTE scritta
 * dalla specifica CBEA (SPU ISA) su un registro in ordine di byte SPU
 * (big-endian, byte 0 = piu' significativo del preferred slot). Ingressi
 * casuali (+ valori strutturati); ogni differenza viene stampata con gli
 * operandi. Compilare e lanciare:
 *
 *   clang -O1 -I runtime/spu -I runtime -o /tmp/spu_audit tests/spu_helpers_audit.c && /tmp/spu_audit
 *
 * Non fa parte della build: e' uno strumento di indagine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "spu_helpers.h"

/* ---- rappresentazione di riferimento: 16 byte in ordine SPU ---- */
typedef struct { uint8_t b[16]; } be128;

static be128 to_be(u128 a) { be128 r; for (int w = 0; w < 4; w++) { uint32_t v = a._u32[w]; r.b[4*w] = v >> 24; r.b[4*w+1] = v >> 16; r.b[4*w+2] = v >> 8; r.b[4*w+3] = v; } return r; }
static u128 from_be(be128 r) { u128 a; for (int w = 0; w < 4; w++) a._u32[w] = ((uint32_t)r.b[4*w] << 24) | ((uint32_t)r.b[4*w+1] << 16) | ((uint32_t)r.b[4*w+2] << 8) | r.b[4*w+3]; return a; }
static uint32_t W(const be128* r, int i) { return ((uint32_t)r->b[4*i] << 24) | ((uint32_t)r->b[4*i+1] << 16) | ((uint32_t)r->b[4*i+2] << 8) | r->b[4*i+3]; }
static void SW(be128* r, int i, uint32_t v) { r->b[4*i] = v >> 24; r->b[4*i+1] = v >> 16; r->b[4*i+2] = v >> 8; r->b[4*i+3] = v; }
static uint16_t H(const be128* r, int i) { return (uint16_t)(((uint16_t)r->b[2*i] << 8) | r->b[2*i+1]); }
static void SH(be128* r, int i, uint16_t v) { r->b[2*i] = v >> 8; r->b[2*i+1] = v; }
static float FW(const be128* r, int i) { uint32_t v = W(r, i); float f; memcpy(&f, &v, 4); return f; }
static void SFW(be128* r, int i, float f) { uint32_t v; memcpy(&v, &f, 4); SW(r, i, v); }

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (uint32_t)(rng_state >> 11); }
static u128 rnd128(void) { u128 a; for (int i = 0; i < 4; i++) a._u32[i] = rnd(); switch (rnd() & 7) {
    case 0: a._u32[rnd() & 3] = 0; break; case 1: a._u32[rnd() & 3] = 0xFFFFFFFFu; break;
    case 2: for (int i = 0; i < 4; i++) { float f = ((float)(int32_t)rnd() / 65536.0f); memcpy(&a._u32[i], &f, 4); } break;
    case 3: a._u32[0] = rnd() & 0x7F; break; default: break; } return a; }

/* ---- riferimento (CBEA) ---- */
static be128 R_shufb(be128 a, be128 b, be128 c) { be128 r; uint8_t cat[32]; memcpy(cat, a.b, 16); memcpy(cat + 16, b.b, 16);
    for (int i = 0; i < 16; i++) { uint8_t s = c.b[i]; if ((s & 0xE0) == 0xE0) r.b[i] = 0x80; else if ((s & 0xC0) == 0xC0) r.b[i] = 0xFF; else if ((s & 0x80)) r.b[i] = 0x00; else r.b[i] = cat[s & 0x1F]; } return r; }
static be128 R_rotqby(be128 a, int n) { be128 r; n &= 15; for (int i = 0; i < 16; i++) r.b[i] = a.b[(i + n) & 15]; return r; }
static be128 R_shlqby(be128 a, int n) { be128 r; n &= 31; for (int i = 0; i < 16; i++) r.b[i] = (i + n < 16) ? a.b[i + n] : 0; return r; }
static be128 R_rotqmby(be128 a, int n) { be128 r; n = (-n) & 31; for (int i = 0; i < 16; i++) r.b[i] = (i - n >= 0) ? a.b[i - n] : 0; return r; }
static be128 R_rotqbi(be128 a, int n) { be128 r; n &= 7; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)((a.b[i] << n) | (n ? (a.b[(i + 1) & 15] >> (8 - n)) : 0)); return r; }
static be128 R_shlqbi(be128 a, int n) { be128 r; n &= 7; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)((a.b[i] << n) | ((n && i < 15) ? (a.b[i + 1] >> (8 - n)) : 0)); return r; }
static be128 R_rotqmbi(be128 a, int n) { be128 r; n = (-n) & 7; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)((a.b[i] >> n) | ((n && i > 0) ? (a.b[i - 1] << (8 - n)) : 0)); return r; }
static be128 R_shli(be128 a, int n) { be128 r; n &= 63; for (int i = 0; i < 4; i++) SW(&r, i, n > 31 ? 0 : W(&a, i) << n); return r; }
static be128 R_roti(be128 a, int n) { be128 r; n &= 31; for (int i = 0; i < 4; i++) { uint32_t v = W(&a, i); SW(&r, i, n ? (v << n) | (v >> (32 - n)) : v); } return r; }
static be128 R_rotmi(be128 a, int n) { be128 r; n = (-n) & 63; for (int i = 0; i < 4; i++) SW(&r, i, n > 31 ? 0 : W(&a, i) >> n); return r; }
static be128 R_rotmai(be128 a, int n) { be128 r; n = (-n) & 63; for (int i = 0; i < 4; i++) { int32_t v = (int32_t)W(&a, i); SW(&r, i, (uint32_t)(n > 31 ? (v >> 31) : (v >> n))); } return r; }
static be128 R_shlhi(be128 a, int n) { be128 r; n &= 31; for (int i = 0; i < 8; i++) SH(&r, i, n > 15 ? 0 : (uint16_t)(H(&a, i) << n)); return r; }
static be128 R_rothi(be128 a, int n) { be128 r; n &= 15; for (int i = 0; i < 8; i++) { uint16_t v = H(&a, i); SH(&r, i, n ? (uint16_t)((v << n) | (v >> (16 - n))) : v); } return r; }
static be128 R_rothmi(be128 a, int n) { be128 r; n = (-n) & 31; for (int i = 0; i < 8; i++) SH(&r, i, n > 15 ? 0 : (uint16_t)(H(&a, i) >> n)); return r; }
static be128 R_rotmahi(be128 a, int n) { be128 r; n = (-n) & 31; for (int i = 0; i < 8; i++) { int16_t v = (int16_t)H(&a, i); SH(&r, i, (uint16_t)(n > 15 ? (v >> 15) : (v >> n))); } return r; }
static be128 R_a(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) + W(&b, i)); return r; }
static be128 R_sf(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&b, i) - W(&a, i)); return r; }
static be128 R_ah(be128 a, be128 b) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)(H(&a, i) + H(&b, i))); return r; }
static be128 R_sfh(be128 a, be128 b) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)(H(&b, i) - H(&a, i))); return r; }
static be128 R_ai(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) + (uint32_t)imm); return r; }
static be128 R_sfi(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)imm - W(&a, i)); return r; }
static be128 R_ahi(be128 a, int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)(H(&a, i) + (uint16_t)imm)); return r; }
static be128 R_sfhi(be128 a, int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)((uint16_t)imm - H(&a, i))); return r; }
static be128 R_mpy(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)((int32_t)(int16_t)H(&a, 2*i+1) * (int32_t)(int16_t)H(&b, 2*i+1))); return r; }
static be128 R_mpyu(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)H(&a, 2*i+1) * (uint32_t)H(&b, 2*i+1)); return r; }
static be128 R_mpyh(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)((int32_t)(int16_t)H(&a, 2*i) * (int32_t)(int16_t)H(&b, 2*i+1)) << 16); return r; }
static be128 R_mpyhh(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)((int32_t)(int16_t)H(&a, 2*i) * (int32_t)(int16_t)H(&b, 2*i))); return r; }
static be128 R_mpys(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)(((int32_t)(int16_t)H(&a, 2*i+1) * (int32_t)(int16_t)H(&b, 2*i+1)) >> 16)); return r; }
static be128 R_mpya(be128 a, be128 b, be128 c) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)((int32_t)(int16_t)H(&a, 2*i+1) * (int32_t)(int16_t)H(&b, 2*i+1)) + W(&c, i)); return r; }
static be128 R_mpyi(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)((int32_t)(int16_t)H(&a, 2*i+1) * (int32_t)(int16_t)imm)); return r; }
static be128 R_mpyui(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)H(&a, 2*i+1) * (uint32_t)(uint16_t)imm); return r; }
static be128 R_clz(be128 a) { be128 r; for (int i = 0; i < 4; i++) { uint32_t v = W(&a, i); int n = 0; if (!v) n = 32; else while (!(v & 0x80000000u)) { v <<= 1; n++; } SW(&r, i, (uint32_t)n); } return r; }
static be128 R_cntb(be128 a) { be128 r; for (int i = 0; i < 16; i++) { uint8_t v = a.b[i], c = 0; while (v) { c += v & 1; v >>= 1; } r.b[i] = c; } return r; }
static be128 R_xsbh(be128 a) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)(int16_t)(int8_t)a.b[2*i+1]); return r; }
static be128 R_xshw(be128 a) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)(int32_t)(int16_t)H(&a, 2*i+1)); return r; }
static be128 R_gb(be128 a) { be128 r = {{0}}; uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (W(&a, i) & 1u) << (3 - i); SW(&r, 0, v); return r; }
static be128 R_gbh(be128 a) { be128 r = {{0}}; uint32_t v = 0; for (int i = 0; i < 8; i++) v |= (H(&a, i) & 1u) << (7 - i); SW(&r, 0, v); return r; }
static be128 R_gbb(be128 a) { be128 r = {{0}}; uint32_t v = 0; for (int i = 0; i < 16; i++) v |= (a.b[i] & 1u) << (15 - i); SW(&r, 0, v); return r; }
static be128 R_fsm(be128 a) { be128 r; uint32_t v = W(&a, 0); for (int i = 0; i < 4; i++) SW(&r, i, ((v >> (3 - i)) & 1) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_fsmh(be128 a) { be128 r; uint32_t v = W(&a, 0); for (int i = 0; i < 8; i++) SH(&r, i, ((v >> (7 - i)) & 1) ? 0xFFFF : 0); return r; }
static be128 R_fsmb(be128 a) { be128 r; uint32_t v = W(&a, 0); for (int i = 0; i < 16; i++) r.b[i] = ((v >> (15 - i)) & 1) ? 0xFF : 0; return r; }
static be128 R_fsmbi(int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (((uint32_t)imm >> (15 - i)) & 1) ? 0xFF : 0; return r; }
static be128 R_selb(be128 a, be128 b, be128 c) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)((c.b[i] & b.b[i]) | (~c.b[i] & a.b[i])); return r; }
static be128 R_and(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] & b.b[i]; return r; }
static be128 R_andc(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] & ~b.b[i]; return r; }
static be128 R_or(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] | b.b[i]; return r; }
static be128 R_xor(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] ^ b.b[i]; return r; }
static be128 R_nand(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)~(a.b[i] & b.b[i]); return r; }
static be128 R_nor(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)~(a.b[i] | b.b[i]); return r; }
static be128 R_andi(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) & (uint32_t)imm); return r; }
static be128 R_ori(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) | (uint32_t)imm); return r; }
static be128 R_andhi(be128 a, int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)(H(&a, i) & (uint16_t)imm)); return r; }
static be128 R_andbi(be128 a, int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(a.b[i] & (uint8_t)imm); return r; }
static be128 R_orbi(be128 a, int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(a.b[i] | (uint8_t)imm); return r; }
static be128 R_ceq(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) == W(&b, i) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_ceqi(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) == (uint32_t)imm ? 0xFFFFFFFFu : 0); return r; }
static be128 R_ceqh(be128 a, be128 b) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, H(&a, i) == H(&b, i) ? 0xFFFF : 0); return r; }
static be128 R_ceqhi(be128 a, int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, H(&a, i) == (uint16_t)imm ? 0xFFFF : 0); return r; }
static be128 R_ceqb(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] == b.b[i] ? 0xFF : 0; return r; }
static be128 R_ceqbi(be128 a, int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] == (uint8_t)imm ? 0xFF : 0; return r; }
static be128 R_cgt(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (int32_t)W(&a, i) > (int32_t)W(&b, i) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_cgti(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (int32_t)W(&a, i) > imm ? 0xFFFFFFFFu : 0); return r; }
static be128 R_cgth(be128 a, be128 b) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (int16_t)H(&a, i) > (int16_t)H(&b, i) ? 0xFFFF : 0); return r; }
static be128 R_cgthi(be128 a, int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (int16_t)H(&a, i) > (int16_t)imm ? 0xFFFF : 0); return r; }
static be128 R_cgtb(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (int8_t)a.b[i] > (int8_t)b.b[i] ? 0xFF : 0; return r; }
static be128 R_cgtbi(be128 a, int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (int8_t)a.b[i] > (int8_t)imm ? 0xFF : 0; return r; }
static be128 R_clgt(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) > W(&b, i) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_clgti(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) > (uint32_t)imm ? 0xFFFFFFFFu : 0); return r; }
static be128 R_clgth(be128 a, be128 b) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, H(&a, i) > H(&b, i) ? 0xFFFF : 0); return r; }
static be128 R_clgtbi(be128 a, int imm) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = a.b[i] > (uint8_t)imm ? 0xFF : 0; return r; }
static be128 R_avgb(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(((uint32_t)a.b[i] + b.b[i] + 1) >> 1); return r; }
static be128 R_absdb(be128 a, be128 b) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(a.b[i] > b.b[i] ? a.b[i] - b.b[i] : b.b[i] - a.b[i]); return r; }
static be128 R_sumb(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) { uint32_t sa = 0, sb = 0; for (int k = 0; k < 4; k++) { sa += a.b[4*i+k]; sb += b.b[4*i+k]; } SH(&r, 2*i, (uint16_t)sb); SH(&r, 2*i+1, (uint16_t)sa); } return r; }
static be128 R_orx(be128 a) { be128 r = {{0}}; SW(&r, 0, W(&a, 0) | W(&a, 1) | W(&a, 2) | W(&a, 3)); return r; }
static be128 R_cbd(uint32_t pos) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(0x10 + i); r.b[pos & 15] = 0x03; return r; }
static be128 R_chd(uint32_t pos) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(0x10 + i); int p = pos & 14; r.b[p] = 0x02; r.b[p+1] = 0x03; return r; }
static be128 R_cwd(uint32_t pos) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(0x10 + i); int p = pos & 12; for (int k = 0; k < 4; k++) r.b[p+k] = (uint8_t)k; return r; }
static be128 R_cdd(uint32_t pos) { be128 r; for (int i = 0; i < 16; i++) r.b[i] = (uint8_t)(0x10 + i); int p = pos & 8; for (int k = 0; k < 8; k++) r.b[p+k] = (uint8_t)k; return r; }
static be128 R_csflt(be128 a, int i8) { be128 r; float f = exp2f((float)(i8 - 155)); for (int i = 0; i < 4; i++) SFW(&r, i, (float)(int32_t)W(&a, i) * f); return r; }
static be128 R_cuflt(be128 a, int i8) { be128 r; float f = exp2f((float)(i8 - 155)); for (int i = 0; i < 4; i++) SFW(&r, i, (float)W(&a, i) * f); return r; }
static be128 R_cflts(be128 a, int i8) { be128 r; float f = exp2f((float)(173 - i8)); for (int i = 0; i < 4; i++) { double v = (double)FW(&a, i) * f; int32_t o; if (v != v) o = 0; else if (v >= 2147483647.0) o = 0x7FFFFFFF; else if (v <= -2147483648.0) o = (int32_t)0x80000000; else o = (int32_t)v; SW(&r, i, (uint32_t)o); } return r; }
static be128 R_cfltu(be128 a, int i8) { be128 r; float f = exp2f((float)(173 - i8)); for (int i = 0; i < 4; i++) { double v = (double)FW(&a, i) * f; uint32_t o; if (v != v || v <= 0) o = 0; else if (v >= 4294967295.0) o = 0xFFFFFFFFu; else o = (uint32_t)v; SW(&r, i, o); } return r; }
static be128 R_fcgt(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, FW(&a, i) > FW(&b, i) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_fcmgt(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, fabsf(FW(&a, i)) > fabsf(FW(&b, i)) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_fceq(be128 a, be128 b) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, FW(&a, i) == FW(&b, i) ? 0xFFFFFFFFu : 0); return r; }
static be128 R_il(int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)imm); return r; }
static be128 R_ilh(int imm) { be128 r; for (int i = 0; i < 8; i++) SH(&r, i, (uint16_t)imm); return r; }
static be128 R_ilhu(int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, (uint32_t)imm << 16); return r; }
static be128 R_iohl(be128 a, int imm) { be128 r; for (int i = 0; i < 4; i++) SW(&r, i, W(&a, i) | ((uint32_t)imm & 0xFFFF)); return r; }

/* ---- confronto ---- */
static int g_fail = 0, g_tests = 0;
static void dump(const char* tag, u128 v) { fprintf(stderr, "    %-6s %08X %08X %08X %08X\n", tag, v._u32[0], v._u32[1], v._u32[2], v._u32[3]); }
static void check(const char* name, u128 ours, be128 ref, u128 a, u128 b, u128 c, int imm, int nops)
{
    g_tests++;
    u128 r = from_be(ref);
    if (memcmp(&r, &ours, 16) != 0) {
        static int shown = 0; g_fail++;
        if (shown++ < 60) { fprintf(stderr, "DIFF %s imm=%d\n", name, imm); dump("a", a); if (nops > 1) dump("b", b); if (nops > 2) dump("c", c); dump("ours", ours); dump("ref", r); }
    }
}
#define T1(NAME) do { u128 a = rnd128(); check(#NAME, spu_##NAME(a), R_##NAME(to_be(a)), a, a, a, 0, 1); } while (0)
#define T2(NAME) do { u128 a = rnd128(), b = rnd128(); check(#NAME, spu_##NAME(a, b), R_##NAME(to_be(a), to_be(b)), a, b, b, 0, 2); } while (0)
#define T3(NAME) do { u128 a = rnd128(), b = rnd128(), c = rnd128(); check(#NAME, spu_##NAME(a, b, c), R_##NAME(to_be(a), to_be(b), to_be(c)), a, b, c, 0, 3); } while (0)
#define TI(NAME, IMM) do { u128 a = rnd128(); int imm = (IMM); check(#NAME, spu_##NAME(a, imm), R_##NAME(to_be(a), imm), a, a, a, imm, 1); } while (0)
/* forme "by register": il conteggio sta nel preferred slot di b */
#define TB(NAME, REF, MASK) do { u128 a = rnd128(), b = rnd128(); check(#NAME, spu_##NAME(a, b), REF(to_be(a), (int)(b._u32[0] & (MASK))), a, b, b, (int)(b._u32[0] & (MASK)), 2); } while (0)
#define TBS(NAME, REF) do { u128 a = rnd128(), b = rnd128(); check(#NAME, spu_##NAME(a, b), REF(to_be(a), (int)b._u32[0]), a, b, b, (int)b._u32[0], 2); } while (0)

int main(void)
{
    for (int it = 0; it < 20000; it++) {
        int i7 = (int)(rnd() % 128) - 64, i10 = (int)(rnd() % 1024) - 512, i16 = (int)(rnd() & 0xFFFF), i8 = (int)(rnd() & 0xFF);
        T3(shufb); T3(selb);
        { u128 a = rnd128(); check("rotqbyi", spu_rotqbyi(a, i7), R_rotqby(to_be(a), i7), a, a, a, i7, 1); check("shlqbyi", spu_shlqbyi(a, i7), R_shlqby(to_be(a), i7), a, a, a, i7, 1);
          check("rotqmbyi", spu_rotqmbyi(a, i7), R_rotqmby(to_be(a), i7), a, a, a, i7, 1); check("rotqbii", spu_rotqbii(a, i7), R_rotqbi(to_be(a), i7), a, a, a, i7, 1);
          check("shlqbii", spu_shlqbii(a, i7), R_shlqbi(to_be(a), i7), a, a, a, i7, 1); check("rotqmbii", spu_rotqmbii(a, i7), R_rotqmbi(to_be(a), i7), a, a, a, i7, 1); }
        TBS(rotqby, R_rotqby); TBS(shlqby, R_shlqby); TBS(rotqmby, R_rotqmby); TBS(rotqbi, R_rotqbi); TBS(shlqbi, R_shlqbi); TBS(rotqmbi, R_rotqmbi);
        TI(shli, i7); TI(roti, i7); TI(rotmi, i7); TI(rotmai, i7); TI(shlhi, i7); TI(rothi, i7); TI(rothmi, i7); TI(rotmahi, i7);
        T2(a); T2(sf); T2(ah); T2(sfh); TI(ai, i10); TI(sfi, i10); TI(ahi, i10); TI(sfhi, i10);
        T2(mpy); T2(mpyu); T2(mpyh); T2(mpyhh); T2(mpys); T3(mpya); TI(mpyi, i10); TI(mpyui, i10);
        T1(clz); T1(cntb); T1(xsbh); T1(xshw); T1(gb); T1(gbh); T1(gbb); T1(fsm); T1(fsmh); T1(fsmb); T1(orx);
        { int imm = i16; check("fsmbi", spu_fsmbi(imm), R_fsmbi(imm), spu_zero(), spu_zero(), spu_zero(), imm, 0); }
        T2(and); T2(andc); T2(or); T2(xor); T2(nand); T2(nor); TI(andi, i10); TI(ori, i10); TI(andhi, i10); TI(andbi, i10); TI(orbi, i10);
        T2(ceq); TI(ceqi, i10); T2(ceqh); TI(ceqhi, i10); T2(ceqb); TI(ceqbi, i10); T2(cgt); TI(cgti, i10); T2(cgth); TI(cgthi, i10); T2(cgtb); TI(cgtbi, i10);
        T2(clgt); TI(clgti, i10); T2(clgth); TI(clgtbi, i10); T2(avgb); T2(absdb); T2(sumb);
        { u128 a = rnd128(); int imm = i7; uint32_t pos = a._u32[0] + (uint32_t)imm;
          check("cbd", spu_cbd(a, imm), R_cbd(pos), a, a, a, imm, 1); check("chd", spu_chd(a, imm), R_chd(pos), a, a, a, imm, 1);
          check("cwd", spu_cwd(a, imm), R_cwd(pos), a, a, a, imm, 1); check("cdd", spu_cdd(a, imm), R_cdd(pos), a, a, a, imm, 1); }
        TI(csflt, i8); TI(cuflt, i8); TI(cflts, i8); TI(cfltu, i8);
        T2(fcgt); T2(fcmgt); T2(fceq);
        { int imm = i16; check("il", spu_il((int16_t)imm), R_il((int16_t)imm), spu_zero(), spu_zero(), spu_zero(), imm, 0);
          check("ilh", spu_ilh(imm), R_ilh(imm), spu_zero(), spu_zero(), spu_zero(), imm, 0);
          check("ilhu", spu_ilhu(imm), R_ilhu(imm), spu_zero(), spu_zero(), spu_zero(), imm, 0);
          u128 a = rnd128(); check("iohl", spu_iohl(a, imm), R_iohl(to_be(a), imm), a, a, a, imm, 1); }
    }
    fprintf(stderr, "%d confronti, %d differenze\n", g_tests, g_fail);
    return g_fail ? 1 : 0;
}

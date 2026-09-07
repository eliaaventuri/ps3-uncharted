/*
 * ps3recomp - PPU (PowerPC Processing Unit) execution context
 *
 * Models the full architectural state of a Cell PPU thread: 32 GPRs, 32 FPRs,
 * 32 VRs, condition register, link register, count register, XER, FPSCR, VSCR,
 * and the current instruction address (CIA).
 */

#ifndef PPU_CONTEXT_H
#define PPU_CONTEXT_H

#include "../../include/ps3emu/ps3types.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread-local storage qualifier. Defined identically in the lifter's
 * HEADER_PREAMBLE (tools/ppu_lifter.py), which this header is already kept
 * in sync with -- code reaches one or the other depending on whether it
 * built against a generated ppu_recomp.h. The #ifndef makes including both
 * harmless. */
#ifndef PPU_THREAD_LOCAL
#  ifdef _MSC_VER
#    define PPU_THREAD_LOCAL __declspec(thread)
#  else
#    define PPU_THREAD_LOCAL __thread
#  endif
#endif

/* ---------------------------------------------------------------------------
 * Condition Register field -- 4 bits: LT, GT, EQ, SO
 * -----------------------------------------------------------------------*/
#define PPU_CR_LT  0x8
#define PPU_CR_GT  0x4
#define PPU_CR_EQ  0x2
#define PPU_CR_SO  0x1

/* XER bit positions */
#define PPU_XER_SO  (1u << 31)
#define PPU_XER_OV  (1u << 30)
#define PPU_XER_CA  (1u << 29)

/* VSCR bits */
#define PPU_VSCR_NJ  (1u << 16)  /* Non-Java mode */
#define PPU_VSCR_SAT (1u << 0)   /* Saturation */

/* ---------------------------------------------------------------------------
 * PPU thread context
 * -----------------------------------------------------------------------*/
typedef struct ppu_context {
    /* General-purpose registers (r0-r31) */
    uint64_t gpr[32];

    /* Floating-point registers (f0-f31) */
    double   fpr[32];

    /* Vector registers (vr0-vr31) for VMX/AltiVec
     * Each is 128 bits, aligned to 16 bytes. */
#ifdef _MSC_VER
    __declspec(align(16)) u128 vr[32];
#else
    u128     vr[32] __attribute__((aligned(16)));
#endif

    /* Condition register -- 8 x 4-bit fields packed into 32 bits.
     * CR0 is bits 31:28, CR1 is 27:24, ..., CR7 is 3:0. */
    uint32_t cr;

    /* Link register -- return address */
    uint64_t lr;

    /* Count register -- used by bdnz-style branches */
    uint64_t ctr;

    /* Fixed-point exception register */
    uint32_t xer;

    /* Floating-point status and control register */
    uint32_t fpscr;

    /* Vector status and control register */
    uint32_t vscr;

    /* Current instruction address (program counter) */
    uint64_t cia;

    /* Thread identification */
    uint64_t thread_id;

    /* Reserve address for lwarx/stwcx (load-linked / store-conditional) */
    uint64_t reserve_addr;
    uint64_t reserve_value;
    int      reserve_valid;

} ppu_context;

/* ---------------------------------------------------------------------------
 * Initialization
 * -----------------------------------------------------------------------*/
static inline void ppu_context_init(ppu_context* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    /* r1 (stack pointer) gets set during thread creation.
     * r2 (TOC) gets set from the ELF entry descriptor.
     * r13 (SDA base) gets set from the module loader. */
}

/* ---------------------------------------------------------------------------
 * Condition Register helpers
 * -----------------------------------------------------------------------*/

/* Get a 4-bit CR field (0-7). */
static inline uint8_t ppu_cr_get(const ppu_context* ctx, int field)
{
    return (uint8_t)((ctx->cr >> (28 - field * 4)) & 0xF);
}

/* Set a 4-bit CR field (0-7). */
static inline void ppu_cr_set(ppu_context* ctx, int field, uint8_t value)
{
    uint32_t shift = (uint32_t)(28 - field * 4);
    ctx->cr = (ctx->cr & ~(0xFu << shift)) | ((uint32_t)(value & 0xF) << shift);
}

/* Set a CR field from a signed 64-bit comparison result. */
static inline void ppu_cr_set_s64(ppu_context* ctx, int field, int64_t a, int64_t b)
{
    uint8_t val;
    if (a < b)      val = PPU_CR_LT;
    else if (a > b) val = PPU_CR_GT;
    else            val = PPU_CR_EQ;
    if (ctx->xer & PPU_XER_SO) val |= PPU_CR_SO;
    ppu_cr_set(ctx, field, val);
}

/* Set a CR field from an unsigned 64-bit comparison result. */
static inline void ppu_cr_set_u64(ppu_context* ctx, int field, uint64_t a, uint64_t b)
{
    uint8_t val;
    if (a < b)      val = PPU_CR_LT;
    else if (a > b) val = PPU_CR_GT;
    else            val = PPU_CR_EQ;
    if (ctx->xer & PPU_XER_SO) val |= PPU_CR_SO;
    ppu_cr_set(ctx, field, val);
}

/* Set a CR field from a signed 32-bit comparison. */
static inline void ppu_cr_set_s32(ppu_context* ctx, int field, int32_t a, int32_t b)
{
    ppu_cr_set_s64(ctx, field, (int64_t)a, (int64_t)b);
}

static inline void ppu_cr_set_u32(ppu_context* ctx, int field, uint32_t a, uint32_t b)
{
    ppu_cr_set_u64(ctx, field, (uint64_t)a, (uint64_t)b);
}

/* ---------------------------------------------------------------------------
 * XER helpers
 * -----------------------------------------------------------------------*/
static inline int ppu_xer_get_so(const ppu_context* ctx) { return (ctx->xer & PPU_XER_SO) != 0; }
static inline int ppu_xer_get_ov(const ppu_context* ctx) { return (ctx->xer & PPU_XER_OV) != 0; }
static inline int ppu_xer_get_ca(const ppu_context* ctx) { return (ctx->xer & PPU_XER_CA) != 0; }

static inline void ppu_xer_set_ca(ppu_context* ctx, int ca)
{
    if (ca) ctx->xer |= PPU_XER_CA;
    else    ctx->xer &= ~PPU_XER_CA;
}

static inline void ppu_xer_set_ov(ppu_context* ctx, int ov)
{
    if (ov) { ctx->xer |= PPU_XER_OV | PPU_XER_SO; }
    else    { ctx->xer &= ~PPU_XER_OV; }
}

/* XER[24:31] byte count field for lswx/stswx */
static inline uint8_t ppu_xer_get_byte_count(const ppu_context* ctx)
{
    return (uint8_t)(ctx->xer & 0x7F);
}

/* ---------------------------------------------------------------------------
 * Stack pointer management
 *
 * PS3/PPC64 ABI: r1 = stack pointer, stack grows downward,
 * 16-byte aligned, with a back-chain word at r1+0.
 * -----------------------------------------------------------------------*/
static inline void ppu_set_stack(ppu_context* ctx, uint64_t stack_top, uint64_t stack_size)
{
    /* Place SP at the top of the allocated region, aligned down to 16 bytes,
     * leaving a whole minimum frame above it.
     *
     * 48 bytes is the LINKAGE area alone, and it is not enough: under the PPC64
     * ELF v1 ABI the PS3 uses, a callee saves its register parameters into the
     * PARAMETER SAVE area of its CALLER's frame -- 8 doublewords immediately
     * above the linkage area -- so the very first call writes as far as sp+0x70.
     * With only 48 reserved, Uncharted's first spawned thread stored at exactly
     * sp+0x30, one byte past the end of its stack and into the next stack's
     * guard page: SIGBUS at guest 0xD0014000. Windows never saw it because
     * there the guest pages commit on demand instead of being PROT_NONE.
     *
     * The ABI minimum is 48 (linkage) + 64 (parameter save) = 112, but a thread
     * body is compiled as an ordinary function and writes into the frame its
     * CALLER is supposed to own -- and a thread has no caller, so that frame has
     * to exist above sp or the first store runs off the end of the stack.
     * Uncharted's Job Manager handler was measured reaching sp+0x30, then
     * sp+0x80, then sp+0x200 as each reserve was raised. 4 KB is 6% of a 64 KB
     * stack and covers any plausible initial frame, which ends the class of
     * fault instead of the instance. */
    uint64_t sp = (stack_top + stack_size - 0x1000) & ~(uint64_t)0xF;
    ctx->gpr[1] = sp;
}

/* Get current stack pointer. */
static inline uint64_t ppu_get_sp(const ppu_context* ctx)
{
    return ctx->gpr[1];
}

/* ---------------------------------------------------------------------------
 * Register accessors by ABI name
 * -----------------------------------------------------------------------*/
#define PPU_SP(ctx)   ((ctx)->gpr[1])
#define PPU_TOC(ctx)  ((ctx)->gpr[2])
#define PPU_ARG0(ctx) ((ctx)->gpr[3])
#define PPU_ARG1(ctx) ((ctx)->gpr[4])
#define PPU_ARG2(ctx) ((ctx)->gpr[5])
#define PPU_ARG3(ctx) ((ctx)->gpr[6])
#define PPU_ARG4(ctx) ((ctx)->gpr[7])
#define PPU_ARG5(ctx) ((ctx)->gpr[8])
#define PPU_ARG6(ctx) ((ctx)->gpr[9])
#define PPU_ARG7(ctx) ((ctx)->gpr[10])
#define PPU_RET(ctx)  ((ctx)->gpr[3])

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PPU_CONTEXT_H */

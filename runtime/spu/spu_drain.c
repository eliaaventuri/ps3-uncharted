/* SPU_DRAIN trampoline model -- runtime state + stub hooks (faithful-adopt).
 *
 * Milestone 1 lands the execution-model plumbing; the per-transfer hooks
 * (lockstep gate, flight recorder, SPURS task-launch) are inert stubs here and
 * gain real bodies in their own milestones. Keeping them as real out-of-line
 * functions (not macros) means the SPU_DRAIN/SPU_RET call sites are already in
 * place -- later milestones only replace the bodies. */
#include "spu_context.h"

/* Pending cross-function transfer target for this host thread's SPU context. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

/* yz_lockstep_tick now has its real body in spu_lockstep.c (milestone 2). */

/* SPURS task-launch interception at a trampoline hop (milestone: SPURS kernel).
 * No-op until then. */

/* PM flow trace (SPURS_PM_FLOW=1): record every cross-function transfer of ONE
 * policy-module run (the ctx spurs_policy.c arms) so the post-claim decision
 * path can be reconstructed offline. Written by the drain-loop hook below;
 * armed/dumped by spu_run_policy_module. */
uint32_t          g_pm_flow_buf[8192];
volatile unsigned g_pm_flow_n = 0;
void* volatile    g_pm_flow_ctx = 0;

#include <stdlib.h>
#include <stdio.h>

/* Sync-model ring-drain: the wwsjob PM's job ring drains only when jobs mark
 * their record's "done" bit (0x8000 in the record's first halfword @LS 0xDF0).
 * On hardware the async load-completion INTERRUPT sets it; our loads complete
 * synchronously, so a job whose code never got a ring slot stays "pending"
 * forever and the ring deadlocks (scan re-bails at LS 0x2318). When we observe
 * that bail spin, the in-flight loads HAVE in fact completed in our model, so
 * set the done bit on the pending records -- the faithful sync equivalent of
 * the completion interrupt. Env LBP_JOBDRAIN (default off while validating). */
extern void spu_halt(spu_context*);
void spu_task_launch_check(spu_context* ctx, void* fn)
{
    (void)fn;
    /* Percorso veloce (sessione 28): questa funzione gira a ogni ingresso di
     * funzione liftata ed era il 6% del tempo delle lane con otto sonde
     * statiche in fila. Senza variabili di trace resta solo il controllo
     * "salto a LS 0 = job finito" e il buffer del flow del PM. */
    static int s_fast = -1;
    if (s_fast < 0) s_fast = !(getenv("SPU_EXITTRACE") || getenv("SPU_STEPTRACE") || getenv("SPURS_PM_WATCHDOG") ||
                               getenv("LBP_JOBDRAIN") || getenv("SPU_NO_LS0_END"));
    if (s_fast) {
        ctx->steps++;
        if (__builtin_expect((ctx->pc & SPU_LS_MASK) == 0 && ctx->steps > 1 && !ctx->policy_mode && ctx->image_id > 0, 0)) {
            static int _n = 0;
            if (_n++ < 8)
                fprintf(stderr, "[spurs-job] img=%d branched to LS 0 -- job complete\n", ctx->image_id);
            spu_halt(ctx);
            return;
        }
        if (g_pm_flow_ctx == (void*)ctx && g_pm_flow_n < 8192)
            g_pm_flow_buf[g_pm_flow_n++] = ctx->pc;
        return;
    }
    /* A SPURS job returning to LS 0 is finished -- its crt tail-jumps to the
     * resident job manager, and with the job loaded at 0 that lands on its own
     * entry. Planting a return address in r0 catches the jobs that get there
     * via `bi $r0`, but some branch to 0 DIRECTLY, which the lifter turns into
     * a plain trampoline that never reaches spu_indirect_branch. The drain sees
     * every step, so catch it here too. Step 0 is the real entry. */
    static int s_no_ls0 = -1;
    if (s_no_ls0 < 0) s_no_ls0 = getenv("SPU_NO_LS0_END") ? 1 : 0;
    /* SPU_EXITTRACE=<img>: remember the last PCs this image executed and dump
     * them when the job ends. "Why did it exit here" is a question about the
     * path taken, and a forward trace from the entry never reaches far enough
     * to show it. */
    static int64_t s_et = -2;
    if (s_et == -2) { const char* e = getenv("SPU_EXITTRACE");
                      s_et = e ? strtol(e, 0, 0) : -1; }
    static uint32_t s_ring[64]; static uint32_t s_ri;
    if (s_et >= 0 && ctx->image_id == s_et)
        s_ring[s_ri++ & 63] = (uint32_t)ctx->pc & SPU_LS_MASK;

    if (!s_no_ls0 && ctx->steps++ && (ctx->pc & SPU_LS_MASK) == 0 &&
        !ctx->policy_mode && ctx->image_id > 0) {
        static int _n = 0;
        if (_n++ < 8)
            fprintf(stderr, "[spurs-job] img=%d branched to LS 0 -- job complete\n",
                    ctx->image_id);
        if (s_et >= 0 && ctx->image_id == s_et) {
            fprintf(stderr, "[spu-exit] img=%d last PCs:", ctx->image_id);
            for (int i = 32; i >= 1; i--)
                fprintf(stderr, " %05X", s_ring[(s_ri - i) & 63]);
            fputc(10, stderr); fflush(stderr);
        }
        spu_halt(ctx);
        return;
    }
    /* SPU_STEPTRACE=<img>: pc and the argument registers at every trampoline
     * step. This runs on each drain iteration, so it is the finest-grained
     * view of a register file changing under a running job. */
    { static int64_t s_t = -2;
      if (s_t == -2) { const char* e = getenv("SPU_STEPTRACE"); s_t = e ? strtol(e,0,0) : -1; }
      static int64_t s_from = -2;
      if (s_from == -2) { const char* e = getenv("SPU_STEPTRACE_FROM");
                          s_from = e ? strtol(e,0,16) : -1; }
      static int s_armed = 0;
      if (s_t >= 0 && ctx->image_id == s_t) {
          if (s_from >= 0 && !s_armed &&
              ((uint32_t)ctx->pc & SPU_LS_MASK) == (uint32_t)s_from) s_armed = 1;
          static int n = 0;
          if ((s_from < 0 || s_armed) && n++ < 40) {
              fprintf(stderr, "[step] pc=0x%05X r1=0x%05X r2=0x%08X r3=0x%08X r4=0x%08X\n",
                      (uint32_t)ctx->pc & SPU_LS_MASK, ctx->gpr[1]._u32[0],
                      ctx->gpr[2]._u32[0], ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0]);
              fflush(stderr);
          }
      } }
    if (g_pm_flow_ctx == (void*)ctx && g_pm_flow_n < 8192)
        g_pm_flow_buf[g_pm_flow_n++] = ctx->pc;
    { static int s_wd = -1;
      if (s_wd < 0) s_wd = getenv("SPURS_PM_WATCHDOG") ? 1 : 0;   /* SPURS_PM_WATCHDOG=<s>: anello degli ultimi 16 salti per contesto, letto dal watchdog delle corse del PM (spurs_policy.c) */
      if (s_wd) ctx->wd_ring[ctx->wd_ring_i++ & 15] = (uint32_t)ctx->pc & SPU_LS_MASK; }

    static int s_on = -1;
    if (s_on < 0) s_on = getenv("LBP_JOBDRAIN") ? 1 : 0;
    if (!s_on || ctx->image_id != 2 || !ctx->policy_mode) return;

    /* 0x2318 = the type-2 load's ring-full bail (resets scan, retries). */
    if ((ctx->pc & 0x3FFFF) != 0x2318) return;

    static unsigned s_bail = 0;
    if (++s_bail < 4096) return;      /* only after a real spin, not one pass */
    s_bail = 0;

    /* Set bit 31 (the 0x8000 first-halfword done flag) on each pending record
     * the release sweep scans (16 slots, 4-byte stride at LS 0xDF0), for any
     * record that is non-empty and not already done. */
    int marked = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t w = spu_ls_get32(ctx->ls, 0xDF0 + i * 4);
        if (w == 0 || (w & 0x80000000u)) continue;
        w |= 0x80000000u;
        spu_ls_put32(ctx->ls, 0xDF0 + i * 4, w);
        marked++;
    }
    static int _n = 0;
    if (marked && _n++ < 12)
        fprintf(stderr, "[pm-jobdrain] ring-full spin: marked %d pending records done\n", marked);
}

/* Restore the active image after a lifted call bracket. The brsl/bisl emission
 * saves image_id in a call-site local and hands it back here so an image
 * adopted inside the callee cannot leak into the caller's continuation. A
 * persistent LS-0xA00 workload module (module_img_a00) is re-applied since the
 * plain restore would undo it. */
void spu_img_restore(spu_context* ctx, int32_t saved_img)
{
    ctx->image_id = ctx->module_img_a00 ? ctx->module_img_a00 : saved_img;
}

/* spu_ch_wake now has its real body in spu_channels.c (milestone 3). */

/* --- SPU interrupt dispatch (drain-loop hook) ------------------------------
 * Called when int_enable && (event_status & event_mask): architectural SPU
 * interrupt. srr0 <- the interrupted continuation (ctx->pc already holds the
 * pending transfer target), interrupts disable, and control vectors through
 * the guest-planted branch instruction at LS 0 (the WWS jobmanager's entry
 * writes `bra 0xA2C` there; its handler drives the DMA-list job pipeline and
 * returns via iret). If LS 0 holds no br/bra, the interrupt is NOT taken --
 * we log once and resume normal flow rather than jump into zeros. */
#include <stdio.h>
#include <stdlib.h>

/* ---- Deferred DMA-list stall-and-notify delivery --------------------------
 * Real MFC hardware processes a queued list command (the WWS job manager's
 * barriered null list, MFC_GETLB) ASYNCHRONOUSLY: the stall-and-notify -- and
 * the interrupt it raises -- land after the SPU has run on past the code that
 * armed it. We execute the list synchronously inside `wrch MFC_Cmd`, so the
 * interrupt was delivered before the job manager had stored
 * g_WwsJob_loadJobState = kReadCommands (LS 0x12A0).
 *
 * Its interrupt handler gates the Load->Run advance on BOTH the ch25 stall mask
 * carrying tag 0 (kLoadJob_readCommands) AND loadJobState == kReadCommands. With
 * the state still kNone it correctly declined to advance -- and because
 * MFC_RdListStallStat is "accumulative, clear on read" that single stall was
 * consumed, so no later interrupt could ever advance it: the job never ran and
 * LBP's loading thread deadlocked on a completion that never came.
 *
 * Holding the interrupt off for N drain ticks was tried and DISPROVEN: a probe
 * at the stall site shows loadJobState is ALREADY 0 when the GETLB is issued, so
 * the arming store never ran on this path at all -- it is not an interrupt-timing
 * race. (The pm takes trychangefreetoloadjob's early-exit `ceqhi jobHeader,1 /
 * brhz cond1,Exit` to LS 0x2D30, skipping the Load setup.) Default OFF; kept
 * env-gated for experiments: SPU_SN_DEFER=<n> ticks (0 = immediate, faithful
 * to the current synchronous list execution). */
void* volatile   g_sn_defer_ctx = 0;
volatile unsigned g_sn_defer     = 0;

unsigned spu_sn_defer_ticks(void)
{
    static int s_n = -1;
    if (s_n < 0) { const char* e = getenv("SPU_SN_DEFER"); s_n = e ? atoi(e) : 0; }
    return (unsigned)(s_n < 0 ? 0 : s_n);
}

/* ---- Interrupt register preservation --------------------------------------
 * Hardware contract: an SPU interrupt handler preserves every register (the
 * WWS jobmanager guarantees it with a save/restore shim it installs at the
 * TOP of LS, ~0x3FEC0 -- interrupthandlerasm.spu.s). That shim lies OUTSIDE
 * the PM image our lifter processed, so the lifted handler runs WITHOUT the
 * save/restore and eats the interrupted code's live registers. Measured
 * mid-GetBufferTag: the fsmbi mask registers (r13/r15) arrived zeroed at the
 * packing selbs, the BufferTag's lanes collapsed, the job saw a NULL buffer
 * and bailed -- LBP's loading freeze.
 *
 * Enforce the contract host-side: snapshot the full GPR file when the
 * interrupt is taken, restore it when the handler irets (detected in
 * spu_indirect_branch: pc == saved srr0 with interrupts re-enabled -- during
 * the handler int_enable stays 0, so the first enabled dispatch at srr0 IS
 * the irete). State the handler legitimately publishes lives in LS/channels
 * and is untouched. Per-ctx slots; no nesting (interrupts stay disabled
 * until iret). */
static void spu_irq_regs_save(spu_context* ctx)
{
    if (ctx->irq_save_valid >= SPU_IRQ_NEST) {
        static int _n = 0;
        if (_n++ < 4) fprintf(stderr, "[spu-int] annidamento oltre %d livelli: istantanea persa\n", SPU_IRQ_NEST);
        return;
    }
    uint32_t d = ctx->irq_save_valid;
    ctx->irq_resume_stack[d] = ctx->srr0;
    memcpy(ctx->irq_gpr[d], ctx->gpr, sizeof ctx->irq_gpr[d]);
    ctx->irq_resume_pc = ctx->srr0;
    ctx->irq_save_valid = d + 1;
}

/* Called from spu_indirect_branch on every dispatch. Restores + clears when
 * the iret lands. Returns 1 if a restore happened (diagnostic). */
/* Drop any saved register file belonging to this context: a fresh run must
 * never inherit the registers of an interrupt taken by an earlier one. */
void spu_irq_regs_forget(spu_context* ctx)
{
    ctx->irq_save_valid = 0;
}

int spu_irq_regs_maybe_restore(spu_context* ctx)
{
    if (!ctx->irq_save_valid) return 0;
    if ((ctx->pc & SPU_LS_MASK) == (ctx->irq_resume_pc & SPU_LS_MASK) &&
        ctx->int_enable) {
        uint32_t d = ctx->irq_save_valid - 1;
        memcpy(ctx->gpr, ctx->irq_gpr[d], sizeof ctx->irq_gpr[d]);
        ctx->irq_save_valid = d;
        ctx->irq_resume_pc = d ? ctx->irq_resume_stack[d - 1] : 0;
        return 1;
    }
    return 0;
}

void (*spu_take_interrupt(spu_context* ctx,
                          void (*tf)(spu_context*)))(spu_context*)
{
    /* Stall-and-notify still settling: let the SPU run on (see above). */
    if (g_sn_defer && g_sn_defer_ctx == (void*)ctx) {
        if (--g_sn_defer == 0) g_sn_defer_ctx = 0;
        return tf;
    }
    uint32_t w = spu_ls_get32(ctx->ls, 0);   /* vector word at LS 0 */
    uint32_t op9 = w >> 23;          /* top 9 opcode bits */
    uint32_t target;
    if (op9 == 0x060) {              /* bra i16 (absolute) */
        target = ((w >> 7) & 0xFFFF) << 2;
    } else if (op9 == 0x064) {       /* br i16 (relative to LS 0) */
        target = (((w >> 7) & 0xFFFF) << 2) & SPU_LS_MASK;
    } else {
        /* Contatore globale atomico: quello locale statico lasciava passare
         * milioni di righe (10 M in una corsa) con piu' thread SPU. */
        static int s_noint_warn = 0;
        if (__atomic_fetch_add(&s_noint_warn, 1, __ATOMIC_RELAXED) < 4)
            fprintf(stderr, "[spu-int] pending (st=0x%X mask=0x%X) but LS0 word "
                    "0x%08X is no branch -- not taken (c%04X img=%d ovl=%d pc=0x%05X)\n",
                    ctx->event_status, ctx->event_mask, w,
                    (unsigned)(((uintptr_t)ctx >> 12) & 0xFFFF), ctx->image_id, ctx->resident_ovl,
                    (uint32_t)ctx->pc & SPU_LS_MASK);
        return tf;
    }
    ctx->srr0 = ctx->pc;             /* resume point for iret */
    ctx->int_enable = 0;
    spu_irq_regs_save(ctx);          /* hardware contract: handler preserves regs */
    { static int s_it = -1; if (s_it < 0) s_it = getenv("SPU_IRQTRACE") ? 1 : 0;
      if (s_it) { static int _n = 0; if (_n++ < 200)
        fprintf(stderr, "[irq] TAKE srr0=0x%05X depth=%d r13=%08X r15=%08X\n",
                ctx->srr0 & SPU_LS_MASK, ctx->host_depth,
                ctx->gpr[13]._u32[0], ctx->gpr[15]._u32[0]); } }
    ctx->pc = target & SPU_LS_MASK;
    { static int _n = 0;
      if (_n++ < 16) {
          /* WWS Load->Run diagnosis: the job-manager interrupt handler advances
           * g_WwsJob_loadJobState kReadCommands(1)->kExecuteCommands(2) only if
           * (a) the ch25 stall mask has tag 0 (kLoadJob_readCommands) AND
           * (b) loadJobState's HALFWORDS compare equal to 1 (ceqhi).
           * Log both at interrupt entry so we can see which test fails. */
          const uint8_t* js = spu_ls_be(ctx->ls, 0x12A0, 16);   /* g_WwsJob_loadJobState quadword */
          fprintf(stderr, "[spu-int] TAKEN img=%d events=0x%X&0x%X vector->0x%05X "
                  "(srr0=0x%05X) stallstat=0x%X parked=0x%X loadJobState@12A0=%02X%02X%02X%02X "
                  "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n", ctx->image_id,
                  ctx->event_status, ctx->event_mask, ctx->pc, ctx->srr0,
                  ctx->list_stall_stat, ctx->list_stall_mask,
                  js[0],js[1],js[2],js[3], js[4],js[5],js[6],js[7],
                  js[8],js[9],js[10],js[11], js[12],js[13],js[14],js[15]); } }
    return spu_indirect_branch;
}

/* See the declaration in spu_context.h. */
void spu_depth_guard(spu_context* ctx)
{
    static uint32_t s_max = 0;
    if (!s_max) {
        const char* e = getenv("SPU_HOST_DEPTH_MAX");
        s_max = e ? (uint32_t)strtoul(e, 0, 0) : 2000u;
    }

    /* Ring of recent drain sites. When the depth trips, the useful question is
     * not "where are we" but "what cycle got us here" -- one pc names a point,
     * a ring names the loop. */
    enum { RING = 24 };
    static uint32_t s_ring[RING];
    static uint32_t s_n;
    s_ring[s_n % RING] = ((uint32_t)ctx->pc & SPU_LS_MASK);
    s_n++;

    if (ctx->host_depth < s_max) return;
    static int s_reported = 0;
    if (s_reported < 1) {   /* one report: five SPU threads all trip together */
        s_reported++;
        fprintf(stderr, "[spu-depth] img=%d pc=0x%05X lr=0x%05X host_depth=%u"
                        " -- lifted call recursion, halting the SPU\n",
                ctx->image_id, (uint32_t)ctx->pc & SPU_LS_MASK,
                ctx->gpr[0]._u32[0] & SPU_LS_MASK, ctx->host_depth);
        fprintf(stderr, "[spu-depth] recent drain sites (oldest first):");
        uint32_t start = (s_n > RING) ? (s_n - RING) : 0;
        for (uint32_t k = start; k < s_n; k++)
            fprintf(stderr, " %05X", s_ring[k % RING]);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    { extern void spu_halt(spu_context*); spu_halt(ctx); }
}

/* See spu_context.h. Env-gated: SPU_TAILRET=1. */
int spu_tailret_enabled(void)
{
    static int s_on = -1;
    if (s_on < 0) { const char* e = getenv("SPU_TAILRET"); s_on = (e && e[0] == 0x31) ? 1 : 0; }
    return s_on;
}
/* SPU_RETLINK=1: vedi il commento in SPU_DRAIN. Spento di default. */
int spu_retlink_enabled(void)
{
    static int s = -1;
    if (s < 0) { const char* e = getenv("SPU_RETLINK"); s = (e && *e && *e != '0') ? 1 : 0; }
    return s;
}


/* Via veloce del drain (s.42, v. SPU_DRAIN in spu_context.h). Il ciclo
 * completo resta in SPU_DRAIN_FULL e si usa con qualunque variabile che
 * accenda una sonda del ciclo, con il lockstep o con SPU_TAILRET=1. */
int g_spu_drain_mode = 0;
int g_spu_xfer_inline = 0, g_spu_ib_inline = 0;   /* s.48, v. SPU_XJUMP */

static int spu_drain_mode_init(void)
{
    static const char* const vars[] = { "SPU_DRAIN_FULL", "SPU_EXITTRACE", "SPU_STEPTRACE",
        "SPURS_PM_WATCHDOG", "LBP_JOBDRAIN", "SPU_NO_LS0_END", "SPU_PROBE_PC", "SPU_PCWATCH",
        "SPU_HOPHIST", "SPU_HOST_DEPTH_MAX", NULL };
    for (int i = 0; vars[i]; i++) if (getenv(vars[i])) return 2;
    const char* e = getenv("YZ_SPU_LOCKSTEP");
    if (e && *e && *e != '0') return 2;
    return spu_tailret_enabled() ? 2 : 1;
}

void spu_drain_slow(spu_context* ctx)
{
    if (!g_spu_drain_mode) {
        int m = spu_drain_mode_init();
        static int _n = 0;
        if (_n++ == 0) fprintf(stderr, "[spu-drain] ciclo %s\n", m == 1 ? "veloce" : "completo (diagnostica)");
        g_spu_drain_mode = m;
        /* s.48: trasferimenti in linea solo col drain veloce */
        { const char* e = getenv("SPU_XFER_INLINE");
          g_spu_xfer_inline = (m == 1 && !(e && *e == '0')) ? 1 : 0;
          fprintf(stderr, "[spu-drain] trasferimenti in linea %s\n", g_spu_xfer_inline ? "accesi" : "spenti"); }
    }
    SPU_DRAIN_FULL(ctx);
}


#ifdef SPU_HOPSTAT
/* Sonda temporanea s.48 (-DSPU_HOPSTAT): istogramma dei salti per
 * (tipo, immagine/overlay, pc). tipo 0 = hop diretto del trampolino,
 * 1 = hop verso spu_indirect_branch, 2 = ogni ingresso in spu_indirect_branch. */
#include <stdatomic.h>
#define HS_N (1u << 16)
static _Atomic uint64_t hs_key[HS_N]; static _Atomic uint64_t hs_cnt[HS_N];
static _Atomic uint64_t hs_tot[16], hs_all;
void spu_hopstat(int kind, spu_context* ctx)
{
    uint64_t k = ((uint64_t)kind << 48) | ((uint64_t)(uint16_t)ctx->image_id << 32) |
                 ((uint64_t)(uint8_t)ctx->resident_ovl << 24) | (ctx->pc & SPU_LS_MASK);
    k += 1;
    uint32_t h = (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 48);
    for (;;) { uint64_t e = atomic_load_explicit(&hs_key[h], memory_order_relaxed);
        if (e == k) break;
        if (e == 0) { uint64_t z = 0; if (atomic_compare_exchange_strong(&hs_key[h], &z, k) || z == k) break; }
        h = (h + 1) & (HS_N - 1); }
    atomic_fetch_add_explicit(&hs_cnt[h], 1, memory_order_relaxed);
    uint64_t t = atomic_fetch_add_explicit(&hs_tot[kind], 1, memory_order_relaxed) + 1;
    (void)t;
    if ((atomic_fetch_add_explicit(&hs_all, 1, memory_order_relaxed) + 1) % 20000000ull == 0) {
        fprintf(stderr, "[hopstat] tot diretti=%llu ind-hop=%llu ind-tutti=%llu\n",
                (unsigned long long)hs_tot[0], (unsigned long long)hs_tot[1], (unsigned long long)hs_tot[2]);
        for (int r = 0; r < 60; r++) { uint64_t best = 0; uint32_t bi = 0;
            for (uint32_t i = 0; i < HS_N; i++) { uint64_t c = hs_cnt[i]; if (c > best) { best = c; bi = i; } }
            if (!best) break; uint64_t kk = hs_key[bi] - 1;
            fprintf(stderr, "[hopstat] %d img=%u ovl=%u pc=%05X %llu\n", (int)(kk >> 48), (unsigned)((kk >> 32) & 0xFFFF),
                    (unsigned)((kk >> 24) & 0xFF), (unsigned)(kk & 0xFFFFFF), (unsigned long long)best);
            hs_cnt[bi] = 0; }
        memset((void*)hs_key, 0, sizeof hs_key); memset((void*)hs_cnt, 0, sizeof hs_cnt); }
}
#endif

/* Lo stesso ciclo di SPU_DRAIN_FULL senza le sonde: ritorno esatto
 * (SPU_RETLINK), fine del job a LS 0 e anello del flow del PM come nella via
 * veloce di spu_task_launch_check, interrupt. */
void spu_drain_fast(spu_context* ctx)
{
    const int retlink = spu_retlink_enabled();
    void (*tf)(spu_context*);
    while ((tf = g_spu_trampoline_fn) != 0) {
        ctx->hops++;
        int irq = (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ? 1 : 0;
        if (!irq && retlink && ctx->host_depth > 0 && ctx->ret_link &&
            ((uint32_t)ctx->pc & SPU_LS_MASK) == ctx->ret_link) {
            g_spu_trampoline_fn = 0;
            return;
        }
        g_spu_trampoline_fn = 0;
#ifdef SPU_HOPSTAT
        if (tf == spu_indirect_branch) {
            /* motivo per cui la via in linea non l'ha preso: 3 = interrupt
             * pendente, 4 = pc speciale, 5 = irq_save_valid, 6 = cache mancata,
             * 7 = img 23, 8 = presa adesso (sito senza XIND: stub/iret/...) */
            uint32_t pc = (uint32_t)ctx->pc & SPU_LS_MASK & ~3u; int why;
            if (ctx->int_enable && (ctx->event_status & ctx->event_mask)) why = 3;
            else if (pc == 0 || pc >= 0x3FEC0u || pc == SPURS_PM_EXIT_TO_KERNEL_LS || pc == SPURS_PM_SELECT_WORKLOAD_LS || pc == 0xA70u) why = 4;
            else if (ctx->irq_save_valid) why = 5;
            else if (ctx->image_id == 23) why = 7;
            else { const uint64_t key = pc | ((uint64_t)(uint16_t)ctx->image_id << 32) | ((uint64_t)(uint16_t)ctx->resident_ovl << 48);
                   why = ctx->ib_cache[spu_ib_slot(key)].key == key ? 8 : 6; }
            spu_hopstat(why, ctx);
        } else spu_hopstat(0, ctx);
#endif
        ctx->steps++;
        if (__builtin_expect((ctx->pc & SPU_LS_MASK) == 0 && ctx->steps > 1 && !ctx->policy_mode && ctx->image_id > 0, 0)) {
            static int _n = 0;
            if (_n++ < 8)
                fprintf(stderr, "[spurs-job] img=%d branched to LS 0 -- job complete\n", ctx->image_id);
            spu_halt(ctx);
        } else if (g_pm_flow_ctx == (void*)ctx && g_pm_flow_n < 8192)
            g_pm_flow_buf[g_pm_flow_n++] = ctx->pc;
        if (irq)
            tf = spu_take_interrupt(ctx, tf);
        tf(ctx);
    }
}

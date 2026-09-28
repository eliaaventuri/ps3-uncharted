/*
 * ps3recomp - SPU (Synergistic Processing Unit) execution context
 *
 * Models the full architectural state of an SPU:
 *   - 128 x 128-bit general-purpose registers
 *   - 256 KB local store
 *   - Channel state (MFC command queue, mailboxes, signal notification)
 */

#ifndef SPU_CONTEXT_H
#define SPU_CONTEXT_H

#include "../../include/ps3emu/ps3types.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Portable 16-byte alignment (MSVC __declspec vs GCC/clang __attribute__). */
#if defined(_MSC_VER)
#  define SPU_ALIGN16 __declspec(align(16))
#else
#  define SPU_ALIGN16 __attribute__((aligned(16)))
#endif

/* Local store size: 256 KB */
#define SPU_LS_SIZE         (256 * 1024)
#define SPU_LS_MASK         (SPU_LS_SIZE - 1)
/* ordine dei byte della LS: vedi "Ordine dei byte della local store" sotto (s.47) */
#include "spu_ls_order.h"   /* SPU_LS_XOR, SPU_LSB */

/* ---------------------------------------------------------------------------
 * Reusable env-driven LS watchpoint (mini-debugger, no rebuild to retarget).
 *   LBP_SPU_WATCH=0x1BE80        watch one 16-byte LS line (reads + writes)
 *   LBP_SPU_WATCH=0x1BE80,0x927D80  up to 4 comma-separated addresses
 * Fires from every SPU image's spu_ls_read128/write128. Cheap: one cached
 * compare on the hot path when disabled.
 * -----------------------------------------------------------------------*/
#define SPU_WATCH_MAX 4
static inline unsigned* spu_ls_watch_list(int* out_n) {
    static int init = 0; static unsigned addr[SPU_WATCH_MAX]; static int n = 0;
    if (!init) {
        init = 1;
        const char* e = getenv("LBP_SPU_WATCH");
        while (e && *e && n < SPU_WATCH_MAX) {
            addr[n++] = (unsigned)strtoul(e, (char**)&e, 0) & ~0xFu;
            while (*e == ',' || *e == ' ') e++;
        }
    }
    *out_n = n;
    return addr;
}
static inline void spu_ls_watch_hit2(uint32_t lsa, int is_write, const uint8_t* p,
                                     uint32_t pc, uint32_t lr) {
    /* percorso caldo (ogni lettura/scrittura della LS): un solo confronto
     * quando la watch e' spenta (era il 7.6% del tempo delle lane) */
    static int s_off = -1;
    if (s_off > 0) return;
    int n; unsigned* w = spu_ls_watch_list(&n);
    if (!n) { s_off = 1; return; }
    s_off = 0;
    uint32_t a = lsa & (SPU_LS_MASK & ~0xFu);
    for (int i = 0; i < n; i++) {
        if (w[i] == a) {
            fprintf(stderr, "[spu-watch %s 0x%05X pc=0x%05X lr=0x%05X] "
                "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
                is_write ? "WR" : "rd", a, pc, lr,
                p[SPU_LSB(0)],p[SPU_LSB(1)],p[SPU_LSB(2)],p[SPU_LSB(3)], p[SPU_LSB(4)],p[SPU_LSB(5)],p[SPU_LSB(6)],p[SPU_LSB(7)],
                p[SPU_LSB(8)],p[SPU_LSB(9)],p[SPU_LSB(10)],p[SPU_LSB(11)], p[SPU_LSB(12)],p[SPU_LSB(13)],p[SPU_LSB(14)],p[SPU_LSB(15)]);
            fflush(stderr);
            break;
        }
    }
}
static inline void spu_ls_watch_hit(uint32_t lsa, int is_write, const uint8_t* p) {
    spu_ls_watch_hit2(lsa, is_write, p, 0, 0);
}

/* Maximum number of MFC tag groups */
#define SPU_MFC_MAX_TAGS    32

/* Mailbox / signal capacities */
#define SPU_MBOX_DEPTH      1    /* SPU write outbound mailbox depth */
#define SPU_INTR_MBOX_DEPTH 1    /* SPU write inbound interrupt mailbox depth */

/* ---------------------------------------------------------------------------
 * SPU channel IDs
 * -----------------------------------------------------------------------*/
#define SPU_RdEventStat     0
#define SPU_WrEventMask     1
#define SPU_WrEventAck      2
#define SPU_RdSigNotify1    3
#define SPU_RdSigNotify2    4
#define SPU_WrDec           7
#define SPU_RdDec           8
#define SPU_RdEventMask     11
#define SPU_RdMachStat      13
#define SPU_WrSRR0          14
#define SPU_RdSRR0          15
#define SPU_WrOutMbox       28
#define SPU_RdInMbox        29
#define SPU_WrOutIntrMbox   30

/* MFC channels */
#define MFC_WrMSSyncReq     9
#define MFC_RdTagMask       12
#define MFC_LSA             16
#define MFC_EAH             17
#define MFC_EAL             18
#define MFC_Size            19
#define MFC_TagID           20
#define MFC_Cmd             21
#define MFC_WrTagMask       22
#define MFC_WrTagUpdate     23
#define MFC_RdTagStat       24
#define MFC_RdListStallStat 25
#define MFC_WrListStallAck  26
#define MFC_RdAtomicStat    27

/* ---------------------------------------------------------------------------
 * MFC DMA command opcodes
 * -----------------------------------------------------------------------*/
#define MFC_PUT_CMD         0x20
#define MFC_PUTB_CMD        0x21
#define MFC_PUTF_CMD        0x22
#define MFC_GET_CMD         0x40
#define MFC_GETB_CMD        0x41
#define MFC_GETF_CMD        0x42
#define MFC_PUTL_CMD        0x24
#define MFC_PUTLB_CMD       0x25
#define MFC_PUTLF_CMD       0x26
#define MFC_GETL_CMD        0x44
#define MFC_GETLB_CMD       0x45
#define MFC_GETLF_CMD       0x46
#define MFC_SNDSIG_CMD      0xA0
#define MFC_BARRIER_CMD     0xC0
#define MFC_EIEIO_CMD       0xC8
#define MFC_SYNC_CMD        0xCC

/* Atomic (lock-line reservation) commands -- operate on a 128-byte line. */
#define MFC_GETLLAR_CMD     0xD0   /* get + reserve line                      */
#define MFC_PUTLLC_CMD      0xB4   /* store conditional (fails if line moved) */
#define MFC_PUTLLUC_CMD     0xB0   /* store unconditional, clears reservation */
#define MFC_PUTQLLUC_CMD    0xB8   /* queued store unconditional              */
#define MFC_ATOMIC_LINE     128

/* ---------------------------------------------------------------------------
 * Channel state
 * -----------------------------------------------------------------------*/
typedef struct spu_channel {
    uint32_t value;
    uint32_t count;   /* number of valid entries (0 or 1 for most channels) */
} spu_channel;

/* ---------------------------------------------------------------------------
 * SPU execution context
 * -----------------------------------------------------------------------*/
typedef struct spu_context {
    /* 128 general-purpose 128-bit registers */
    SPU_ALIGN16 u128 gpr[128];

    /* 256 KB local store, 16-byte aligned */
    SPU_ALIGN16 uint8_t ls[SPU_LS_SIZE];

    /* Program counter (local store address, 0-0x3FFFF) */
    uint32_t pc;

    /* SPU status (running, stopped, etc.) */
    uint32_t status;
    #define SPU_STATUS_STOPPED      0x0
    #define SPU_STATUS_RUNNING      0x1
    #define SPU_STATUS_STOPPED_BY_STOP  0x2
    #define SPU_STATUS_STOPPED_BY_HALT  0x4
    #define SPU_STATUS_WAITING_CHANNEL  0x8

    /* 14-bit signal code of the most recent `stop`/`stopd`. SPURS leaf tasks
     * invoke kernel syscalls via `stop <code>` (CELL_SPURS_TASK_SYSCALL_EXIT=0,
     * YIELD=1, WAIT_SIGNAL=2, POLL=3, RECV_WKL_FLAG=4; |0x10 = the v2 form), so
     * the taskset-PM emulation must read the code, not just halt. */
    uint32_t stop_code;
    #define SPU_STATUS_SINGLE_STEP  0x10

    /* SPU thread identification */
    uint32_t spu_id;
    uint32_t spu_group_id;

    /* Active recompiled image for per-context indirect-branch dispatch. SPURS
     * loads kernel/policy/job into overlapping LS addresses at different times,
     * so spu_indirect_branch resolves pc within the image currently selected
     * here. 0 = match any image (back-compat for single-image contexts). */
    int image_id;

    /* When set, a rchcnt(SPU_RdInMbox) that finds the inbound mailbox EMPTY halts
     * the SPU (spu_halt longjmp) instead of returning 0. Lets a persistent-worker
     * SPU (e.g. the Rubber Ducky AsyncCopy raw SPU) run SYNCHRONOUSLY: it does its
     * full init + ready-mailbox handshake, then parks the first time it idle-waits
     * for a PPU command -- no async host thread racing the PPU. */
    int park_on_empty_inmbox;

    /* Decrementer: a free-running down counter, ticking at the PS3 timebase.
     * SPU_WrDec latches the reload value here and stamps dec_base_ns; SPU_RdDec
     * derives the live value from the host clock. Storing only the written
     * value (and returning it unchanged) makes every timed wait immortal --
     * see SPU_RdDec in spu_channels.c. */
    uint32_t decrementer;
    uint64_t dec_base_ns;

    /* SRR0 - Save/Restore Register (exception return address) */
    uint32_t srr0;

    /* Event status / mask */
    uint32_t event_status;
    uint32_t event_mask;

    /* Channels */
    spu_channel ch_out_mbox;        /* SPU -> PPU outbound mailbox */
    spu_channel ch_in_mbox;         /* PPU -> SPU inbound mailbox */
    /* sys_spu_thread_receive_event (stop 0x110) replies with FOUR values --
     * {CELL_OK, data1, data2, data3} -- and the worker reads them back with
     * four rdch SPU_RdInMbox. spu_channel holds one, so queue the reply here
     * and let the in-mailbox read drain it first. */
    uint32_t rcv_evt[4];
    int      rcv_evt_n;             /* remaining unread reply words */
    int      rcv_evt_i;
    spu_channel ch_out_intr_mbox;   /* SPU -> PPU interrupt mailbox */
    spu_channel ch_sig_notify[2];   /* Signal notification 1 & 2 */

    /* MFC command staging registers (written via channels before issuing cmd) */
    uint32_t mfc_lsa;
    uint32_t mfc_eah;
    uint32_t mfc_eal;
    uint32_t mfc_size;
    uint32_t mfc_tag;

    /* MFC tag completion mask and status */
    uint32_t mfc_tag_mask;
    uint32_t mfc_tag_status;

    /* Atomic reservation state (GETLLAR/PUTLLC lock-line semantics). Multiple
     * SPU kernel threads share a lock-free queue in main memory; PUTLLC must
     * FAIL if the reserved 128-byte line changed since GETLLAR, or concurrent
     * claims corrupt the queue. */
    uint32_t resv_ea;          /* reserved 128-byte line EA, aligned (0 = none) */
    int      resv_valid;
    uint32_t atomic_stat;      /* last atomic op result -> MFC_RdAtomicStat */
    uint8_t  resv_line[128];   /* snapshot of the line at GETLLAR time */

    /* SPURS policy-module run mode (spurs_policy.c): nonzero while a lifted
     * policy module runs under the HLE'd kernel handoff. spu_indirect_branch
     * intercepts branches to the two reserved kernel-service addresses below
     * only when this is set. */
    int policy_mode;

    /* Resident code-overlay image id (0 = none). FMOD's mixer swaps per-DSP
     * plugin code in and out of ONE LS scratch region (e.g. the Compressor
     * streams over the Fader plugin's slot), so one static per-image function
     * registry cannot hold both: their lifted addresses overlap. The MFC GET
     * path recognizes a registered overlay's source EA and records which
     * overlay is now resident; dispatch retries a missed lookup against it. */
    int resident_ovl;

    /* --- SPU_DRAIN trampoline execution model (faithful-adopt, canersaka) ---
     * host_depth counts live lifted call frames (matched brsl/bisl). SPU_RET
     * (`bi $r0`) does a host `return` while host_depth>0 (the caller's frame is
     * live); at 0 it dispatches to the link register instead (a cross-context
     * task resume / restack unwind killed the frame). module_img_a00 is the
     * persistent workload-module image adopted at LS 0xA00, re-applied at
     * dispatch after a call-bracket image restore. */
    uint32_t host_depth;
    /* Indirizzo di ritorno del brsl/bisl attualmente in esecuzione (0 se
     * nessuno). Serve perche' `bi $rN` con rN != r0 puo' essere sia un salto
     * calcolato sia il ritorno di questa chiamata: se il target E' esattamente
     * questo indirizzo, e' il ritorno, e va fatto tornando dalla funzione host
     * invece di rientrare nel blocco dal trampolino (altrimenti il blocco viene
     * eseguito due volte -- una nel drain della chiamata e una per la caduta in
     * avanti del chiamante). Salvato e ripristinato attorno a ogni chiamata. */
    uint32_t ret_link;

    /* Trampoline steps taken since this context started running. Only needed
     * to tell a job's INITIAL entry at LS 0 from a later return to LS 0, which
     * means something quite different (see spu_task_launch_check). */
    uint32_t steps;
    int      module_img_a00;

    /* SPU lockstep gate (spu_lockstep.c; env YZ_SPU_LOCKSTEP, default off).
     * quantum_ctr counts tick sites toward a token handoff; release_tb stamps
     * the guest-timebase moment this ctx last released the token. dec_start_tb
     * is the lockstep decrementer-freeze anchor (written only while armed). */
    uint64_t lockstep_quantum_ctr;
    uint64_t lockstep_release_tb;
    uint64_t dec_start_tb;

    /* Channel-stall contract (spu_channels.c): a blocking spu_rdch on an empty
     * RdInMbox/RdSigNotify/RdEventStat parks the SPU host thread on this per-SPU
     * CV until a producer (mailbox/signal write, event raise) calls spu_ch_wake.
     * Storage for a Win32 CONDITION_VARIABLE + SRWLOCK -- each is pointer-sized
     * and zero-init is the valid initial value (CONDITION_VARIABLE_INIT /
     * SRWLOCK_INIT == {0}), so spu_context_init's memset is enough. Kept as
     * void* to avoid pulling <windows.h> into this widely-included header;
     * spu_channels.c casts to the real types. */
    void* ch_wait_cv;
    void* ch_wait_lock;

    /* --- SPU interrupt facility (faithful; WWS jobmanager depends on it) ---
     * The bi-family E/D bits (bie/bid, irete/iretd...) toggle int_enable; while
     * enabled, a pending (event_status & event_mask) vectors execution to LS 0
     * at the next trampoline transfer (SPU_DRAIN hook -> spu_take_interrupt):
     * srr0 <- interrupted pc, int_enable <- 0, pc <- target of the guest-planted
     * branch instruction AT LS 0 (pm_wwsjob's entry writes `bra 0xA2C` there).
     * The handler returns via iret (pc <- srr0). */
    uint32_t int_enable;

    /* Parked DMA-list commands (stall-and-notify), keyed PER TAG GROUP. A
     * GETL/PUTL that transfers a list element with the stall-and-notify bit
     * (bit 31 of word0) set raises SPU event 0x2 (SPU_EVENT_SN) and parks the
     * remainder here; MFC_WrListStallAck for the tag resumes it.
     *
     * The WWS job manager pipelines Load/Run/Store stages, each issuing its own
     * barriered-null-list interrupt on a DISTINCT tag (readCommands=0,
     * writeShareableBuffers=3, writeAllShareableBuffers=5, writeJobBuffers=6 --
     * see jobmanagerdmatags.h), so multiple stalls can be outstanding at once.
     * A single-slot park dropped all but the last -> a pipeline stage's
     * completion interrupt was lost -> the job ring deadlocked. Hardware (and
     * RPCS3) accumulate: ch_stall_stat ORs (1<<tag) per stall; rdch returns the
     * accumulated mask then clears it; ch_stall_mask tracks parked tags and is
     * cleared per-tag by WrListStallAck. Modelled here as per-tag arrays. */
    uint32_t list_stall_mask;             /* bit t set = tag t has a parked list */
    uint32_t list_stall_stat;             /* readable notify mask (rdch clears) */
    uint32_t list_stall_elem_lsa[32];     /* LS addr of NEXT element, per tag */
    uint32_t list_stall_remaining[32];    /* elements left after stall, per tag */
    uint32_t list_stall_dest_lsa[32];     /* accumulated destination LSA, per tag */
    uint64_t list_stall_ea_base[32];      /* ea base (hi32 carries), per tag */
    uint32_t list_stall_cmd[32];          /* base (non-list) MFC command, per tag */

    /* Anello degli ultimi salti dispacciati (SPURS_PM_WATCHDOG): ctx->pc si
     * aggiorna solo ai trampolini, e un solo campione non dice in quale ciclo
     * gira una lane ferma. Riempito in spu_task_launch_check. */
    uint32_t wd_ring[16];
    uint32_t wd_ring_i;

    /* Motore MFC di questo contesto (spu_channels.c mfc_for): cache del
     * puntatore allo slot, per non rifare la ricerca a ogni canale. */
    void* mfc_cache;

    /* File registri salvato all'ingresso di un interrupt (spu_drain.c
     * spu_irq_regs_save), ripristinato all'iret. Era una tabella globale di
     * 8 slot condivisa fra tutti i thread host: con le lane concorrenti di
     * WwsJob finivano gli slot ("behave as before" = niente ripristino) e
     * due thread si prendevano lo stesso slot. Per contesto, niente corse. */
    /* Pila di istantanee: il gestore del policy module di WwsJob salva i
     * registri sullo stack guest (2032 byte per livello) e puo' essere
     * interrotto a sua volta nella finestra `bie` del suo helper di attesa DMA
     * -- misurato in Uncharted: due TAKE a srr0=0x2430 di fila. Con una sola
     * istantanea la seconda sovrascriveva la prima e l'iret esterno
     * ripristinava i registri del gestore (r4 = 0x24F8 al posto del link). */
    uint32_t irq_save_valid;              /* = livelli salvati (0..SPU_IRQ_NEST) */
    uint32_t irq_resume_pc;               /* srr0 del livello piu' interno */
#define SPU_IRQ_NEST 4
    uint32_t irq_resume_stack[SPU_IRQ_NEST];
    u128     irq_gpr[SPU_IRQ_NEST][128];

    /* IN CODA: il codice liftato (spu_jobs/, spu_lifted/) e' compilato con
     * gli offset di questa struct e build.sh non lo ricompila quando cambia
     * l'header -- un campo aggiunto nel mezzo lo manda in "Illegal
     * instruction" all'avvio (sessione 28). */
    uint64_t dec_cache_ns;   /* ultimo clock host letto per SPU_RdDec (rinfrescato ogni 64 letture) */
    uint32_t dec_cache_n;    /* letture dall'ultimo rinfresco */
    uint64_t hops;           /* salti del trampolino (SPU_DRAIN), diagnostica [label-wait] */
    /* s.46: cache dei salti indiretti di spu_indirect_branch. Il registro
     * delle funzioni e' riempito solo all'avvio, quindi (pc, immagine,
     * overlay) -> funzione non scade mai. fn NULL = vuota (spu_context_init
     * azzera tutto). Chiave: pc | image_id<<32 | resident_ovl<<48. */
#define SPU_IB_CACHE 2048
    struct { uint64_t key; void (*fn)(struct spu_context*); } ib_cache[SPU_IB_CACHE];
} spu_context;

/* Reserved LS addresses (inside the kernel area, below the 0xA00 policy-module
 * base) that the HLE kernel plants as exitToKernelAddr / selectWorkloadAddr in
 * the SpursKernelContext. A policy module branching to them is performing a
 * kernel service; spu_indirect_branch intercepts (policy_mode only). */
#define SPURS_PM_EXIT_TO_KERNEL_LS   0x9C0u
#define SPURS_PM_SELECT_WORKLOAD_LS  0x9D0u

/* ---------------------------------------------------------------------------
 * Initialization
 * -----------------------------------------------------------------------*/
static inline void spu_context_init(spu_context* ctx, uint32_t spu_id)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->spu_id = spu_id;
    ctx->status = SPU_STATUS_STOPPED;
}

/* ---------------------------------------------------------------------------
 * Local store access helpers
 * -----------------------------------------------------------------------*/
static inline uint8_t* spu_ls_ptr(spu_context* ctx, uint32_t lsa)
{
    return &ctx->ls[lsa & SPU_LS_MASK];
}

/* Ordine dei byte della local store (sessione 47). Con SPU_LS_XOR = 3 la LS
 * tiene ogni parola SPU di 4 byte come VALORE dell'host (little-endian): il
 * byte SPU `a` sta all'indice fisico SPU_LSB(a) = a ^ 3, e i quadword si
 * leggono/scrivono con una load/store semplice, senza rev32 (era una
 * istruzione per ogni lqd/stqd del codice liftato: sonda con due rev32 in piu'
 * = +16 ms di CPU per fotogramma). Il costo passa ai trasferimenti DMA e alle
 * copie a blocchi (spu_ls_copy_in/out), molto piu' rari. Con SPU_LS_XOR = 0
 * la LS e' in ordine SPU come prima (A/B: -DSPU_LS_XOR=0). Chi tocca
 * ctx->ls DEVE passare da questi accessori o da SPU_LSB. */
/* SPU_LS_XOR e SPU_LSB: definiti in cima al file */

static inline uint8_t spu_ls_get8(const uint8_t* ls, uint32_t lsa) { return ls[SPU_LSB(lsa & SPU_LS_MASK)]; }
static inline void spu_ls_set8(uint8_t* ls, uint32_t lsa, uint8_t v) { ls[SPU_LSB(lsa & SPU_LS_MASK)] = v; }

/* parola big-endian all'indirizzo SPU lsa (anche non allineato) */
static inline uint32_t spu_ls_get32(const uint8_t* ls, uint32_t lsa)
{
    lsa &= SPU_LS_MASK;
#if SPU_LS_XOR
    if ((lsa & 3) == 0) { uint32_t w; memcpy(&w, ls + lsa, 4); return w; }
#endif
    return ((uint32_t)spu_ls_get8(ls, lsa) << 24) | ((uint32_t)spu_ls_get8(ls, lsa + 1) << 16) |
           ((uint32_t)spu_ls_get8(ls, lsa + 2) << 8) | (uint32_t)spu_ls_get8(ls, lsa + 3);
}
static inline void spu_ls_put32(uint8_t* ls, uint32_t lsa, uint32_t val)
{
    lsa &= SPU_LS_MASK;
#if SPU_LS_XOR
    if ((lsa & 3) == 0) { memcpy(ls + lsa, &val, 4); return; }
#endif
    spu_ls_set8(ls, lsa, (uint8_t)(val >> 24)); spu_ls_set8(ls, lsa + 1, (uint8_t)(val >> 16));
    spu_ls_set8(ls, lsa + 2, (uint8_t)(val >> 8)); spu_ls_set8(ls, lsa + 3, (uint8_t)val);
}

/* Copia fra la LS (indirizzo SPU lsa) e un buffer in ordine SPU/guest
 * (big-endian, come la memoria principale). La conversione e' la stessa nei
 * due versi (inversione dei byte di ogni parola), quindi un solo nucleo. */
static inline void spu__ls_xcopy(uint8_t* ls, uint32_t lsa, uint8_t* buf, uint32_t n, int to_ls)
{
#if SPU_LS_XOR
    uint32_t i = 0;
    while (i < n && ((lsa + i) & 3)) {                       /* testa fino alla parola */
        if (to_ls) ls[SPU_LSB((lsa + i) & SPU_LS_MASK)] = buf[i]; else buf[i] = ls[SPU_LSB((lsa + i) & SPU_LS_MASK)];
        i++; }
#if defined(__aarch64__) && defined(__ARM_NEON)
    {   /* corpo a 64 byte per giro fino al bordo della LS (s.48: il giro da 16
         * costava 8 istruzioni ogni 16 byte, spu_ls_copy_in 4% della CPU) */
        uint32_t o = (lsa + i) & SPU_LS_MASK, k = n - i;
        if (k > SPU_LS_SIZE - o) k = SPU_LS_SIZE - o;
        uint8_t* l = ls + o; uint8_t* b = buf + i; uint8_t* e = l + (k & ~63u);
        for (; l < e; l += 64, b += 64) {
            uint8x16x4_t v = vld1q_u8_x4(to_ls ? b : l);
            v.val[0] = vrev32q_u8(v.val[0]); v.val[1] = vrev32q_u8(v.val[1]);
            v.val[2] = vrev32q_u8(v.val[2]); v.val[3] = vrev32q_u8(v.val[3]);
            vst1q_u8_x4(to_ls ? l : b, v); }
        i += k & ~63u; }
    for (; i + 16 <= n && ((lsa + i) & SPU_LS_MASK) + 16 <= SPU_LS_SIZE; i += 16) {
        uint8_t* l = ls + ((lsa + i) & SPU_LS_MASK);
        if (to_ls) vst1q_u8(l, vrev32q_u8(vld1q_u8(buf + i))); else vst1q_u8(buf + i, vrev32q_u8(vld1q_u8(l))); }
#endif
    for (; i + 4 <= n; i += 4) {
        uint8_t* l = ls + ((lsa + i) & SPU_LS_MASK); uint32_t w;
        if (to_ls) { memcpy(&w, buf + i, 4); w = __builtin_bswap32(w); memcpy(l, &w, 4); }
        else { memcpy(&w, l, 4); w = __builtin_bswap32(w); memcpy(buf + i, &w, 4); } }
    for (; i < n; i++) {                                     /* coda */
        if (to_ls) ls[SPU_LSB((lsa + i) & SPU_LS_MASK)] = buf[i]; else buf[i] = ls[SPU_LSB((lsa + i) & SPU_LS_MASK)]; }
#else
    for (uint32_t i = 0; i < n; ) {                          /* a pezzi, per l'avvolgimento a 256 KB */
        uint32_t o = (lsa + i) & SPU_LS_MASK, k = n - i;
        if (k > SPU_LS_SIZE - o) k = SPU_LS_SIZE - o;
        if (to_ls) memcpy(ls + o, buf + i, k); else memcpy(buf + i, ls + o, k);
        i += k; }
#endif
}
static inline void spu_ls_copy_in(uint8_t* ls, uint32_t lsa, const void* src, uint32_t n) { spu__ls_xcopy(ls, lsa, (uint8_t*)src, n, 1); }
static inline void spu_ls_copy_out(const uint8_t* ls, uint32_t lsa, void* dst, uint32_t n) { spu__ls_xcopy((uint8_t*)ls, lsa, (uint8_t*)dst, n, 0); }
static inline void spu_ls_zero(uint8_t* ls, uint32_t lsa, uint32_t n)
{
    for (uint32_t i = 0; i < n; ) {                          /* i byte di una parola intera restano nella parola */
        uint32_t o = (lsa + i) & SPU_LS_MASK;
        if ((o & 3) == 0 && n - i >= 4) { uint32_t k = (n - i) & ~3u; if (k > SPU_LS_SIZE - o) k = SPU_LS_SIZE - o; memset(ls + o, 0, k); i += k; }
        else { ls[SPU_LSB(o)] = 0; i++; } }
}

/* Vista in ordine SPU (big-endian) di n byte della LS a partire da lsa, per
 * le sonde e i dump che leggono i byte con un puntatore: con SPU_LS_XOR e' una
 * copia in un buffer del thread (due, alternati: al massimo due viste vive
 * insieme), senza e' la LS stessa. In sola lettura. */
static inline const uint8_t* spu_ls_be(const uint8_t* ls, uint32_t lsa, uint32_t n)
{
#if SPU_LS_XOR
    static __thread uint8_t* b[2]; static __thread int k;
    k ^= 1;
    if (!b[k]) b[k] = (uint8_t*)malloc(SPU_LS_SIZE + 16);
    if (n > SPU_LS_SIZE) n = SPU_LS_SIZE;
    spu_ls_copy_out(ls, lsa, b[k], n);
    return b[k];
#else
    (void)n; return ls + (lsa & SPU_LS_MASK);
#endif
}
static inline uint32_t spu_ls_read32(const spu_context* ctx, uint32_t lsa) { return spu_ls_get32(ctx->ls, lsa); }
static inline void spu_ls_write32(spu_context* ctx, uint32_t lsa, uint32_t val) { spu_ls_put32(ctx->ls, lsa, val); }

/* Local-store quadword access.
 *
 * LS bytes are big-endian (the SPU's native order); registers in our
 * model are stored in native (little-endian on x86) order so that
 * `_u32[i]` directly gives lane i's value and `spu_preferred_u32(r)`
 * == `r._u32[0]`. The byte-swap below converts between the two on
 * every quadword load/store, mirroring what `spu_ls_read32` already
 * does explicitly. Doing the swap here (rather than in every channel
 * extractor) keeps the per-word `_u32[i]` semantics that the lifter
 * helpers in `spu_helpers.h` were written against. */
/* Modo in virgola mobile dell'host per il codice SPU liftato (sessione 36).
 * La SPU non ha denormali (valgono zero) e arrotonda SOLO verso lo zero;
 * l'host di default tiene i denormali e arrotonda al piu' vicino. Con FZ e
 * RMode=RZ nell'FPCR (ARM64: FZ bit 24, RMode bit 23:22 = 11; x86: FTZ|DAZ e
 * RC = 11 nell'MXCSR) le istruzioni del ferro danno il risultato della SPU
 * senza controlli sugli ingressi (vedi SPU__FLOAT3_NEON in spu_helpers.h) e
 * i risultati coincidono col troncamento del ferro vero. Si imposta
 * all'ingresso di ogni corsa (spu_run_with_halt) e si ripristina all'uscita:
 * i thread PPU non devono vederlo. SPU_FP_HOST=1 lascia il modo dell'host
 * (A/B; con il modo dell'host i denormali in ingresso non vengono azzerati). */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif
static inline uint64_t spu_host_fp_enter(void)
{
    static int s_off = -1;
    if (s_off < 0) s_off = getenv("SPU_FP_HOST") ? 1 : 0;
    if (s_off) return ~0ull;
#if defined(__aarch64__)
    uint64_t old; __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    uint64_t nv = old | (1ull << 24) | (3ull << 22);
    if (nv != old) __asm__ volatile("msr fpcr, %0" :: "r"(nv));
    return old;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    unsigned old = _mm_getcsr();
    _mm_setcsr(old | 0x8040u | 0x6000u);
    return old;
#else
    return ~0ull;
#endif
}
static inline void spu_host_fp_leave(uint64_t old)
{
    if (old == ~0ull) return;
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" :: "r"(old));
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_setcsr((unsigned)old);
#endif
}

/* The 128-bit LS accessors are THE hot primitive of lifted SPU execution
 * (every load/store in a Bink movie frame's ~millions of lifted instructions
 * runs through here). On x86 the whole BE<->LE quadword swizzle is one
 * SSSE3 byte-shuffle instead of 16 scalar byte ops -- measured against the
 * scalar path with the same per-lane _u32 semantics. */
#if defined(_MSC_VER)
#include <stdlib.h>
#define SPU_BSWAP32(x) _byteswap_ulong(x)
#define SPU_LS_FAST 1
#elif defined(__GNUC__) || defined(__clang__)
#define SPU_BSWAP32(x) __builtin_bswap32(x)
#define SPU_LS_FAST 1
#endif

/* Percorso veloce degli accessi alla local store (sessione 28). Le due
 * funzioni *_diag qui sotto portano una decina di sonde ciascuna (statiche,
 * confronti, probe WWS) e con ~100 milioni di accessi al secondo per lane
 * erano il 37% del tempo delle lane SPU di Uncharted (Instruments). Se
 * nessuna variabile di diagnostica della LS e' impostata si passa dal
 * percorso corto: solo load/store + inversione dei byte di ogni parola. */
extern int g_spu_ls_diag;   /* spu_channels.c: -1 da decidere, 0 veloce, 1 con le sonde */
int spu_ls_diag_init(void);  /* spu_channels.c: legge le variabili d'ambiente */
static inline u128 spu_ls_read128_diag(const spu_context* ctx, uint32_t lsa);
static inline void spu_ls_write128_diag(spu_context* ctx, uint32_t lsa, u128 val);
/* SPU_LS_NODIAG (s.36): compila via il ramo delle sonde. Il solo controllo
 * `g_spu_ls_diag` costa: la chiamata (non in linea) sul ramo lento fa
 * scaricare al compilatore OGNI registro guest tenuto nei registri host
 * prima di ogni accesso alla LS e rileggerlo dopo -- il codice liftato
 * diventa una sequenza di load/store. Con la macro le variabili SPU_LS_*
 * (watch, LOWREAD, ...) non funzionano: si ricompila senza. */
static inline u128 spu_ls_read128(const spu_context* ctx, uint32_t lsa)
{
#ifndef SPU_LS_NODIAG
    if (__builtin_expect(g_spu_ls_diag != 0, 0)) {
        if (g_spu_ls_diag < 0) g_spu_ls_diag = spu_ls_diag_init();
        if (g_spu_ls_diag) return spu_ls_read128_diag(ctx, lsa);
    }
#endif
    const uint8_t* p = &ctx->ls[lsa & (SPU_LS_MASK & ~0xFu)];
    u128 v;
#if defined(__aarch64__) && defined(__ARM_NEON)
#if SPU_LS_XOR
    vst1q_u8(v._u8, vld1q_u8(p));
#else
    vst1q_u8(v._u8, vrev32q_u8(vld1q_u8(p)));
#endif
#else
    uint32_t w0, w1, w2, w3;
    memcpy(&w0, p, 4); memcpy(&w1, p + 4, 4); memcpy(&w2, p + 8, 4); memcpy(&w3, p + 12, 4);
    v._u32[0] = SPU_BSWAP32(w0); v._u32[1] = SPU_BSWAP32(w1); v._u32[2] = SPU_BSWAP32(w2); v._u32[3] = SPU_BSWAP32(w3);
#endif
    return v;
}
static inline void spu_ls_write128(spu_context* ctx, uint32_t lsa, u128 val)
{
#ifndef SPU_LS_NODIAG
    if (__builtin_expect(g_spu_ls_diag != 0, 0)) {
        if (g_spu_ls_diag < 0) g_spu_ls_diag = spu_ls_diag_init();
        if (g_spu_ls_diag) { spu_ls_write128_diag(ctx, lsa, val); return; }
    }
#endif
    uint8_t* p = &ctx->ls[lsa & (SPU_LS_MASK & ~0xFu)];
#if defined(__aarch64__) && defined(__ARM_NEON)
#if SPU_LS_XOR
    vst1q_u8(p, vld1q_u8(val._u8));
#else
    vst1q_u8(p, vrev32q_u8(vld1q_u8(val._u8)));
#endif
#else
    uint32_t w0 = SPU_BSWAP32(val._u32[0]), w1 = SPU_BSWAP32(val._u32[1]), w2 = SPU_BSWAP32(val._u32[2]), w3 = SPU_BSWAP32(val._u32[3]);
    memcpy(p, &w0, 4); memcpy(p + 4, &w1, 4); memcpy(p + 8, &w2, 4); memcpy(p + 12, &w3, 4);
#endif
}

static inline u128 spu_ls_read128_diag(const spu_context* ctx, uint32_t lsa)
{
    u128 v;
    /* SPU_LS_LOWREAD=1: a job that reads its OWN first bytes as data is
     * dereferencing a null base -- the job binary loads at LS 0, so [NULL+off]
     * returns its own instruction words. Report each distinct low address once,
     * with the pc, to find which pointer was never filled in. */
    { static int s_lw = -1;
      if (s_lw < 0) s_lw = getenv("SPU_LS_LOWREAD") ? 1 : 0;
      if (s_lw && lsa < 0x200u && ctx->image_id > 0 && !ctx->policy_mode) {
          static uint32_t seen[24]; static int n = 0; int known = 0;
          uint32_t key = (uint32_t)ctx->image_id << 24 | (lsa & 0x1F0);
          for (int i = 0; i < n; i++) if (seen[i] == key) { known = 1; break; }
          if (!known && n < 24) {
              seen[n++] = key;
              fprintf(stderr, "[spu-lowread] img=%d read LS 0x%03X at pc=0x%05X "
                      "ctx=%p r1=0x%05X r2=0x%08X r3=0x%08X r4=0x%08X\n",
                      ctx->image_id, lsa, (uint32_t)ctx->pc & SPU_LS_MASK,
                      (const void*)ctx,                      ctx->gpr[1]._u32[0], ctx->gpr[2]._u32[0],
                      ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0]);
              fflush(stderr);
          }
      } }
    /* WWS buffer-resolution probe: GetLogicalBuffer reads bufferSetArray at
     * 0xDF0 + (jobNum<<6) + (logBufSet<<2), so the raw address encodes both.
     * Log them (capped) to see which set each command -- especially RunJob --
     * resolves to. Uses the RAW lsa before 16-byte alignment masking. */
    /* Traccia dei comandi di caricamento: l'interprete (LS 0x1BB0) sceglie il
     * gestore con `lqd $r6, 0x1590($r17)`, dove r17 = indice*4. L'indirizzo
     * grezzo di quella lettura E' l'indice del comando, ed e' l'unico punto in
     * cui si vede quali comandi il job manager esegue davvero e in che ordine.
     * I gestori nella tabella (nell'immagine di Uncharted): 0=0x2418 fine,
     * 1=0x1C20, 2=0x1D20 caricamento, 3=0x22B0, 4=0x2368, 5=0x23BC RunJob,
     * 6=0x2418, 7=0x1604. */
    if (ctx->image_id == 2 && ctx->policy_mode && lsa >= 0x1590 && lsa < 0x15B0) {
        static int s_cmd = -1;
        if (s_cmd < 0) s_cmd = getenv("SPU_WWS_CMDS") ? 1 : 0;
        if (s_cmd) {
            static int n = 0;
            if (n++ < 400) {
                static const uint16_t H[8] = { 0x2418, 0x1C20, 0x1D20, 0x22B0,
                                               0x2368, 0x23BC, 0x2418, 0x1604 };
                unsigned idx = ((lsa - 0x1590) >> 2) & 7;
                fprintf(stderr, "[wws-cmd] #%d idx=%u -> handler 0x%04X\n",
                        n, idx, H[idx]);
            }
        }
    }
    if (ctx->image_id == 2 && ctx->policy_mode && lsa >= 0xDF0 && lsa < 0xEB0) {
        extern int g_wws_read_probe;
        if (g_wws_read_probe < 48) {
            g_wws_read_probe++;
            uint32_t off = lsa - 0xDF0;
            fprintf(stderr, "[wws-res] bufferSetArray read jobNum=%u logBufSet=%u off=0x%X\n",
                    off >> 6, (off >> 2) & 0xF, off);
        }
    }
    lsa &= SPU_LS_MASK & ~0xFu;
    const uint8_t* p = &ctx->ls[lsa];
    spu_ls_watch_hit2(lsa, 0, p, (uint32_t)ctx->pc & SPU_LS_MASK,
                      ctx->gpr[0]._u32[0] & SPU_LS_MASK);
#if SPU_LS_XOR
    memcpy(v._u32, p, 16);
#elif SPU_LS_FAST
    uint32_t w0, w1, w2, w3;
    memcpy(&w0, p,      4); memcpy(&w1, p + 4,  4);
    memcpy(&w2, p + 8,  4); memcpy(&w3, p + 12, 4);
    v._u32[0] = SPU_BSWAP32(w0); v._u32[1] = SPU_BSWAP32(w1);
    v._u32[2] = SPU_BSWAP32(w2); v._u32[3] = SPU_BSWAP32(w3);
#else
    for (int i = 0; i < 4; i++) {
        v._u32[i] = ((uint32_t)p[i*4]     << 24) |
                    ((uint32_t)p[i*4 + 1] << 16) |
                    ((uint32_t)p[i*4 + 2] <<  8) |
                    (uint32_t)p[i*4 + 3];
    }
#endif
    return v;
}

extern int g_spu_ls0_watch;   /* SPU_LS0_WATCH=1 (spu_channels.c): chi scrive LS 0 */
static inline void spu_ls_write128_diag(spu_context* ctx, uint32_t lsa, u128 val)
{
    lsa &= SPU_LS_MASK & ~0xFu;
    uint8_t* p = &ctx->ls[lsa];
    /* SPU_LS0_WATCH=1: i primi 16 store a LS 0 (il vettore di interrupt del
     * PM). Uncharted, livello dive-boat: LS0 diventava `fsmbi` e l'interrupt
     * di stall-and-notify non veniva piu' preso. */
    /* SPU_LS0_WATCH=1 guarda LS 0; SPU_LS_WRITE_WATCH=<hex> un altro indirizzo
     * (allineato a 16). g_spu_ls0_watch: -1 = da leggere, 0 = spento, altrimenti
     * l'indirizzo+1. */
    if (g_spu_ls0_watch < 0) { const char* e = getenv("SPU_LS_WRITE_WATCH");
        g_spu_ls0_watch = e ? (int)(strtoul(e, 0, 16) & ~0xFu) + 1 : (getenv("SPU_LS0_WATCH") ? 1 : 0); }
    if (g_spu_ls0_watch && lsa == (uint32_t)(g_spu_ls0_watch - 1)) {
        extern int g_hle_trace_armed;   /* con PS3_TTY_BT parte dall'hit */
        static int s_gate = -1; if (s_gate < 0) s_gate = getenv("PS3_TTY_BT") ? 1 : 0;
        static int s_wovl = -2; if (s_wovl == -2) { const char* e = getenv("SPU_LS_WRITE_WATCH_OVL"); s_wovl = e ? atoi(e) : -1; }
        static int _n = 0; static int s_wmax = -1; if (s_wmax < 0) { const char* e = getenv("SPU_LS_WRITE_WATCH_MAX"); s_wmax = e ? atoi(e) : 16; }
        if ((!s_gate || g_hle_trace_armed) && (s_wovl < 0 || ctx->resident_ovl == s_wovl) && _n++ < s_wmax)
        fprintf(stderr, "[ls-watch] c%04X img=%d ovl=%d pc=0x%05X lr=0x%05X stq LS 0x%05X = %08X %08X %08X %08X\n",
                (unsigned)(((uintptr_t)ctx >> 12) & 0xFFFF), ctx->image_id, ctx->resident_ovl,
                (uint32_t)ctx->pc & SPU_LS_MASK, ctx->gpr[0]._u32[0] & SPU_LS_MASK, lsa,
                val._u32[0], val._u32[1], val._u32[2], val._u32[3]); }
    /* WWS code-buffer probe: LBP's ChangeLoadToRunJob dispatches to LS[0x1320]
     * (= lsaJobCodeBuffer in LBP's layout) + entryOffset. When TecRunJob writes
     * that code-buffer LS address, dump the RunJob decision inputs so we can see
     * why it resolves to an empty buffer: the resolved address, the whole
     * bufferSetArray (0xDF0), and the live loadCommands (0xC00) whose RunJob
     * command (commandNum==5) names the code buffer set. */
    if (lsa == 0x1320 && ctx->image_id == 2 && ctx->policy_mode) {
        extern int g_wws_code_probe;   /* defined in spu_channels.c, capped */
        if (g_wws_code_probe < 6) {
            g_wws_code_probe++;
            uint32_t codeLsa = val._u32[0];
            #define _RD32(o) spu_ls_get32(ctx->ls, (o))
            uint32_t loadJob = _RD32(0x12D0), nextLoad = _RD32(0x12C0), runJob = _RD32(0x12E0);
            /* GetLogicalBuffer keys the bufferSet on jobNum(=loadJobNum): row at
             * 0xDF0 + jobNum*64. Dump THAT row (and job0's) so we see which set
             * the code resolves from. codeLsa>>10 = the resolved page. */
            fprintf(stderr, "[wws-code] codeLS=0x%05X (page 0x%X) loadJobNum=0x%X nextLoad=0x%X runJobNum=0x%X\n",
                    codeLsa & 0x3FFFF, (codeLsa>>10)&0xFF, loadJob, nextLoad, runJob);
            uint32_t row = (loadJob <= 3) ? (0xDF0 + loadJob*64) : 0xDF0;
            uint8_t bs[32]; spu_ls_copy_out(ctx->ls, row, bs, 32);
            fprintf(stderr, "  bufferSetArray[job=0x%X @0x%X]:", loadJob, row);
            for (int i = 0; i < 32; i += 4)
                fprintf(stderr, " %02X%02X%02X%02X", bs[i],bs[i+1],bs[i+2],bs[i+3]);
            uint8_t lc[0x80]; spu_ls_copy_out(ctx->ls, 0xC00, lc, 0x80);
            fprintf(stderr, "\n  loadCommands[0xC00]:");
            for (int i = 0; i < 0x80; i += 4)
                fprintf(stderr, " %02X%02X%02X%02X", lc[i],lc[i+1],lc[i+2],lc[i+3]);
            fprintf(stderr, "\n");
            #undef _RD32
        }
    }
#if SPU_LS_XOR
    memcpy(p, val._u32, 16);
#elif SPU_LS_FAST
    uint32_t w0 = SPU_BSWAP32(val._u32[0]), w1 = SPU_BSWAP32(val._u32[1]);
    uint32_t w2 = SPU_BSWAP32(val._u32[2]), w3 = SPU_BSWAP32(val._u32[3]);
    memcpy(p,      &w0, 4); memcpy(p + 4,  &w1, 4);
    memcpy(p + 8,  &w2, 4); memcpy(p + 12, &w3, 4);
#else
    for (int i = 0; i < 4; i++) {
        uint32_t w = val._u32[i];
        p[i*4]     = (uint8_t)(w >> 24);
        p[i*4 + 1] = (uint8_t)(w >> 16);
        p[i*4 + 2] = (uint8_t)(w >>  8);
        p[i*4 + 3] = (uint8_t)w;
    }
#endif
    /* SPU_WWS_STATE=1: le sei parole di stato del job manager di WwsJob, solo
     * quando cambiano. Sono l'unico modo di vedere se la pipeline avanza da
     * Load a Run a Store: una watch generica su quegli indirizzi stampa anche
     * ogni LETTURA, e il loop di scheduling le rilegge a ogni giro -- centinaia
     * di MB di log per sei valori che cambiano una manciata di volte. */
    if (ctx->image_id == 2 && ctx->policy_mode) {
        static int s_ws = -1;
        if (s_ws < 0) { const char* e = getenv("SPU_WWS_STATE"); s_ws = e ? 1 : 0; }
        if (s_ws) {
            const char* nm = 0; int idx = -1;
            switch (lsa) {
            case 0x12A0: nm = "loadState"; idx = 0; break;
            case 0x12C0: nm = "nextLoad";  idx = 1; break;
            case 0x12D0: nm = "loadJob";   idx = 2; break;
            case 0x12E0: nm = "runJob";    idx = 3; break;
            case 0x12F0: nm = "storeJob";  idx = 4; break;
            case 0x1300: nm = "lastStore"; idx = 5; break;
            default: break;
            }
            if (nm) {
                static uint32_t last[6] = { 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
                                            0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu };
                uint32_t nv = val._u32[0];
                if (nv != last[idx]) {
                    last[idx] = nv;
                    static int n = 0;
                    if (n++ < 200)
                        fprintf(stderr, "[wws-state] %s = 0x%08X\n", nm, nv);
                }
            }
        }
    }
    spu_ls_watch_hit2(lsa, 1, p, (uint32_t)ctx->pc & SPU_LS_MASK,
                      ctx->gpr[0]._u32[0] & SPU_LS_MASK);
    /* SPU_SMC_WATCH=<img>: self-modification detector. Log any store whose
     * target LS line falls inside that image's CODE segment (the segment
     * bounds come from SPU_SMC_LO/HI, default the pm_wwsjob range 0xA00..
     * 0x3700). A hit proves the guest rewrites its own instructions -- which
     * a static recompiler cannot follow. */
    { static int s = -2; static uint32_t lo, hi, img;
      if (s == -2) { const char* e = getenv("SPU_SMC_WATCH");
        s = e ? atoi(e) : -1; img = (uint32_t)s;
        const char* l = getenv("SPU_SMC_LO"); lo = l ? (uint32_t)strtoul(l,0,0) : 0xA00;
        const char* h = getenv("SPU_SMC_HI"); hi = h ? (uint32_t)strtoul(h,0,0) : 0x3700; }
      if (s >= 0 && (uint32_t)ctx->image_id == img && lsa >= lo && lsa < hi) {
          static int _n = 0;
          if (_n++ < 48)
              fprintf(stderr, "[spu-SMC] img=%d WROTE CODE @0x%05X (pc=0x%05X) = %02X%02X%02X%02X\n",
                      ctx->image_id, lsa, (uint32_t)ctx->pc & SPU_LS_MASK,
                      p[0], p[1], p[2], p[3]);
      } }
}

/* ---------------------------------------------------------------------------
 * Preferred slot extraction
 *
 * SPU instructions operate on the "preferred slot" of a 128-bit register,
 * which is the leftmost (highest-address in big-endian) element.
 * For word operations, preferred slot = element 0 of the _u32 array
 * (since our u128 stores big-endian element order).
 * -----------------------------------------------------------------------*/
static inline uint32_t spu_preferred_u32(const u128* reg)
{
    return reg->_u32[0];
}

static inline int32_t spu_preferred_s32(const u128* reg)
{
    return reg->_s32[0];
}

static inline float spu_preferred_f32(const u128* reg)
{
    return reg->_f32[0];
}

static inline uint64_t spu_preferred_u64(const u128* reg)
{
    return reg->_u64[0];
}

/* Create a register with a value splatted to the preferred word slot */
static inline u128 spu_make_preferred_u32(uint32_t val)
{
    u128 r;
    memset(&r, 0, sizeof(r));
    r._u32[0] = val;
    return r;
}

/* ---------------------------------------------------------------------------
 * Channel read/write helpers
 * -----------------------------------------------------------------------*/
static inline void spu_channel_write(spu_channel* ch, uint32_t val)
{
    ch->value = val;
    ch->count = 1;
}

static inline uint32_t spu_channel_read(spu_channel* ch)
{
    uint32_t val = ch->value;
    ch->count = 0;
    return val;
}

static inline int spu_channel_has_data(const spu_channel* ch)
{
    return ch->count > 0;
}

/* ---------------------------------------------------------------------------
 * SPU_DRAIN trampoline execution model (faithful-adopt, from canersaka's fork).
 *
 * Replaces the musttail SPU_TAILCALL chain: a cross-function transfer sets
 * ctx->pc + g_spu_trampoline_fn and RETURNS (unwinds the host stack); the
 * enclosing SPU_DRAIN loop re-enters. This keeps the host stack bounded and
 * gives ONE central per-transfer hook (lockstep tick, later flight recorder).
 * The lifter (tools/spu_lifter.py) emits: calls as a bracketed nested host call
 * + SPU_DRAIN; tail/indirect branches as trampoline-set + return; `bi $r0` as
 * SPU_RET. Hooks below are STUBS for now (spu_drain.c) -- lockstep/fltrec land
 * in later milestones.
 * -----------------------------------------------------------------------*/
#if defined(_MSC_VER)
#  define SPU_THREAD_LOCAL __declspec(thread)
#else
#  define SPU_THREAD_LOCAL __thread
#endif

/* Indirect-branch dispatcher (spu_channels.c): resolves ctx->pc to a lifted
 * function in the active image and runs it. Referenced by SPU_RET/SPU_DRAIN. */
void spu_indirect_branch(spu_context* ctx);

/* Pending cross-function transfer target; NULL when none. Thread-local: each
 * spu_context is pinned to one host thread for its lifetime. */
extern SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);

/* Central per-transfer hooks (stubbed in spu_drain.c until their milestones). */
void yz_lockstep_tick(spu_context* ctx);             /* round-robin token gate */
void spu_task_launch_check(spu_context* ctx, void* fn); /* SPURS task-launch    */
/* Restore image_id after a call-bracket (adopt-on-serve); re-applies the
 * persistent LS-0xA00 workload module if one is set. */
void spu_img_restore(spu_context* ctx, int32_t saved_img);
/* Wake a host thread blocked in a channel wait (channel-stall milestone). */
void spu_ch_wake(spu_context* ctx);
/* Runaway lifted-call recursion guard. A lifted brsl/bisl nests a real host
 * call frame, so an SPU loop the lifter lowered as a CALL rather than a
 * transfer recurses until the host stack dies -- surfacing as a bare
 * STACKOVERFLOW backtrace of unsymbolised RVAs that says nothing about which
 * SPU code is looping. Report the SPU pc/image once and halt instead.
 * SPU_HOST_DEPTH_MAX overrides the limit. */
void spu_depth_guard(spu_context* ctx);
int  spu_tailret_enabled(void);
int  spu_retlink_enabled(void);
/* Take a pending SPU interrupt (spu_drain.c): srr0 <- ctx->pc, int_enable <- 0,
 * decode the guest-planted branch at LS 0 into ctx->pc, and return the
 * dispatcher to run instead of the interrupted transfer. Returns `tf`
 * unchanged (interrupt not taken) if LS 0 holds no branch instruction. */
void (*spu_take_interrupt(spu_context* ctx,
                          void (*tf)(spu_context*)))(spu_context*);

/* Drain the pending trampoline chain: run each queued transfer target until
 * none remain. The one central hook site for the faithful execution model. */
/* SPU_PCWATCH=<pc esadecimale>: a ogni passaggio da quel blocco stampa la
 * parola a stack+0xC0. Serve a leggere un bivio del job module dall'esterno,
 * senza toccare il codice generato.
 *
 * LIMITE MISURATO (09/09/2026): vede solo i blocchi raggiunti dal TRAMPOLINO.
 * Nel codice liftato una chiamata diventa una chiamata C diretta
 * (`func(ctx); SPU_DRAIN(ctx);`), e la funzione chiamata non passa mai di qui:
 * provato su 0x6120, che di sicuro esegue (il suo DMA si vede nel log), e i
 * passaggi contati sono zero. Per quei blocchi serve un altro aggancio. */
static inline void spu_pcwatch(spu_context* ctx)
{
    /* Via veloce (s.33): un solo flag per tutte le diagnostiche del salto.
     * Questo hook gira a OGNI salto del trampolino (milioni al secondo) e i
     * suoi 5 controlli lazy valevano il 5% del tempo delle lane. */
    static int s_any = -1;
    if (s_any < 0) s_any = (getenv("SPU_PROBE_PC") || getenv("SPU_PCWATCH") || getenv("SPU_HOPHIST")) ? 1 : 0;
    if (__builtin_expect(!s_any, 1)) return;
    /* SPU_PROBE_PC=<hex LS>[,<img>] + SPU_PROBE_REGS=<r,r,...> (s.32): ai primi
     * SPU_PROBE_N (20) salti del trampolino a quel pc stampa i registri
     * elencati (4 parole, hex e float): gli ingressi di un kernel liftato
     * (es. lo skinning di Uncharted, func_D5AC del job 215) senza toccare il
     * codice generato. Stesso limite di SPU_PCWATCH: solo i blocchi raggiunti
     * dal trampolino. */
    { static long s_ppc = -2, s_pimg = -1; static int s_pregs[16], s_npr = 0, s_pn = 20;
      if (s_ppc == -2) { const char* e = getenv("SPU_PROBE_PC"); s_ppc = -1;
          if (e) { char* c = 0; s_ppc = strtol(e, &c, 16); if (c && *c == ',') s_pimg = strtol(c + 1, 0, 0);
              const char* r = getenv("SPU_PROBE_REGS"); while (r && *r && s_npr < 16) { s_pregs[s_npr++] = (int)strtol(r, (char**)&r, 10); if (*r == ',') r++; else break; }
              const char* n = getenv("SPU_PROBE_N"); if (n) s_pn = atoi(n); } }
      if (s_ppc >= 0 && ((uint32_t)ctx->pc & SPU_LS_MASK) == (uint32_t)s_ppc && (s_pimg < 0 || ctx->image_id == s_pimg)) {
          static int _n = 0; if (_n++ < s_pn) {
              fprintf(stderr, "[probe] pc=%05X img=%d ovl=%d hit %d\n", (uint32_t)ctx->pc & SPU_LS_MASK, ctx->image_id, ctx->resident_ovl, _n);
              for (int k = 0; k < s_npr; k++) { const u128* g = &ctx->gpr[s_pregs[k]];
                  fprintf(stderr, "[probe]   r%-3d %08X %08X %08X %08X  = %g %g %g %g\n", s_pregs[k], g->_u32[0], g->_u32[1], g->_u32[2], g->_u32[3], g->_f32[0], g->_f32[1], g->_f32[2], g->_f32[3]); } } } }
    /* SPU_HOPHIST=<ovl>: istogramma dei salti del trampolino per pc nel modulo
     * di job residente <ovl>, stampato ogni 4M salti (i 40 pc piu' frequenti) */
    { static long s_hh = -2; if (s_hh == -2) { const char* e = getenv("SPU_HOPHIST"); s_hh = e ? atol(e) : -1; }
      if (s_hh >= 0 && ctx->resident_ovl == (int)s_hh) {
          static uint32_t* cnt = 0; static unsigned long long tot = 0;
          if (!cnt) cnt = (uint32_t*)calloc(65536, sizeof(uint32_t));
          cnt[((uint32_t)ctx->pc & SPU_LS_MASK) >> 2]++;
          if ((++tot % 4000000ull) == 0) {
              fprintf(stderr, "[hophist] ovl %ld, %llu salti:", s_hh, tot);
              for (int k = 0; k < 40; k++) { uint32_t best = 0, bi = 0; for (uint32_t i = 0; i < 65536; i++) if (cnt[i] > best) { best = cnt[i]; bi = i; }
                  if (!best) break; fprintf(stderr, " %05X:%u", bi << 2, best); cnt[bi] = 0; }
              fputc('\n', stderr); } } }
    static long w = -1;
    if (w < 0) { const char* e = getenv("SPU_PCWATCH"); w = e ? (long)strtoul(e, 0, 16) : 0; }
    if (!w) return;
    if (((uint32_t)ctx->pc & SPU_LS_MASK) != (uint32_t)w) return;
    static unsigned n = 0;
    if (n++ >= 4000) return;
    uint32_t sp = ctx->gpr[1]._u32[0] & SPU_LS_MASK;
    uint32_t at = (sp + 0xC0) & SPU_LS_MASK;
    uint32_t v  = spu_ls_get32(ctx->ls, at);
    fprintf(stderr, "[spu-pc] img=%d pc=0x%05X sp=0x%05X [sp+0xC0]=0x%08X\n",
            ctx->image_id, (uint32_t)w, sp, v);
}

/* Via veloce del drain (s.42). Il ciclo completo qui sotto chiama sei funzioni
 * fuori linea a OGNI salto del trampolino (sonde, lockstep, guardia di
 * profondita'), e i salti sono 25-60 milioni al secondo: `sample` le dava al 7%
 * del tempo delle lane. Senza variabili diagnostiche (spu_drain_mode_init in
 * spu_drain.c) il ciclo gira in spu_drain_fast, con gli stessi controlli di
 * semantica e nessuna sonda. SPU_DRAIN_FULL=1 torna al ciclo completo. */
extern int g_spu_drain_mode;              /* 0 = da decidere, 1 = veloce, 2 = completo */
void spu_drain_slow(spu_context* ctx);
void spu_drain_fast(spu_context* ctx);
#define SPU_DRAIN(ctx) do {                                                  \
        if (__builtin_expect(g_spu_drain_mode != 1, 0)) { spu_drain_slow(ctx); break; } \
        if (__builtin_expect((ctx)->host_depth >= 2000u, 0)) spu_depth_guard(ctx); \
        if (g_spu_trampoline_fn) spu_drain_fast(ctx);                        \
    } while (0)

/* Trasferimenti in linea (s.48, lifter --inline-xfer). Un salto fra funzioni
 * liftate passava SEMPRE dal ciclo del drain (impostare il trampolino, tornare,
 * rileggerlo, controllare, chiamare): ~95 M salti ogni 50 M diretti nel
 * livello, spu_drain_fast + spu_indirect_branch ~11% della CPU attiva. Qui il
 * codice liftato fa gli stessi controlli del drain e salta in coda (musttail)
 * al bersaglio; se uno qualunque scatta (interrupt pendente, ritorno esatto
 * SPU_RETLINK, traccia del flow del PM, bersaglio speciale, cache mancata) si
 * ricade nel trampolino e il drain fa esattamente cio' che faceva prima.
 * g_spu_xfer_inline lo decide il drain al primo giro (0 con il drain completo
 * o con SPU_XFER_INLINE=0: A/B con lo stesso binario); g_spu_ib_inline lo
 * decide spu_indirect_branch (0 con le sue sonde o SPU_IB_NOCACHE). */
extern int g_spu_xfer_inline, g_spu_ib_inline;
/* Casella di ib_cache: hash moltiplicativo di tutta la chiave (s.48). Con
 * (pc>>2)&255 i bersagli caldi del job 215 a 0x11298/0x11A98/0x12298/0x12A98
 * cadevano nella stessa casella e si scacciavano a vicenda. */
static inline uint32_t spu_ib_slot(uint64_t key)
{
    return (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 48) & (SPU_IB_CACHE - 1);
}
extern void* volatile g_pm_flow_ctx;
static inline int spu_xfer_ok(spu_context* ctx)
{
    return g_spu_xfer_inline &&
           !(ctx->int_enable && (ctx->event_status & ctx->event_mask)) &&
           !(ctx->host_depth > 0 && ctx->ret_link &&
             ((uint32_t)ctx->pc & SPU_LS_MASK) == ctx->ret_link) &&
           g_pm_flow_ctx != (void*)ctx;
}
/* Bersaglio di un salto indiretto se e' gia' nella cache del contesto e non e'
 * nessuno degli indirizzi che spu_indirect_branch tratta a parte (ritorno del
 * job, servizi del kernel SPURS, syscall del taskset, shim in cima alla LS,
 * LS 0, restauro dei registri d'interrupt, immagine 23). */
static inline void (*spu_ib_inline(spu_context* ctx))(spu_context*)
{
    if (!g_spu_ib_inline || !spu_xfer_ok(ctx)) return 0;
    uint32_t pc = (uint32_t)ctx->pc & SPU_LS_MASK & ~3u;
    if (pc == 0 || pc >= 0x3FEC0u || pc == SPURS_PM_EXIT_TO_KERNEL_LS ||
        pc == SPURS_PM_SELECT_WORKLOAD_LS || pc == 0xA70u ||
        ctx->irq_save_valid || ctx->image_id == 23) return 0;
    const uint64_t key = pc | ((uint64_t)(uint16_t)ctx->image_id << 32)
                            | ((uint64_t)(uint16_t)ctx->resident_ovl << 48);
    const __typeof__(ctx->ib_cache[0])* c = &ctx->ib_cache[spu_ib_slot(key)];
    if (c->key != key) return 0;
    ctx->pc = pc;
    return c->fn;
}
/* SPU_TAILCALL e' definita nel preambolo del codice liftato. */
#define SPU_XJUMP(ctx, tgt, fn) do { (ctx)->pc = (tgt);                     \
        if (__builtin_expect(spu_xfer_ok(ctx), 1)) {                          \
            (ctx)->hops++; (ctx)->steps++; SPU_TAILCALL(fn(ctx)); }           \
        g_spu_trampoline_fn = fn; return; } while (0)
/* chiamata indiretta (bisl): la stessa cache, poi il dispatcher completo */
#define SPU_XCALLI(ctx) do { void (*_xf)(spu_context*) = spu_ib_inline(ctx);  \
        if (__builtin_expect(_xf != 0, 1)) _xf(ctx); else spu_indirect_branch(ctx); } while (0)
#define SPU_XIND(ctx, target) do { (ctx)->pc = (target);                    \
        void (*_xf)(spu_context*) = spu_ib_inline(ctx);                       \
        if (__builtin_expect(_xf != 0, 1)) {                                  \
            (ctx)->hops++; (ctx)->steps++; SPU_TAILCALL(_xf(ctx)); }          \
        g_spu_trampoline_fn = spu_indirect_branch; return; } while (0)

#define SPU_DRAIN_FULL(ctx) do {                               \
        spu_depth_guard(ctx);                                  \
        while (g_spu_trampoline_fn) {                           \
            (ctx)->hops++;                                                    \
            /* L'interrupt si prende PRIMA delle scorciatoie di ritorno.      \
             * Sull'hardware un interrupt abilitato e pendente entra fra due  \
             * istruzioni qualunque, ritorno compreso; qui invece le due      \
             * scorciatoie interrompevano il drain e saltavano il controllo.  \
             * Nel policy module di WwsJob quella e' la differenza fra funzio-\
             * nare e no: la sua finestra di interrupt e' `bid $r0` verso un   \
             * indirizzo calcolato che coincide con gpr[0], cioe' esattamente  \
             * il caso che SPU_TAILRET scambia per un ritorno -- e quella      \
             * finestra e' l'unico punto in cui lo stall-and-notify puo'       \
             * entrare. */                                                    \
            int _irq = ((ctx)->int_enable &&                                  \
                        ((ctx)->event_status & (ctx)->event_mask)) ? 1 : 0;   \
            if (!_irq) {                                                      \
            /* Ritorno ESATTO: il trasferimento in sospeso punta                \
             * all'indirizzo di ritorno di questa chiamata. Non e' un'euristica \
             * come quello su gpr[0] qui sotto -- e' l'indirizzo che il brsl ha \
             * piantato -- ma cambia il dispatch di tutti i titoli, quindi resta \
             * dietro SPU_RETLINK finche' non e' misurato altrove. */            \
            if (spu_retlink_enabled() && (ctx)->host_depth > 0 &&              \
                (ctx)->ret_link &&                                             \
                (((uint32_t)(ctx)->pc & SPU_LS_MASK) == (ctx)->ret_link)) {    \
                g_spu_trampoline_fn = 0;                                       \
                break;                                                         \
            }                                                                  \
            /* SPU_TAILRET=1: a pending transfer whose target equals the   \
             * link register IS a return. SPU_RET only recognises `bi $r0`;\
             * code returning through another register (`bi $r6`, r6 = the \
             * link) lowers as a transfer, so the host frame opened by the \
             * matching brsl/bisl never pops and a poll loop nests a frame \
             * per iteration until the host stack dies. Off by default --  \
             * this changes core dispatch for every title. */              \
            if (spu_tailret_enabled() && (ctx)->host_depth > 0 &&          \
                (((uint32_t)(ctx)->pc & SPU_LS_MASK) ==                    \
                 ((ctx)->gpr[0]._u32[0] & SPU_LS_MASK))) {                 \
                g_spu_trampoline_fn = 0;                                   \
                break;                                                     \
            }                                                              \
            }                                                              \
            spu_pcwatch(ctx);                                   \
            void (*_tf)(spu_context*) = g_spu_trampoline_fn;    \
            g_spu_trampoline_fn = 0;                            \
            yz_lockstep_tick(ctx);                             \
            spu_task_launch_check((ctx), (void*)_tf);          \
            if (_irq)                                           \
                _tf = spu_take_interrupt((ctx), _tf);          \
            _tf(ctx);                                          \
        }                                                      \
    } while (0)

/* Depth-aware SPU link return (`bi $r0`): host `return` while a matched
 * brsl/bisl frame is live (host_depth>0); at 0 the frame was destroyed (task
 * resume / restack unwind) so dispatch to the link register instead -- faithful
 * to CBEA `bi` (branch to word 0 of RA). pc set unconditionally. */
/* Un `bi $r0` con r0 DIVERSO dall'indirizzo di ritorno della chiamata corrente
 * (ret_link, piantato da brsl/bisl) non e' il ritorno di questo frame: e' un
 * salto calcolato, e va dispatchato come tale. Il policy module di WwsJob fa
 * `ila $r0, epilogo; br helper` e l'helper finisce con `bi $r0`: preso per
 * ritorno, saltava l'epilogo (stack e r80-r81 mai ripristinati: il job 218 di
 * Uncharted usciva senza disegnare). SPU_RET_LOOSE=1 ripristina il vecchio
 * comportamento. */
static inline int spu_ret_strict(void)
{
    static int s = -1;
    if (s < 0) s = getenv("SPU_RET_LOOSE") ? 0 : 1;
    return s;
}
#define SPU_RETR(ctx, reg) do {                                \
        (ctx)->pc = (ctx)->gpr[reg]._u32[0];                   \
        if ((ctx)->host_depth == 0) {                          \
            /* s.48: come SPU_XIND (nessun chiamante C: dispatch) */ \
            void (*_xf)(spu_context*) = spu_ib_inline(ctx);    \
            if (__builtin_expect(_xf != 0, 1)) {               \
                (ctx)->hops++; (ctx)->steps++; SPU_TAILCALL(_xf(ctx)); } \
            g_spu_trampoline_fn = spu_indirect_branch; }       \
        else if (spu_ret_strict() && (ctx)->ret_link &&        \
                 (((ctx)->pc & SPU_LS_MASK) != (ctx)->ret_link)) { \
            static int _n = 0; extern int g_hle_trace_armed;   \
            if (__builtin_expect(_n < 60, 0)) { _n++; fprintf(stderr, "[spu-ret] salto: r%d=0x%05X ret_link=0x%05X depth=%d img=%d ovl=%d\n", \
                (reg), (ctx)->pc & SPU_LS_MASK, (ctx)->ret_link, (ctx)->host_depth, (ctx)->image_id, (ctx)->resident_ovl); } \
            /* s.50: e' un salto calcolato, come SPU_XIND. Il ciclo dei comandi \
             * del modulo 201 (`ila $r0, testa; bi gestore`, il gestore chiude \
             * con `bi $r0`) ci passava ~1/4 di tutte le ricadute nel trampolino; \
             * e _n++ senza tetto era una riga condivisa scritta da tutte le lane. */ \
            void (*_xf)(spu_context*) = spu_ib_inline(ctx);    \
            if (__builtin_expect(_xf != 0, 1)) {               \
                (ctx)->hops++; (ctx)->steps++; SPU_TAILCALL(_xf(ctx)); } \
            g_spu_trampoline_fn = spu_indirect_branch; }       \
        return;                                                \
    } while (0)
#define SPU_RET(ctx) SPU_RETR(ctx, 0)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPU_CONTEXT_H */

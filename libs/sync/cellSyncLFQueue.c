/*
 * ps3recomp - CellSyncLFQueue, nel layout reale del gioco
 *
 * Questa coda e' il canale con cui il lato PPU di un titolo passa lavoro al suo
 * lato SPU. Il punto che rende tutto il resto obbligato: l'oggetto coda sono
 * 128 byte di memoria DEL GIOCO, e il consumatore e' codice SPU del titolo che
 * li legge con GETLLAR e li riscrive con PUTLLC, nel layout Sony, big-endian.
 *
 * La versione precedente era una struct dell'host (atomic_uint nativi, un
 * PUNTATORE host a 64 bit dentro la memoria guest) scritta allo stesso
 * indirizzo. I due lati non potevano incontrarsi: il PPU spingeva in campi che
 * per l'SPU erano rumore, e l'SPU leggeva una coda per sempre vuota. Uncharted
 * si ferma esattamente li' -- il task 0 del taskset parcheggia in WAIT_SIGNAL
 * al primo pop e il PPU aspetta per sempre l'event flag che quel task dovrebbe
 * alzare a lavoro finito.
 *
 * Layout (RPCS3 Emu/Cell/Modules/cellSync.h, verificato contro il codice SPU
 * del titolo -- l'immagine 1 fa GETLLAR di 128 byte sulla coda e legge m_h4 a
 * +0x06, m_h5 a +0x08, m_depth a +0x14 e i tre campi da 5 bit di push2 a +0x30,
 * esattamente questi offset):
 *
 *   0x00  pop1   : m_h1 m_h2 m_h3 m_h4   (4 x be16, unita' atomica da 8 byte)
 *   0x08  push1  : m_h5 m_h6 m_h7 m_h8   (4 x be16, unita' atomica da 8 byte)
 *   0x10  m_size      be32
 *   0x14  m_depth     be32
 *   0x18  m_buffer    be64  (EA guest; bit 0 = flag ANY2ANY)
 *   0x20  m_bs[4]     u8
 *   0x24  m_direction be32
 *   0x28  m_v1        be32
 *   0x2C  init        be32
 *   0x30  push2.pack  be16  (tre campi da 5 bit)
 *   0x32  m_hs1[15]   be16  (id dei consumatori in attesa: chi svegliare)
 *   0x50  pop2.pack   be16
 *   0x52  m_hs2[15]   be16
 *   0x70  m_eaSignal  be64  (per una coda SPURS: l'EA del taskset)
 *   0x78  m_v2        be32
 *   0x7C  m_eq_id     be32
 *
 * pop3 sono i primi 2 campi di pop1 (m_h1, m_h2), push3 i primi 2 di push1.
 */
#include "cellSync.h"
#include "../../runtime/ppu/ppu_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define SYNC_YIELD() SwitchToThread()
#else
#include <sched.h>
#define SYNC_YIELD() sched_yield()
#endif

/* Gli argomenti HLE arrivano come EA guest travestiti da puntatore. */
#define EA_OF(p) ((u32)(uintptr_t)(p))

/* Lo stesso lucchetto che serializza GETLLAR/PUTLLC di tutti gli SPU
 * (runtime/spu/spu_channels.c). Tenendolo per l'intera read-modify-write il
 * lato PPU e' atomico rispetto a ogni singola operazione atomica SPU; e una
 * PUTLLC che stesse a cavallo della nostra scrittura fallisce da sola, perche'
 * la commit confronta la linea con lo snapshot preso alla GETLLAR. */
extern void spu_atomic_line_lock(void);
extern void spu_atomic_line_unlock(void);

/* Offset dei campi */
#define LFQ_POP1        0x00u
#define LFQ_H1          0x00u
#define LFQ_H2          0x02u
#define LFQ_H3          0x04u
#define LFQ_H4          0x06u
#define LFQ_PUSH1       0x08u
#define LFQ_H5          0x08u
#define LFQ_H6          0x0Au
#define LFQ_H7          0x0Cu
#define LFQ_H8          0x0Eu
#define LFQ_SIZE        0x10u
#define LFQ_DEPTH       0x14u
#define LFQ_BUFFER      0x18u
#define LFQ_BS          0x20u
#define LFQ_DIRECTION   0x24u
#define LFQ_V1          0x28u
#define LFQ_INIT        0x2Cu
#define LFQ_PUSH2       0x30u
#define LFQ_HS1         0x32u
#define LFQ_POP2        0x50u
#define LFQ_HS2         0x52u
#define LFQ_EASIGNAL    0x70u
#define LFQ_V2          0x78u
#define LFQ_EQ_ID       0x7Cu
#define LFQ_BYTES       0x80u

#define LFQ_NO_SIGNAL   0xFFFFFFFFu

static int lfq_trace(void)
{
    static int t = -1;
    if (t < 0) t = getenv("PS3_LFQ_TRACE") ? 1 : 0;
    return t;
}

/* Zeri iniziali in un u16, 0..16 (il conto che RPCS3 fa con countl_zero su un
 * u16 promosso a u32, meno 16). */
static int lfq_clz16(u16 v)
{
    int n = 0;
    for (int b = 15; b >= 0; b--) {
        if (v & (1u << b)) break;
        n++;
    }
    return n;
}

/* =========================================================================
 * Inizializzazione
 * =====================================================================*/

static void lfq_init_fields(u32 ea, u32 buffer_ea, u32 size, u32 depth,
                            u32 direction, u32 eaSignal)
{
    vm_write32(ea + LFQ_SIZE, size);
    vm_write32(ea + LFQ_DEPTH, depth);
    vm_write64(ea + LFQ_BUFFER, (u64)buffer_ea);
    vm_write32(ea + LFQ_DIRECTION, direction);
    for (int i = 0; i < 15; i++) {
        vm_write16(ea + LFQ_HS1 + 2u * i, 0);
        vm_write16(ea + LFQ_HS2 + 2u * i, 0);
    }
    vm_write64(ea + LFQ_EASIGNAL, (u64)eaSignal);

    if (direction == CELL_SYNC_QUEUE_ANY2ANY) {
        vm_write64(ea + LFQ_POP1, 0);
        vm_write64(ea + LFQ_PUSH1, 0);
        vm_write64(ea + LFQ_BUFFER, (u64)buffer_ea | 1u);
        vm_write8(ea + LFQ_BS + 0, 0xFF);
        vm_write8(ea + LFQ_BS + 1, 0xFF);
        vm_write32(ea + LFQ_V1, 0xFFFFFFFFu);
        vm_write16(ea + LFQ_PUSH2, 0xFFFF);
        vm_write16(ea + LFQ_POP2, 0xFFFF);
    } else {
        u16 h3 = vm_read16(ea + LFQ_H3);
        u16 h7 = vm_read16(ea + LFQ_H7);
        vm_write16(ea + LFQ_H1, 0); vm_write16(ea + LFQ_H2, 0);
        vm_write16(ea + LFQ_H3, h3); vm_write16(ea + LFQ_H4, 0);
        vm_write16(ea + LFQ_H5, 0); vm_write16(ea + LFQ_H6, 0);
        vm_write16(ea + LFQ_H7, h7); vm_write16(ea + LFQ_H8, 0);
        vm_write32(ea + LFQ_BS, 0xFFFFFFFFu);
        vm_write32(ea + LFQ_V1, 0);
        vm_write16(ea + LFQ_PUSH2, 0);
        vm_write16(ea + LFQ_POP2, 0);
    }

    vm_write32(ea + LFQ_V2, 0);
    vm_write32(ea + LFQ_EQ_ID, 0);
}

s32 cellSyncLFQueueInitialize(CellSyncLFQueue* queue, void* buffer,
                              u32 size, u32 depth, u32 direction,
                              void* eaSignal)
{
    u32 ea  = EA_OF(queue);
    u32 buf = EA_OF(buffer);
    u32 sig = EA_OF(eaSignal);

    if (!ea) return CELL_SYNC_ERROR_NULL_POINTER;
    if (size) {
        if (!buf) return CELL_SYNC_ERROR_NULL_POINTER;
        if (size > 0x4000 || (size % 16)) return CELL_SYNC_ERROR_INVAL;
    }
    if (!depth || depth > 0x7FFF || direction > 3) return CELL_SYNC_ERROR_INVAL;
    if ((ea & 127u) || (buf & 15u)) return CELL_SYNC_ERROR_ALIGN;

    spu_atomic_line_lock();
    u32 old = vm_read32(ea + LFQ_INIT);
    if (old == 0) {
        /* Su hardware una coda gia' sporca fa tornare CELL_SYNC_ERROR_STAT.
         * Qui lo segnalo e inizializzo lo stesso: se i byte non sono a zero il
         * problema e' a monte, nel nostro allocatore, e far fallire l'init
         * nasconderebbe la causa dietro a un titolo che rinuncia. */
        int dirty = 0;
        for (u32 o = 0; o < LFQ_BYTES; o += 8)
            if (vm_read64(ea + o)) { dirty = 1; break; }
        if (dirty)
            fprintf(stderr, "[cellSync] LFQueueInitialize: coda 0x%08X non azzerata "
                            "(hardware direbbe STAT) -- inizializzo comunque\n", ea);
        lfq_init_fields(ea, buf, size, depth, direction, sig);
        vm_write32(ea + LFQ_INIT, 0);
    } else if (old == 2) {
        if (vm_read32(ea + LFQ_SIZE) != size ||
            vm_read32(ea + LFQ_DEPTH) != depth ||
            (u32)vm_read64(ea + LFQ_BUFFER) != buf) {
            spu_atomic_line_unlock();
            return CELL_SYNC_ERROR_INVAL;
        }
    }
    spu_atomic_line_unlock();

    printf("[cellSync] LFQueueInitialize(queue=0x%08X buf=0x%08X size=%u depth=%u "
           "dir=%u eaSignal=0x%08X)\n", ea, buf, size, depth, direction, sig);
    return CELL_OK;
}

/* =========================================================================
 * Lato push (una coda PPU2SPU la spinge il PPU)
 * =====================================================================*/

/* Prende uno slot. Torna CELL_OK con *pointer valorizzato, CELL_SYNC_ERROR_AGAIN
 * se la coda e' piena e non stiamo bloccando, o un errore. */
static s32 lfq_get_push_pointer(u32 ea, s32* pointer, u32 isBlocking)
{
    if (vm_read32(ea + LFQ_DIRECTION) != CELL_SYNC_QUEUE_PPU2SPU)
        return CELL_SYNC_ERROR_PERM;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);

    for (;;) {
        spu_atomic_line_lock();
        s32 h8   = (s16)vm_read16(ea + LFQ_H8);
        s32 used = h8 - (s32)vm_read16(ea + LFQ_H1);
        if (used < 0) used += depth * 2;

        if (used < depth) {
            *pointer = h8;
            vm_write16(ea + LFQ_H8, (u16)((h8 + 1 >= depth * 2) ? 0 : h8 + 1));
            spu_atomic_line_unlock();
            return CELL_OK;
        }
        spu_atomic_line_unlock();

        if (!isBlocking) return CELL_SYNC_ERROR_AGAIN;
        SYNC_YIELD();
    }
}

/* Chiude la push: fa avanzare la finestra push3 e, se un consumatore si era
 * registrato in m_hs1, restituisce chi svegliare tramite `send`. E' quel
 * risveglio -- non la scrittura del dato -- a far ripartire il task SPU. */
static s32 lfq_complete_push_pointer(u32 ea, s32 pointer,
                                     CellSyncLFQueueSendSignal send)
{
    if (vm_read32(ea + LFQ_DIRECTION) != CELL_SYNC_QUEUE_PPU2SPU)
        return CELL_SYNC_ERROR_PERM;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);

    for (;;) {
        spu_atomic_line_lock();

        u16 pack = vm_read16(ea + LFQ_PUSH2);
        u16 h5   = vm_read16(ea + LFQ_H5);
        u16 h6   = vm_read16(ea + LFQ_H6);

        s32 var1 = pointer - (s32)h5;
        if (var1 < 0) var1 += depth * 2;

        s32 var2 = (s32)(s16)vm_read16(ea + LFQ_H4) - (s32)vm_read16(ea + LFQ_H1);
        if (var2 < 0) var2 += depth * 2;

        s32 var9_ = 15 - var1;
        var9_ = (var9_ & 0x30) ? 0 : (1 << var9_);

        s32 var9 = lfq_clz16((u16)~((u16)var9_ | h6));

        s32 var5 = (s32)h6 | var9_;
        var5 = (var9 & 0x30) ? 0 : (var5 << var9);

        s32 var3 = (s32)h5 + var9;
        if (var3 >= depth * 2) var3 -= depth * 2;

        s32 f_hi = (pack >> 10) & 0x1F;
        s32 f_mid = (pack >> 5) & 0x1F;
        s32 f_lo = pack & 0x1F;

        s32 var4 = f_hi - f_mid;
        if (var4 < 0) var4 += 0x1E;

        u32 var6 = LFQ_NO_SIGNAL;
        u16 newpack = pack;
        if (var2 + var4 <= 15 && f_hi != f_lo) {
            s32 var11 = f_hi;
            if (var11 >= 15) var11 -= 15;
            u16 var12 = (u16)f_hi;
            var12 = (var12 == 0x1D) ? 0 : (u16)((var12 + 1) << 10);
            newpack = (u16)((pack & 0x83FF) | var12);
            var6 = vm_read16(ea + LFQ_HS1 + 2u * (u32)var11);
        }

        vm_write16(ea + LFQ_PUSH2, newpack);

        if (var6 != LFQ_NO_SIGNAL) {
            vm_write16(ea + LFQ_H5, (u16)var3);
            vm_write16(ea + LFQ_H6, (u16)var5);
            u32 sigea = (u32)vm_read64(ea + LFQ_EASIGNAL);
            spu_atomic_line_unlock();
            if (lfq_trace())
                fprintf(stderr, "[cellSync] LFQueue 0x%08X push completa: sveglio "
                        "arg=%u su eaSignal=0x%08X\n", ea, var6, sigea);
            if (!send) {
                fprintf(stderr, "[cellSync] LFQueue 0x%08X: c'e' un consumatore da "
                        "svegliare (arg=%u) ma nessuna callback di segnale\n", ea, var6);
                return CELL_OK;
            }
            return send(sigea, var6);
        }

        if ((newpack & 0x1F) == ((newpack >> 10) & 0x1F)) {
            vm_write16(ea + LFQ_H5, (u16)var3);
            vm_write16(ea + LFQ_H6, (u16)var5);
            spu_atomic_line_unlock();
            return CELL_OK;
        }
        spu_atomic_line_unlock();
        SYNC_YIELD();   /* un'altra push e' a meta': lasciala chiudere */
    }
}

/* Ultima coda su cui si e' spinto: serve a fotografarne i puntatori quando il
 * PPU si mette ad aspettare, per dire se un elemento e' rimasto dentro senza
 * che il consumatore SPU lo prendesse. */
static u32 s_lfq_ultima;

void cellSync_lfq_dump_ultima(void)
{
    u32 ea = s_lfq_ultima;
    if (!ea) { fprintf(stderr, "[cellSync] nessuna coda usata\n"); return; }
    fprintf(stderr,
            "[cellSync] coda 0x%08X  pop1=%u push1=%u  push2=%u pop2=%u  "
            "hs1=%u  depth=%d size=%u\n",
            ea,
            (unsigned)(u16)vm_read16(ea + LFQ_POP1),
            (unsigned)(u16)vm_read16(ea + LFQ_PUSH1),
            (unsigned)(u16)vm_read16(ea + LFQ_PUSH2),
            (unsigned)(u16)vm_read16(ea + LFQ_POP2),
            (unsigned)(u16)vm_read16(ea + LFQ_HS1),
            (int)vm_read32(ea + LFQ_DEPTH),
            (unsigned)vm_read32(ea + LFQ_SIZE));
    fflush(stderr);
}

s32 cellSyncLFQueuePushSignal(CellSyncLFQueue* queue, const void* data,
                              u32 isBlocking, CellSyncLFQueueSendSignal send)
{
    u32 ea  = EA_OF(queue);
    u32 src = EA_OF(data);
    if (!ea || !src) return CELL_SYNC_ERROR_NULL_POINTER;
    if ((ea & 127u) || (src & 15u)) return CELL_SYNC_ERROR_ALIGN;

    s_lfq_ultima = ea;

    s32 pos = 0;
    s32 res = lfq_get_push_pointer(ea, &pos, isBlocking);
    if (res != CELL_OK) return res;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);
    const u32 size  = vm_read32(ea + LFQ_SIZE);
    const u32 buf   = (u32)(vm_read64(ea + LFQ_BUFFER) & ~1ull);
    const u32 slot  = buf + size * (u32)(pos >= depth ? pos - depth : pos);

    memcpy(vm_ptr8(slot), vm_ptr8(src), size);

    if (lfq_trace())
        fprintf(stderr, "[cellSync] LFQueue 0x%08X push pos=%d slot=0x%08X size=%u\n",
                ea, pos, slot, size);

    return lfq_complete_push_pointer(ea, pos, send);
}

s32 cellSyncLFQueuePush(CellSyncLFQueue* queue, const void* data)
{
    return cellSyncLFQueuePushSignal(queue, data, 1, NULL);
}

s32 cellSyncLFQueueTryPush(CellSyncLFQueue* queue, const void* data)
{
    return cellSyncLFQueuePushSignal(queue, data, 0, NULL);
}

/* =========================================================================
 * Lato pop (una coda SPU2PPU la svuota il PPU)
 * =====================================================================*/

static s32 lfq_get_pop_pointer(u32 ea, s32* pointer, u32 isBlocking)
{
    if (vm_read32(ea + LFQ_DIRECTION) != CELL_SYNC_QUEUE_SPU2PPU)
        return CELL_SYNC_ERROR_PERM;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);

    for (;;) {
        spu_atomic_line_lock();
        s32 h4    = (s16)vm_read16(ea + LFQ_H4);
        s32 avail = (s32)vm_read16(ea + LFQ_H5) - h4;
        if (avail < 0) avail += depth * 2;

        if (avail > 0) {
            *pointer = h4;
            vm_write16(ea + LFQ_H4, (u16)((h4 + 1 >= depth * 2) ? 0 : h4 + 1));
            spu_atomic_line_unlock();
            return CELL_OK;
        }
        spu_atomic_line_unlock();

        if (!isBlocking) return CELL_SYNC_ERROR_AGAIN;
        SYNC_YIELD();
    }
}

static s32 lfq_complete_pop_pointer(u32 ea, s32 pointer,
                                    CellSyncLFQueueSendSignal send, u32 noQueueFull)
{
    if (vm_read32(ea + LFQ_DIRECTION) != CELL_SYNC_QUEUE_SPU2PPU)
        return CELL_SYNC_ERROR_PERM;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);

    for (;;) {
        spu_atomic_line_lock();

        u16 pack = vm_read16(ea + LFQ_POP2);
        u16 h1   = vm_read16(ea + LFQ_H1);
        u16 h2   = vm_read16(ea + LFQ_H2);

        s32 var1 = pointer - (s32)h1;
        if (var1 < 0) var1 += depth * 2;

        s32 var2 = (s32)(s16)vm_read16(ea + LFQ_H8) - (s32)vm_read16(ea + LFQ_H5);
        if (var2 < 0) var2 += depth * 2;

        s32 var9_ = 15 - var1;
        var9_ = (var9_ & 0x30) ? 0 : (1 << var9_);

        s32 var9 = lfq_clz16((u16)~((u16)var9_ | h2));

        s32 var5 = (s32)h2 | var9_;
        var5 = (var9 & 0x30) ? 0 : (var5 << var9);

        s32 var3 = (s32)h1 + var9;
        if (var3 >= depth * 2) var3 -= depth * 2;

        s32 f_hi = (pack >> 10) & 0x1F;
        s32 f_mid = (pack >> 5) & 0x1F;
        s32 f_lo = pack & 0x1F;

        s32 var4 = f_hi - f_mid;
        if (var4 < 0) var4 += 0x1E;

        u32 var6 = LFQ_NO_SIGNAL;
        u16 newpack = pack;
        if (!noQueueFull && var2 + var4 <= 15 && f_hi != f_lo) {
            s32 var11 = f_hi;
            if (var11 >= 15) var11 -= 15;
            u16 var12 = (u16)f_hi;
            var12 = (var12 == 0x1D) ? 0 : (u16)((var12 + 1) << 10);
            newpack = (u16)((pack & 0x83FF) | var12);
            var6 = vm_read16(ea + LFQ_HS2 + 2u * (u32)var11);
        }

        vm_write16(ea + LFQ_POP2, newpack);

        if (var6 != LFQ_NO_SIGNAL) {
            vm_write16(ea + LFQ_H1, (u16)var3);
            vm_write16(ea + LFQ_H2, (u16)var5);
            u32 sigea = (u32)vm_read64(ea + LFQ_EASIGNAL);
            spu_atomic_line_unlock();
            if (!send) return CELL_OK;
            return send(sigea, var6);
        }

        if ((newpack & 0x1F) == ((newpack >> 10) & 0x1F)) {
            vm_write16(ea + LFQ_H1, (u16)var3);
            vm_write16(ea + LFQ_H2, (u16)var5);
            spu_atomic_line_unlock();
            return CELL_OK;
        }
        spu_atomic_line_unlock();
        SYNC_YIELD();
    }
}

static s32 lfq_pop_body(CellSyncLFQueue* queue, void* data, u32 isBlocking)
{
    u32 ea  = EA_OF(queue);
    u32 dst = EA_OF(data);
    if (!ea || !dst) return CELL_SYNC_ERROR_NULL_POINTER;
    if ((ea & 127u) || (dst & 15u)) return CELL_SYNC_ERROR_ALIGN;

    s32 pos = 0;
    s32 res = lfq_get_pop_pointer(ea, &pos, isBlocking);
    if (res != CELL_OK) return res;

    const s32 depth = (s32)vm_read32(ea + LFQ_DEPTH);
    const u32 size  = vm_read32(ea + LFQ_SIZE);
    const u32 buf   = (u32)(vm_read64(ea + LFQ_BUFFER) & ~1ull);
    const u32 slot  = buf + size * (u32)(pos >= depth ? pos - depth : pos);

    memcpy(vm_ptr8(dst), vm_ptr8(slot), size);

    return lfq_complete_pop_pointer(ea, pos, NULL, 0);
}

s32 cellSyncLFQueuePop(CellSyncLFQueue* queue, void* data)
{
    return lfq_pop_body(queue, data, 1);
}

s32 cellSyncLFQueueTryPop(CellSyncLFQueue* queue, void* data)
{
    return lfq_pop_body(queue, data, 0);
}

/* =========================================================================
 * Interrogazioni
 * =====================================================================*/

s32 cellSyncLFQueueGetDirection(const CellSyncLFQueue* queue, u32* dir)
{
    u32 ea = EA_OF(queue), d = EA_OF(dir);
    if (!ea || !d) return CELL_SYNC_ERROR_NULL_POINTER;
    if (ea & 127u) return CELL_SYNC_ERROR_ALIGN;
    vm_write32(d, vm_read32(ea + LFQ_DIRECTION));
    return CELL_OK;
}

s32 cellSyncLFQueueDepth(const CellSyncLFQueue* queue, u32* depth)
{
    u32 ea = EA_OF(queue), d = EA_OF(depth);
    if (!ea || !d) return CELL_SYNC_ERROR_NULL_POINTER;
    if (ea & 127u) return CELL_SYNC_ERROR_ALIGN;
    vm_write32(d, vm_read32(ea + LFQ_DEPTH));
    return CELL_OK;
}

s32 cellSyncLFQueueGetEntrySize(const CellSyncLFQueue* queue, u32* entry_size)
{
    u32 ea = EA_OF(queue), d = EA_OF(entry_size);
    if (!ea || !d) return CELL_SYNC_ERROR_NULL_POINTER;
    if (ea & 127u) return CELL_SYNC_ERROR_ALIGN;
    vm_write32(d, vm_read32(ea + LFQ_SIZE));
    return CELL_OK;
}

s32 cellSyncLFQueueSize(CellSyncLFQueue* queue, u32* size)
{
    u32 ea = EA_OF(queue), d = EA_OF(size);
    if (!ea || !d) return CELL_SYNC_ERROR_NULL_POINTER;
    if (ea & 127u) return CELL_SYNC_ERROR_ALIGN;

    spu_atomic_line_lock();
    u32 var1 = vm_read16(ea + LFQ_H1);
    u32 var2 = vm_read16(ea + LFQ_H5);
    u32 depth = vm_read32(ea + LFQ_DEPTH);
    spu_atomic_line_unlock();

    vm_write32(d, (var1 <= var2) ? (var2 - var1) : (var2 - var1 + depth * 2));
    return CELL_OK;
}

s32 cellSyncLFQueueClear(CellSyncLFQueue* queue)
{
    u32 ea = EA_OF(queue);
    if (!ea) return CELL_SYNC_ERROR_NULL_POINTER;
    if (ea & 127u) return CELL_SYNC_ERROR_ALIGN;

    spu_atomic_line_lock();
    u16 h1 = vm_read16(ea + LFQ_H1), h3 = vm_read16(ea + LFQ_H3);
    u16 h4 = vm_read16(ea + LFQ_H4);
    u16 h5 = vm_read16(ea + LFQ_H5), h6 = vm_read16(ea + LFQ_H6);
    u16 h7 = vm_read16(ea + LFQ_H7), h8 = vm_read16(ea + LFQ_H8);

    s32 var1, var2;
    if (vm_read32(ea + LFQ_DIRECTION) != CELL_SYNC_QUEUE_ANY2ANY) {
        var1 = var2 = (s32)vm_read16(ea + LFQ_POP2);
    } else {
        var1 = h7; var2 = h3;
    }

    if ((s16)h4 != (s16)h1 || (s16)h8 != (s16)h5 ||
        ((var2 >> 10) & 0x1F) != (var2 & 0x1F) ||
        ((var1 >> 10) & 0x1F) != (var1 & 0x1F)) {
        spu_atomic_line_unlock();
        return CELL_SYNC_ERROR_BUSY;
    }

    vm_write16(ea + LFQ_H1, h5);
    vm_write16(ea + LFQ_H2, h6);
    vm_write16(ea + LFQ_H3, h7);
    vm_write16(ea + LFQ_H4, h8);
    spu_atomic_line_unlock();
    return CELL_OK;
}

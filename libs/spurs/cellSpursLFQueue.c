/*
 * ps3recomp - cellSpurs lock-free queue
 *
 * SPURS' lock-free queue is the channel a title's PPU side uses to hand work to
 * its SPU side, and its entry points are thin wrappers over the cellSync ones:
 * the queue object is a CellSyncLFQueue with a SPURS (or taskset) instance
 * attached, which is what lets the SPU half find the queue from its own side.
 *
 * They were missing entirely. Uncharted calls _cellSpursLFQueueInitialize and
 * cellSpursLFQueueAttachLv2EventQueue while starting its job manager, both came
 * back as "unresolved NID", and with no queue ever initialised the SPURS kernel
 * poller saw ready=0 forever -- SPU workloads dispatched, found nothing to do,
 * and returned. The job side was running the whole time; nothing could reach it.
 *
 * NIDs (computed, verified against the title's own import table):
 *   0x011EE38B  _cellSpursLFQueueInitialize
 *   0x8A85674D  _cellSpursLFQueuePushBody
 *   0x1656D49F  cellSpursLFQueueAttachLv2EventQueue
 *   0x73E06F91  cellSpursLFQueueDetachLv2EventQueue
 */
#include "cellSpurs.h"
#include "../sync/cellSync.h"
#include "ps3emu/error_codes.h"
#include "../../runtime/ppu/ppu_memory.h"
#include <stdio.h>

/* The SPURS/taskset instance a queue belongs to. The guest passes it as the
 * first argument to the initializer and the SPU side reads it back out of the
 * queue object; keeping it beside the queue EA is enough for the PPU half. */
#define SPURS_LFQ_MAX 64
static struct { u32 queue_ea; u32 owner_ea; u32 event_queue; } s_lfq[SPURS_LFQ_MAX];

static int lfq_slot(u32 queue_ea)
{
    for (int i = 0; i < SPURS_LFQ_MAX; i++)
        if (s_lfq[i].queue_ea == queue_ea) return i;
    for (int i = 0; i < SPURS_LFQ_MAX; i++)
        if (!s_lfq[i].queue_ea) { s_lfq[i].queue_ea = queue_ea; return i; }
    return -1;
}

/* NID: 0x011EE38B */
s32 _cellSpursLFQueueInitialize(void* pTasksetOrSpurs, void* queue,
                                const void* buffer, u32 size, u32 depth,
                                u32 direction)
{
    if (!queue) return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    u32 qea = (u32)(uintptr_t)queue;
    int i = lfq_slot(qea);
    if (i >= 0) {
        s_lfq[i].owner_ea = (u32)(uintptr_t)pTasksetOrSpurs;
        s_lfq[i].event_queue = 0;
    }

    printf("[cellSpurs] LFQueueInitialize(queue=0x%08X owner=0x%08X buf=0x%08X "
           "size=%u depth=%u dir=%u)\n",
           qea, (u32)(uintptr_t)pTasksetOrSpurs, (u32)(uintptr_t)buffer,
           size, depth, direction);

    /* Il corpo della coda e' un CellSyncLFQueue: l'inizializzatore scrive il
     * layout reale nella memoria del gioco, e l'owner (taskset o istanza SPURS)
     * diventa m_eaSignal -- il destinatario del risveglio. */
    return cellSyncLFQueueInitialize((CellSyncLFQueue*)queue, (void*)(uintptr_t)buffer,
                                     size, depth, direction, pTasksetOrSpurs);
}

/* Chi svegliare quando una push rende la coda non vuota.
 *
 * Il consumatore SPU, trovando la coda vuota, registra il proprio task id in
 * m_hs1 dentro l'oggetto coda e poi parcheggia con la syscall WAIT_SIGNAL del
 * taskset. La chiusura della push rilegge quell'id e chiama questa callback con
 * (m_eaSignal, id): per una coda SPURS m_eaSignal e' l'EA del taskset, che e'
 * esattamente cio' che _cellSpursLFQueueInitialize ci ha passato come owner.
 *
 * Senza questa callback il dato finisce nel buffer e nessuno se ne accorge: e'
 * il segnale, non la scrittura, a far ripartire il task. */
static s32 spurs_lfq_send_signal(u32 eaSignal, u32 arg)
{
    return cellSpursSendSignal((CellSpursTaskset*)(uintptr_t)eaSignal,
                               (CellSpursTaskId)arg);
}

/* NID: 0x8A85674D
 *
 * Il terzo argomento e' isBlocking, come in _cellSyncLFQueuePushBody:
 * cellSpursLFQueuePush passa 1, cellSpursLFQueueTryPush passa 0. */
s32 _cellSpursLFQueuePushBody(void* queue, const void* buffer, u32 isBlocking)
{
    if (!queue) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    return cellSyncLFQueuePushSignal((CellSyncLFQueue*)queue, buffer,
                                     isBlocking, spurs_lfq_send_signal);
}

/* NID: 0x1656D49F
 *
 * On hardware this binds an lv2 event queue so the SPU side gets woken when the
 * queue goes from empty to non-empty. Our SPURS kernel polls instead, so the
 * binding only has to be remembered and reported as successful -- refusing it
 * is what makes a title give up on its job manager. */
s32 cellSpursLFQueueAttachLv2EventQueue(void* queue)
{
    if (!queue) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    u32 qea = (u32)(uintptr_t)queue;
    int i = lfq_slot(qea);
    if (i >= 0) s_lfq[i].event_queue = 1;
    printf("[cellSpurs] LFQueueAttachLv2EventQueue(queue=0x%08X)\n", qea);
    return CELL_OK;
}

/* NID: 0x73E06F91 */
s32 cellSpursLFQueueDetachLv2EventQueue(void* queue)
{
    if (!queue) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    u32 qea = (u32)(uintptr_t)queue;
    int i = lfq_slot(qea);
    if (i >= 0) s_lfq[i].event_queue = 0;
    printf("[cellSpurs] LFQueueDetachLv2EventQueue(queue=0x%08X)\n", qea);
    return CELL_OK;
}

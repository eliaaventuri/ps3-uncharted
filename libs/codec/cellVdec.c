/*
 * ps3recomp - cellVdec HLE
 *
 * Stub decoder with the SDK's real API shape (see cellVdec.h for the guest
 * layouts). Every access unit produces one black picture; the picture size
 * comes from the MPEG-2 sequence header found in the AU data, so the title's
 * upload/convert path sees the dimensions the stream really has.
 *
 * Callbacks are guest functions: they go through ps3_invoke_guest, and they
 * run synchronously on the calling thread inside DecodeAu/EndSeq (the real
 * library has its own PPU thread; titles only use the callback to signal).
 */

#include "cellVdec.h"
#include <stdio.h>
#include <string.h>
#include "../guest_struct.h"       /* vm_read/vm_write (guest EA -> host) */
#include "ps3emu/guest_call.h"     /* ps3_invoke_guest */

#define MAX_VDEC        4
#define VDEC_PICITEM_SZ 80
#define VDEC_PICINFO_SZ 0x80
#define VDEC_PICRING    256        /* coda di descrittori nella memoria del titolo: il
                                    * titolo manda decine di AU prima di ritirare la
                                    * prima immagine e poi le ritira una per una */

typedef struct {
    int  in_use;
    u32  codecType;
    u32  cb_opd;        /* OPD guest della callback */
    u32  cb_arg;
    u32  mem_ea, mem_size;   /* CellVdecResource: memoria data dal titolo */
    u32  ring_ea;       /* dove vivono i CellVdecPicItem (+ Mpeg2Info) */
    int  seqStarted;
    u32  auCount;
    u16  width, height;      /* dal sequence header, altrimenti 0 */
    u32  aspect, frc, bitrate, vbv;
    /* coda delle immagini "decodificate" non ancora ritirate con GetPicItem */
    u32  q_head, q_tail;     /* indici nell'anello dei descrittori */
} VdecSlot;

static VdecSlot s_vdec[MAX_VDEC];

/* Memoria guest di riserva se il titolo non passa una CellVdecResource
 * utilizzabile: bump sotto lo stack principale, sopra la zona di gcm_guest_alloc
 * (0x0F800000..) di ppu_sysprx.cpp. Mai liberata. */
static u32 vdec_fallback_alloc(u32 size)
{
    static u32 bump = 0x0F700000u;
    u32 a = (bump + 127u) & ~127u;
    bump = a + ((size + 127u) & ~127u);
    return a;
}

/* Sequence header MPEG-2: 00 00 01 B3, poi 12 bit larghezza, 12 bit altezza,
 * 4 bit aspect, 4 bit frame_rate_code, 18 bit bit_rate, 1 marker, 10 bit vbv. */
static void vdec_scan_seq_header(VdecSlot* v, u32 ea, u32 size)
{
    if (!ea || size < 12) return;
    for (u32 i = 0; i + 12 <= size && i < 4096; i++) {
        if (vm_read8(ea + i) == 0 && vm_read8(ea + i + 1) == 0 &&
            vm_read8(ea + i + 2) == 1 && vm_read8(ea + i + 3) == 0xB3) {
            u32 b4 = vm_read8(ea + i + 4), b5 = vm_read8(ea + i + 5), b6 = vm_read8(ea + i + 6);
            u32 b7 = vm_read8(ea + i + 7), b8 = vm_read8(ea + i + 8), b9 = vm_read8(ea + i + 9);
            u32 b10 = vm_read8(ea + i + 10);
            u32 w = (b4 << 4) | (b5 >> 4);
            u32 h = ((b5 & 0xF) << 8) | b6;
            v->aspect  = b7 >> 4;
            v->frc     = b7 & 0xF;
            v->bitrate = (b8 << 10) | (b9 << 2) | (b10 >> 6);
            v->vbv     = ((b10 & 0x1F) << 5) | (vm_read8(ea + i + 11) >> 3);
            if (w && h && w <= 1920 && h <= 1088) { v->width = (u16)w; v->height = (u16)h; }
            printf("[cellVdec] sequence header: %ux%u aspect=%u frc=%u\n", w, h, v->aspect, v->frc);
            return;
        }
    }
}

static void vdec_callback(VdecSlot* v, u32 handle, s32 msgType, s32 msgData)
{
    if (!v->cb_opd) return;
    ps3_invoke_guest(v->cb_opd, handle, (uint64_t)(uint32_t)msgType,
                     (uint64_t)(uint32_t)msgData, v->cb_arg, 0, 0, 0, 0);
}

s32 cellVdecQueryAttr(u32 type_ea, u32 attr_ea)
{
    u32 codec = type_ea ? vm_read32(type_ea) : 0;
    printf("[cellVdec] QueryAttr(codecType=%u)\n", codec);
    if (!type_ea || !attr_ea)
        return (s32)CELL_VDEC_ERROR_ARG;
    vm_write32(attr_ea + 0, 0x00010000u);     /* decoderVerUpper */
    vm_write32(attr_ea + 4, 0x00010000u);     /* decoderVerLower */
    vm_write32(attr_ea + 8, 0x00100000u);     /* memSize: 1 MB, ampio per i descrittori */
    vm_write8 (attr_ea + 12, 4);              /* cmdDepth */
    return CELL_OK;
}

s32 cellVdecOpen(u32 type_ea, u32 res_ea, u32 cb_ea, u32 handle_ea)
{
    u32 codec_type = type_ea ? vm_read32(type_ea) : 0;
    printf("[cellVdec] Open(codecType=%u res=0x%08X cb=0x%08X)\n", codec_type, res_ea, cb_ea);
    if (!type_ea || !handle_ea)
        return (s32)CELL_VDEC_ERROR_ARG;

    for (int i = 0; i < MAX_VDEC; i++) {
        VdecSlot* v = &s_vdec[i];
        if (v->in_use) continue;
        memset(v, 0, sizeof(*v));
        v->in_use    = 1;
        v->codecType = codec_type;
        if (cb_ea) { v->cb_opd = vm_read32(cb_ea + 0); v->cb_arg = vm_read32(cb_ea + 4); }
        if (res_ea) { v->mem_ea = vm_read32(res_ea + 0); v->mem_size = vm_read32(res_ea + 4); }
        u32 need = VDEC_PICRING * (VDEC_PICITEM_SZ + VDEC_PICINFO_SZ);
        if (v->mem_ea && v->mem_size >= need) v->ring_ea = (v->mem_ea + 127u) & ~127u;
        else v->ring_ea = vdec_fallback_alloc(need);
        vm_write32(handle_ea, (u32)i);
        printf("[cellVdec] Open -> handle=%d cbFunc=0x%08X cbArg=0x%08X mem=0x%08X+0x%X ring=0x%08X\n",
               i, v->cb_opd, v->cb_arg, v->mem_ea, v->mem_size, v->ring_ea);
        return CELL_OK;
    }
    return (s32)CELL_VDEC_ERROR_BUSY;
}

s32 cellVdecClose(CellVdecHandle handle)
{
    printf("[cellVdec] Close(handle=%u)\n", handle);
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    s_vdec[handle].in_use = 0;
    return CELL_OK;
}

s32 cellVdecStartSeq(CellVdecHandle handle)
{
    printf("[cellVdec] StartSeq(handle=%u)\n", handle);
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    s_vdec[handle].seqStarted = 1;
    s_vdec[handle].auCount = 0;
    s_vdec[handle].q_head = s_vdec[handle].q_tail = 0;
    return CELL_OK;
}

s32 cellVdecEndSeq(CellVdecHandle handle)
{
    printf("[cellVdec] EndSeq(handle=%u)\n", handle);
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    v->seqStarted = 0;
    /* la libreria vera svuota le immagini in coda e poi avvisa SEQDONE */
    vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, CELL_OK);
    return CELL_OK;
}

s32 cellVdecDecodeAu(CellVdecHandle handle, s32 mode, u32 au_ea)
{
    (void)mode;
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    if (!au_ea)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    if (!v->seqStarted)
        return (s32)CELL_VDEC_ERROR_SEQ;

    u32 startAddr = vm_read32(au_ea + 0), size = vm_read32(au_ea + 4);
    u32 pts_hi = vm_read32(au_ea + 8),  pts_lo = vm_read32(au_ea + 12);
    u32 dts_hi = vm_read32(au_ea + 16), dts_lo = vm_read32(au_ea + 20);
    u64 userData = vm_read64(au_ea + 24);

    { static int _n = 0; if (_n++ < 8 || (_n & 63) == 0)
        printf("[cellVdec] DecodeAu(handle=%u, addr=0x%08X, size=%u, pts=%u:%u) #%u\n",
               handle, startAddr, size, pts_hi, pts_lo, v->auCount + 1); }

    if (!v->width) vdec_scan_seq_header(v, startAddr, size);
    v->auCount++;

    /* 1. AU consumato */
    vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_AUDONE, CELL_OK);

    /* 2. Un'immagine per AU: il descrittore nella memoria del titolo. Se la
     * coda e' piena (il titolo non ritira), si sovrascrive il piu' vecchio. */
    if (v->q_tail - v->q_head >= VDEC_PICRING) v->q_head = v->q_tail - VDEC_PICRING + 1;
    u32 item = v->ring_ea + (v->q_tail % VDEC_PICRING) * (VDEC_PICITEM_SZ + VDEC_PICINFO_SZ);
    u32 info = item + VDEC_PICITEM_SZ;
    v->q_tail++;
    for (u32 o = 0; o < VDEC_PICITEM_SZ + VDEC_PICINFO_SZ; o += 4) vm_write32(item + o, 0);
    vm_write32(item + 0,  v->codecType);
    vm_write32(item + 4,  startAddr);
    vm_write32(item + 8,  size);
    vm_write8 (item + 12, (u8)v->auCount);
    vm_write32(item + 16, pts_hi); vm_write32(item + 20, pts_lo);      /* auPts[0] */
    vm_write32(item + 24, 0xFFFFFFFFu); vm_write32(item + 28, 0xFFFFFFFFu); /* auPts[1]: assente */
    vm_write32(item + 32, dts_hi); vm_write32(item + 36, dts_lo);      /* auDts[0] */
    vm_write32(item + 40, 0xFFFFFFFFu); vm_write32(item + 44, 0xFFFFFFFFu);
    vm_write64(item + 48, userData);
    vm_write64(item + 56, 0);
    vm_write32(item + 64, 0);            /* status: CELL_OK */
    vm_write32(item + 68, 0);            /* attr: CELL_VDEC_PICITEM_ATTR_NORMAL */
    vm_write32(item + 72, info);         /* picInfo_addr -> CellVdecMpeg2Info */
    u32 w = v->width ? v->width : 1280, h = v->height ? v->height : 720;
    vm_write16(info + 0, (u16)w);
    vm_write16(info + 2, (u16)h);
    vm_write32(info + 4, v->aspect ? v->aspect : 3);
    vm_write32(info + 8, v->frc ? v->frc : 4);
    vm_write32(info + 12, v->bitrate);
    vm_write32(info + 16, v->vbv);
    /* +20 constrained, +21 load_intra, +22 load_non_intra, +23 profile_and_level, +24 progressive_sequence */
    vm_write8 (info + 24, 1);

    vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_PICOUT, CELL_OK);
    return CELL_OK;
}

s32 cellVdecGetPicItem(CellVdecHandle handle, u32 picItem_ptr_ea)
{
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    if (v->q_head == v->q_tail)
        return (s32)CELL_VDEC_ERROR_EMPTY;
    u32 item = v->ring_ea + (v->q_head % VDEC_PICRING) * (VDEC_PICITEM_SZ + VDEC_PICINFO_SZ);
    v->q_head++;
    if (picItem_ptr_ea) vm_write32(picItem_ptr_ea, item);
    return CELL_OK;
}

/* Pixel dell'immagine nel formato chiesto: tutto nero (Y=16, Cb=Cr=128 nei
 * formati YUV; 0 con l'alpha chiesto nei formati RGB). */
s32 cellVdecGetPicture(CellVdecHandle handle, u32 format_ea, u32 out_ea)
{
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[handle];
    u32 fmt = format_ea ? vm_read32(format_ea + 0) : CELL_VDEC_PICFMT_YUV420_PLANAR;
    u8 alpha = format_ea ? vm_read8(format_ea + 8) : 0xFF;
    u32 w = v->width ? v->width : 1280, h = v->height ? v->height : 720;
    { static int _n = 0; if (_n++ < 4)
        printf("[cellVdec] GetPicture(handle=%u fmt=%u out=0x%08X) %ux%u\n", handle, fmt, out_ea, w, h); }
    if (!out_ea) return CELL_OK;
    uint8_t* p = vm_base + out_ea;
    switch (fmt) {
    case CELL_VDEC_PICFMT_YUV420_PLANAR:
        memset(p, 16, (size_t)w * h);
        memset(p + (size_t)w * h, 128, (size_t)(w / 2) * (h / 2) * 2);
        break;
    case CELL_VDEC_PICFMT_UYVY422_ILV:
        for (size_t i = 0; i < (size_t)w * h * 2; i += 2) { p[i] = 128; p[i + 1] = 16; }
        break;
    case CELL_VDEC_PICFMT_ARGB32_ILV:
        for (size_t i = 0; i < (size_t)w * h * 4; i += 4) { p[i] = alpha; p[i + 1] = p[i + 2] = p[i + 3] = 0; }
        break;
    default: /* RGBA32 */
        for (size_t i = 0; i < (size_t)w * h * 4; i += 4) { p[i] = p[i + 1] = p[i + 2] = 0; p[i + 3] = alpha; }
        break;
    }
    return CELL_OK;
}

s32 cellVdecSetFrameRate(CellVdecHandle handle, u32 frameRateCode)
{
    printf("[cellVdec] SetFrameRate(handle=%u, code=%u)\n", handle, frameRateCode);
    if (handle >= MAX_VDEC || !s_vdec[handle].in_use)
        return (s32)CELL_VDEC_ERROR_ARG;
    return CELL_OK;
}

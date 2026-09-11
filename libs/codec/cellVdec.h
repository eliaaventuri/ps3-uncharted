/*
 * ps3recomp - cellVdec HLE
 *
 * Video decoder (MPEG-2 / H.264): the title feeds access units with
 * cellVdecDecodeAu and gets AUDONE/PICOUT callbacks, then fetches the picture
 * with cellVdecGetPicItem (descriptor in guest memory) and cellVdecGetPicture
 * (pixels in the requested format). Layouts and constants follow the SDK as
 * documented by RPCS3's cellVdec.h; the guest structs are big-endian and are
 * read/written field by field through vm_read/vm_write, never dereferenced.
 *
 * No real decoder is linked: every AU yields one black picture of the size
 * declared by the MPEG-2 sequence header. Good enough for logo/intro movies
 * that only gate on "the movie finished".
 */

#ifndef PS3RECOMP_CELL_VDEC_H
#define PS3RECOMP_CELL_VDEC_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define CELL_VDEC_ERROR_ARG            0x80610101
#define CELL_VDEC_ERROR_SEQ            0x80610102
#define CELL_VDEC_ERROR_BUSY           0x80610103
#define CELL_VDEC_ERROR_EMPTY          0x80610104
#define CELL_VDEC_ERROR_AU             0x80610105
#define CELL_VDEC_ERROR_PIC            0x80610106
#define CELL_VDEC_ERROR_FATAL          0x80610107

/* Codec types (SDK values: MPEG2 is 0, AVC is 1) */
#define CELL_VDEC_CODEC_TYPE_MPEG2     0
#define CELL_VDEC_CODEC_TYPE_AVC       1
#define CELL_VDEC_CODEC_TYPE_DIVX      5

/* Callback message types */
#define CELL_VDEC_MSG_TYPE_AUDONE      1
#define CELL_VDEC_MSG_TYPE_PICOUT      2
#define CELL_VDEC_MSG_TYPE_SEQDONE     3
#define CELL_VDEC_MSG_TYPE_ERROR       4

/* cellVdecGetPicture output formats */
#define CELL_VDEC_PICFMT_ARGB32_ILV    0
#define CELL_VDEC_PICFMT_RGBA32_ILV    1
#define CELL_VDEC_PICFMT_UYVY422_ILV   2
#define CELL_VDEC_PICFMT_YUV420_PLANAR 3

/* DecodeAu modes */
#define CELL_VDEC_DEC_MODE_NORMAL      0
#define CELL_VDEC_DEC_MODE_B_SKIP      1
#define CELL_VDEC_DEC_MODE_PB_SKIP     2

typedef u32 CellVdecHandle;

/* Guest layouts (offsets), big-endian:
 *   CellVdecType      +0 codecType  +4 profileLevel                      (8)
 *   CellVdecAttr      +0 decoderVerUpper +4 decoderVerLower +8 memSize +12 cmdDepth(u8)
 *   CellVdecResource  +0 memAddr +4 memSize +8 ppuThreadPriority +12 ppuThreadStackSize
 *                     +16 spuThreadPriority +20 numOfSpus                (24)
 *   CellVdecCb        +0 cbFunc (OPD) +4 cbArg
 *   CellCodecTimeStamp+0 upper +4 lower
 *   CellVdecAuInfo    +0 startAddr +4 size +8 pts(ts) +16 dts(ts) +24 userData(u64)
 *                     +32 codecSpecificData(u64)                          (40)
 *   CellVdecPicItem   +0 codecType +4 startAddr +8 size +12 auNum(u8) +16 auPts[2](ts)
 *                     +32 auDts[2](ts) +48 auUserData[2](u64) +64 status +68 attr
 *                     +72 picInfo_addr                                    (80)
 *   CellVdecPicFormat +0 formatType +4 colorMatrixType +8 alpha(u8)
 *   CellVdecMpeg2Info +0 horizontal_size(u16) +2 vertical_size(u16) +4 aspect_ratio_information
 *                     +8 frame_rate_code +12 bit_rate_value +16 vbv_buffer_size_value
 *                     +20 flags(u8 x5) ... (0x80 bytes reserved)
 */

/* Callback: u32 cbFunc(CellVdecHandle handle, s32 msgType, s32 msgData, void* cbArg). */

s32 cellVdecQueryAttr(u32 type_ea, u32 attr_ea);
s32 cellVdecOpen(u32 type_ea, u32 res_ea, u32 cb_ea, u32 handle_ea);
s32 cellVdecClose(CellVdecHandle handle);
s32 cellVdecStartSeq(CellVdecHandle handle);
s32 cellVdecEndSeq(CellVdecHandle handle);
s32 cellVdecDecodeAu(CellVdecHandle handle, s32 mode, u32 auInfo_ea);
s32 cellVdecGetPicture(CellVdecHandle handle, u32 format_ea, u32 out_ea);
s32 cellVdecGetPicItem(CellVdecHandle handle, u32 picItem_ptr_ea);
s32 cellVdecSetFrameRate(CellVdecHandle handle, u32 frameRateCode);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_CELL_VDEC_H */

/*
 * ps3recomp - RSX texture layout (see rsx_texture_layout.h)
 */
#include "rsx_texture_layout.h"

#include <stdlib.h>   /* getenv, atoi */
#include <string.h>   /* memcpy       */

u32 rsx_log2_ceil(u32 v)
{
    u32 l = 0;
    while ((1u << l) < v) l++;
    return l;
}

u32 rsx_swizzle_offset(u32 x, u32 y, u32 log2w, u32 log2h)
{
    u32 off = 0, shift = 0;
    while (log2w && log2h) {
        off |= (x & 1u) << shift; x >>= 1; shift++;
        off |= (y & 1u) << shift; y >>= 1; shift++;
        log2w--; log2h--;
    }
    off |= (x | y) << shift;     /* only one of x/y still has bits */
    return off;
}

void rsx_texture_layout(u32 rsx_fmt, u32 w, u32 h, rsx_tex_layout* out)
{
    if (!out) return;

    /* Format classes (base = fmt & 0x9F, masking off the LN/UN flag bits).
     * The LBP loading screen exercises all of them: 0x85 A8R8G8B8 (swizzled UI
     * art), 0x8B G8B8 (the 1024x2048 linear font atlas -- without it no text
     * renders at all), 0x86/87/88 DXT1/23/45 (512x512 detail and LUT layers
     * bound on every draw). */
    u32 basef = rsx_fmt & 0x9Fu;

    out->compressed     = 0;
    out->block_bytes    = 0;
    out->bytes_per_texel = 1;
    out->fmt            = RSX_TEXFMT_R8;

    switch (basef) {
    case 0x85: out->fmt = RSX_TEXFMT_R8G8B8A8; out->bytes_per_texel = 4; break;
    case 0x8B: out->fmt = RSX_TEXFMT_R8G8;     out->bytes_per_texel = 2; break;
    case 0x86: out->fmt = RSX_TEXFMT_BC1; out->compressed = 1; out->block_bytes = 8;  break;
    case 0x87: out->fmt = RSX_TEXFMT_BC2; out->compressed = 1; out->block_bytes = 16; break;
    case 0x88: out->fmt = RSX_TEXFMT_BC3; out->compressed = 1; out->block_bytes = 16; break;
    default:   break;   /* R8, one byte per texel -- the pre-existing fallback */
    }

    if (out->compressed) {
        /* Compressed data is stored as linear rows of 4x4 blocks, and is never
         * Morton-swizzled on RSX. */
        out->bytes_per_texel = 0;
        out->swizzled = 0;
        out->row_bytes = ((w + 3u) / 4u) * out->block_bytes;
        out->rows      = (h + 3u) / 4u;
    } else {
        /* Swizzled unless the LN bit is set. The hardware requires power-of-two
         * dimensions to swizzle, so a NPOT image is linear regardless. */
        out->swizzled  = !(rsx_fmt & 0x20u) &&
                         w && h &&
                         (w & (w - 1u)) == 0u && (h & (h - 1u)) == 0u;
        out->row_bytes = w * out->bytes_per_texel;
        out->rows      = h;
    }

    out->face_bytes = out->row_bytes * out->rows;
}

int rsx_texture_argb_is_rgba(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("TEX_RGBA");
        cached = e ? atoi(e) : 0;
    }
    return cached;
}

void rsx_texture_decode(void* dst, u32 dst_pitch,
                        const u8* src, u32 w, u32 h,
                        const rsx_tex_layout* tl, int argb_as_rgba)
{
    if (!dst || !src || !tl || !w || !h) return;

    u8* d = (u8*)dst;

    if (tl->compressed) {
        /* BC1/2/3 are bit-identical to DXT1/23/45, so the payload is copied
         * rather than converted -- only the row stride changes. */
        for (u32 y = 0; y < tl->rows; y++)
            memcpy(d + (size_t)y * dst_pitch,
                   src + (size_t)y * tl->row_bytes, tl->row_bytes);
        return;
    }

    /* Morton order interleaves the low bits of x and y, so a swizzled image has
     * to be walked texel by texel. A linear one is a straight row copy, which is
     * worth keeping as a separate path: it is the common case and by far the
     * faster one. */
    const u32 l2w = rsx_log2_ceil(w), l2h = rsx_log2_ceil(h);
    const u32 bpp = tl->bytes_per_texel;

    if (tl->fmt == RSX_TEXFMT_R8G8B8A8) {
        for (u32 y = 0; y < h; y++) {
            u8* drow = d + (size_t)y * dst_pitch;
            for (u32 x = 0; x < w; x++) {
                const u8* s = src + (size_t)(tl->swizzled
                                  ? rsx_swizzle_offset(x, y, l2w, l2h)
                                  : (size_t)y * w + x) * 4;
                if (argb_as_rgba) {
                    drow[x*4+0] = s[0]; drow[x*4+1] = s[1];
                    drow[x*4+2] = s[2]; drow[x*4+3] = s[3];
                } else {
                    /* guest A,R,G,B -> host R,G,B,A */
                    drow[x*4+0] = s[1]; drow[x*4+1] = s[2];
                    drow[x*4+2] = s[3]; drow[x*4+3] = s[0];
                }
            }
        }
        return;
    }

    /* R8G8 and R8: no channel reordering. G8B8's placement is done by the
     * sampler's component remap, not here. */
    for (u32 y = 0; y < h; y++) {
        u8* drow = d + (size_t)y * dst_pitch;
        if (tl->swizzled) {
            for (u32 x = 0; x < w; x++) {
                const u8* s = src + (size_t)rsx_swizzle_offset(x, y, l2w, l2h) * bpp;
                for (u32 b = 0; b < bpp; b++) drow[x * bpp + b] = s[b];
            }
        } else {
            memcpy(drow, src + (size_t)y * w * bpp, (size_t)w * bpp);
        }
    }
}

void rsx_texture_component_remap(u32 control1, u32 rsx_fmt, u8 out[4])
{
    if (!out) return;

    /* Source codes index the presented vector {A,R,G,B}. The uploaded resource
     * holds R,G,B,A at components 0..3, so A is component 3 and R,G,B are
     * 0,1,2 -- that is what lanes_argb says. G8B8 only has two real channels,
     * and the sampler presents them as {G,R,G,R}. */
    static const u8 lanes_argb[4] = {3, 0, 1, 2};
    static const u8 lanes_g8b8[4] = {1, 0, 1, 0};
    const u8* src2res = ((rsx_fmt & 0x9Fu) == 0x8Bu) ? lanes_g8b8 : lanes_argb;

    if (!(control1 & 0xFFFFu)) control1 = 0xAAE4u;   /* unset -> identity */

    /* i runs A, R, G, B -- the crossbar's field order, LSB first. */
    for (int i = 0; i < 4; i++) {
        u32 s  = (control1 >> (i * 2)) & 3u;
        u32 op = (control1 >> (8 + i * 2)) & 3u;
        out[i] = (op == 0) ? (u8)RSX_REMAP_ZERO
               : (op == 1) ? (u8)RSX_REMAP_ONE
                           : src2res[s];
    }
}

/* ---- decompressione BC1/BC2/BC3 ------------------------------------------
 * I blocchi DXT sono 4x4 texel. BC1: due colori 565 e due bit di indice per
 * texel. BC2: 8 byte di alpha a 4 bit, poi un blocco BC1. BC3: due estremi di
 * alpha a 8 bit con indici a 3 bit, poi un blocco BC1.
 *
 * Serve al backend Metal, che carica una RGBA8: senza, i blocchi finivano nella
 * texture cosi' com'erano e a schermo erano rumore. Il percorso di
 * rsx_texture_decode() non cambia -- chi carica BC nativo (D3D12) continua a
 * ricevere i blocchi intatti. -------------------------------------------- */
static void bc_color_block(const u8* b, u8 out[16][4])
{
    const u16 c0 = (u16)(b[0] | (b[1] << 8));
    const u16 c1 = (u16)(b[2] | (b[3] << 8));
    u8 pal[4][3];
    pal[0][0] = (u8)(((c0 >> 11) & 31) * 255 / 31);
    pal[0][1] = (u8)(((c0 >>  5) & 63) * 255 / 63);
    pal[0][2] = (u8)(( c0        & 31) * 255 / 31);
    pal[1][0] = (u8)(((c1 >> 11) & 31) * 255 / 31);
    pal[1][1] = (u8)(((c1 >>  5) & 63) * 255 / 63);
    pal[1][2] = (u8)(( c1        & 31) * 255 / 31);
    /* c0 > c1 sceglie i due colori interpolati; altrimenti uno solo, e il
     * quarto indice e' trasparente (e' cosi' che BC1 fa l'alpha a 1 bit). */
    const int four = (c0 > c1);
    for (int k = 0; k < 3; k++) {
        if (four) {
            pal[2][k] = (u8)((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (u8)((pal[0][k] + 2 * pal[1][k]) / 3);
        } else {
            pal[2][k] = (u8)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    const u32 bits = (u32)b[4] | ((u32)b[5] << 8) | ((u32)b[6] << 16) | ((u32)b[7] << 24);
    for (int t = 0; t < 16; t++) {
        const u32 idx = (bits >> (t * 2)) & 3u;
        out[t][0] = pal[idx][0];
        out[t][1] = pal[idx][1];
        out[t][2] = pal[idx][2];
        out[t][3] = (!four && idx == 3) ? 0 : 255;
    }
}

void rsx_texture_decode_bc(void* dst, u32 dst_pitch,
                           const void* src, u32 w, u32 h,
                           const rsx_tex_layout* tl)
{
    if (!dst || !src || !tl || !tl->compressed) return;
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    const u32 bw = (w + 3) / 4;               /* blocchi per riga */

    for (u32 by = 0; by < tl->rows; by++) {
        const u8* row = s + (size_t)by * tl->row_bytes;
        for (u32 bx = 0; bx < bw; bx++) {
            const u8* blk = row + (size_t)bx * tl->block_bytes;
            u8 texel[16][4];
            u8 alpha[16];
            int have_alpha = 0;

            if (tl->fmt == RSX_TEXFMT_BC1) {
                bc_color_block(blk, texel);
            } else {
                bc_color_block(blk + 8, texel);
                have_alpha = 1;
                if (tl->fmt == RSX_TEXFMT_BC2) {
                    for (int t = 0; t < 16; t++) {
                        const u8 nib = (u8)((blk[t / 2] >> ((t & 1) * 4)) & 0xF);
                        alpha[t] = (u8)(nib * 17);
                    }
                } else {                       /* BC3 */
                    u8 a[8];
                    a[0] = blk[0]; a[1] = blk[1];
                    if (a[0] > a[1]) {
                        for (int k = 1; k < 7; k++)
                            a[k + 1] = (u8)(((7 - k) * a[0] + k * a[1]) / 7);
                    } else {
                        for (int k = 1; k < 5; k++)
                            a[k + 1] = (u8)(((5 - k) * a[0] + k * a[1]) / 5);
                        a[6] = 0; a[7] = 255;
                    }
                    unsigned long long bits = 0;
                    for (int k = 0; k < 6; k++)
                        bits |= (unsigned long long)blk[2 + k] << (8 * k);
                    for (int t = 0; t < 16; t++)
                        alpha[t] = a[(bits >> (t * 3)) & 7u];
                }
            }

            for (int ty = 0; ty < 4; ty++) {
                const u32 py = by * 4 + (u32)ty;
                if (py >= h) break;
                u8* drow = d + (size_t)py * dst_pitch;
                for (int tx = 0; tx < 4; tx++) {
                    const u32 px = bx * 4 + (u32)tx;
                    if (px >= w) break;
                    const int t = ty * 4 + tx;
                    u8* o = drow + (size_t)px * 4;
                    o[0] = texel[t][0];
                    o[1] = texel[t][1];
                    o[2] = texel[t][2];
                    o[3] = have_alpha ? alpha[t] : texel[t][3];
                }
            }
        }
    }
}

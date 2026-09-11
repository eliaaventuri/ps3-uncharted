/*
 * ps3recomp - RSX -> Metal backend (macOS / Apple Silicon)
 *
 * The first non-Windows render path in the project. It implements the subset of
 * the rsx_backend vtable needed to drive a guest clear/flip loop: `clear`
 * captures the colour written by NV4097_CLEAR_SURFACE, `set_render_target`
 * tracks the guest's surface dimensions, and present paints a drawable with it.
 * The remaining callbacks stay NULL -- every dispatch site in rsx_commands.c is
 * guarded (`if (s_backend && s_backend->x)`), so a partial vtable is the
 * intended way to bring a backend up incrementally.
 *
 * Why Metal rather than SDL_Renderer: SDL2's renderer is a 2D sprite API with
 * no route to a custom vertex program, depth/stencil, MRT or render-to-texture,
 * so it cannot grow into the real pipeline. The shader path this backend will
 * need is already proven end to end -- the existing rsx_fp/vp decompilers emit
 * HLSL, glslang lowers it to SPIR-V, spirv-cross emits MSL, and
 * -newLibraryWithSource: compiles it at runtime with no full Xcode install.
 */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#if !TARGET_OS_IPHONE
#  import <AppKit/AppKit.h>
#endif

#include "rsx_commands.h"
#include "rsx_metal_backend.h"
#include "rsx_vertex_fetch.h"
#include "rsx_primitives.h"
#include "rsx_fp_decompiler.h"   /* guest fragment program -> MSL */
#include "rsx_texture_layout.h"  /* swizzle/format decode, shared with D3D12 */
#include "cellGcmSys.h"          /* cellGcmResolveLocated */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------------- */

static id<MTLDevice>       s_dev;
static id<MTLCommandQueue> s_queue;
static CAMetalLayer*       s_layer;      /* windowed only   */
static id<MTLTexture>      s_offscreen;  /* headless only   */
static NSWindow*           s_window;
static int                 s_headless;
static int                 s_closed;
static int                 s_ready;
static u32                 s_width  = 1280;
static u32                 s_height = 720;

/* ---- draw recording -------------------------------------------------------
 * RSX draws arrive while the guest builds its frame; the flip comes later. So
 * draws are recorded with their vertices already fetched out of guest memory,
 * then replayed inside a single render pass at present time. This mirrors the
 * D3D12 backend's D3D12DrawRecord / render_frame split.
 * --------------------------------------------------------------------------*/

/* Misurato: un fotogramma del titolo arriva a ~10.400 quad in modalita'
 * immediata, e con il tetto a 4096 ne buttavamo 6.286 -- cioe' meta' immagine.
 * I draw sono record piccoli, il costo di alzare il tetto e' memoria statica. */
#define MTL_MAX_DRAWS      32768
#define MTL_MAX_VERTS      (1024u * 1024u)
#define MTL_VB_INDEX       30      /* buffer(0) is the constant buffer */

/* Matches the built-in shader's stage_in layout. */
typedef struct { float pos[4]; float col[4]; float tc[4]; } MtlVertex;

typedef struct {
    u32 base;        /* first vertex in s_verts               */
    u32 count;       /* vertex count after primitive expansion */
    MTLPrimitiveType topology;
    int blend_enable;
    u32 blend_sfactor, blend_dfactor, blend_equation;
    float mvp[16];   /* 4 rows of the RSX vertex-constant matrix */
    u32 fp_addr;     /* NV4097_SET_SHADER_PROGRAM: 0 = built-in shader */
    int tex0;        /* texture unit 0 enabled: pick the sampling built-in */
    u32 surf;        /* surface_color_offset[0] al momento del draw          */
    u32 sx, sy, sw, sh;  /* rettangolo di ritaglio (scissor) al momento del draw */
} MtlDraw;

static MtlVertex* s_verts;
static u32        s_vert_count;
static MtlDraw    s_draws[MTL_MAX_DRAWS];
static u32        s_draw_count;
static u32        s_dropped_draws;

static id<MTLLibrary> s_shader_lib;

/* Frames the CPU may run ahead of the GPU.
 *
 * Presenting used to end in -waitUntilCompleted unconditionally, which is the
 * one pattern where a translation layer or a driver cannot hide its encoding
 * work: the CPU stalls on every frame instead of preparing the next one.
 * Dolphin measured exactly this shape (their bounding-box path) as the single
 * largest CPU-side penalty in their Metal/MoltenVK comparison.
 *
 * Windowed presents now bound in-flight frames with a semaphore released from
 * the command buffer's completion handler, so the CPU keeps working while the
 * GPU drains. Headless still waits, because the readback has to observe a
 * finished frame -- there the stall is the point. */
#define MTL_MAX_INFLIGHT 3
static dispatch_semaphore_t s_inflight;

/* PSO cache. Keyed on the blend state, which is all the built-in shader path
 * varies; a guest-shader key (vp/fp ucode hash) joins it when that lands. */
typedef struct { u32 key; id<MTLRenderPipelineState> pso; } MtlPsoEntry;
static MtlPsoEntry s_pso_cache[64];
static u32         s_pso_count;

/* RSX clear colour, ARGB8888, as written by NV4097_SET_COLOR_CLEAR_VALUE. */
static u32 s_clear_argb = 0xFF000000u;
static u32 s_last_present_bgra;

/* ---- rsx_backend vtable -------------------------------------------------- */

static void mtl_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud; (void)depth; (void)stencil;
    /* 0xF0 is the colour-buffer mask; depth/stencil clears carry 0x03. */
    if (flags & 0xF0u) s_clear_argb = color;
}

/* The draw callbacks are not handed the state, so it is latched here. Every
 * state setter caches it; set_vertex_attribs in particular always fires before
 * a draw. The D3D12 backend does the same via s_d3d.current_rsx_state. */
static const rsx_state* s_state;

static void mtl_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    if (state) s_state = state;
    if (!state) return;
    u32 w = state->surface_clip_w, h = state->surface_clip_h;
    if (!w || !h || (w == s_width && h == s_height)) return;
    s_width = w; s_height = h;
    if (s_layer) s_layer.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
}

static void mtl_set_vertex_attribs(void* ud, const rsx_state* state)
{ (void)ud; if (state) s_state = state; }

static void mtl_set_blend(void* ud, const rsx_state* state)
{ (void)ud; if (state) s_state = state; }

static void mtl_set_viewport(void* ud, const rsx_state* state)
{ (void)ud; if (state) s_state = state; }

static void mtl_draw_arrays(void*, u32, u32, u32);
static void mtl_draw_indexed(void*, u32, u32, u32);
static void mtl_draw_inline(void*, u32, const float*, u32);
static void mtl_bind_texture(void*, u32, const rsx_texture_state*);

static rsx_backend s_backend_vtable = {
    .userdata          = NULL,
    .clear             = mtl_clear,
    .set_render_target = mtl_set_render_target,
    .set_vertex_attribs = mtl_set_vertex_attribs,
    .set_blend          = mtl_set_blend,
    .set_viewport       = mtl_set_viewport,
    .draw_arrays       = mtl_draw_arrays,
    .draw_indexed      = mtl_draw_indexed,
    .draw_inline       = mtl_draw_inline,
    .bind_texture      = mtl_bind_texture,
};

/* ---- helpers ------------------------------------------------------------- */

static MTLClearColor clear_color_from_rsx(void)
{
    /* ARGB8888 -> normalised RGBA. sRGB conversion is deliberately skipped:
     * the D3D12 backend treats the guest value as raw UNORM too, so both
     * backends agree pixel-for-pixel. */
    const double a = (double)((s_clear_argb >> 24) & 0xFF) / 255.0;
    const double r = (double)((s_clear_argb >> 16) & 0xFF) / 255.0;
    const double g = (double)((s_clear_argb >>  8) & 0xFF) / 255.0;
    const double b = (double)( s_clear_argb        & 0xFF) / 255.0;
    return MTLClearColorMake(r, g, b, a);
}

static int create_offscreen(void)
{
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:s_width
                                                          height:s_height
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    s_offscreen = [s_dev newTextureWithDescriptor:td];
    return s_offscreen ? 0 : -1;
}

#if !TARGET_OS_IPHONE
/* AppKit refuses to build an NSWindow off the main thread, and the guest calls
 * into the backend from whatever thread ran cellGcmInit. Hop to the main queue
 * when needed -- which only works because the host now keeps the main thread
 * draining that queue (see boot_main.cpp on Apple platforms) instead of running
 * the guest on it. */
static int create_window_impl(const char* title);

static int create_window(const char* title)
{
    if ([NSThread isMainThread]) return create_window_impl(title);

    __block int rc = -1;
    dispatch_sync(dispatch_get_main_queue(), ^{ rc = create_window_impl(title); });
    return rc;
}

static int create_window_impl(const char* title)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect frame = NSMakeRect(0, 0, (CGFloat)s_width, (CGFloat)s_height);
    s_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!s_window) return -1;

    [s_window setTitle:[NSString stringWithUTF8String:(title ? title : "ps3recomp")]];
    [s_window center];

    s_layer = [CAMetalLayer layer];
    s_layer.device          = s_dev;
    s_layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    /* RSX_DUMP_FRAME=<n> deve poter rileggere il drawable per salvarlo. */
    s_layer.framebufferOnly = getenv("RSX_DUMP_FRAME") ? NO : YES;
    s_layer.drawableSize    = CGSizeMake((CGFloat)s_width, (CGFloat)s_height);

    NSView* view = [s_window contentView];
    [view setWantsLayer:YES];
    [view setLayer:s_layer];

    [s_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return 0;
}
#endif

/* ---- guest vertex fetch --------------------------------------------------
 * Was a port of the D3D12 backend's read_vp_vertex, kept here as a second
 * copy. It had already drifted from the original in two ways that matter --
 * it fed caller literals rather than the constant vertex attribute register
 * for a disabled array, and it resolved LOCAL offsets through the IO table --
 * so both copies are now gone and rsx_vertex_fetch.c holds the one definition.
 * --------------------------------------------------------------------------*/

/* Position is attrib 0, diffuse colour attrib 3, texcoord0 attrib 8 -- the same
 * slots the D3D12 fallback path assumes. */
static void fetch_vertex(const rsx_state* st, u32 vi, MtlVertex* out)
{
    rsx_fetch_attrib(st, 0, vi, out->pos);
    rsx_fetch_attrib(st, 3, vi, out->col);
    rsx_fetch_attrib(st, 8, vi, out->tc);
}

/* ---- primitive conversion -------------------------------------------------
 * Metal, like D3D12, has no quads, quad strips, triangle fans or polygons, so
 * those are expanded to triangle lists on the CPU while the vertices are being
 * fetched. Everything else maps directly.
 * --------------------------------------------------------------------------*/

/* Which primitives need CPU expansion, and what they end up drawn as, both
 * come from rsx_primitives.h now -- this file used to answer the first
 * question with its own list, which disagreed with the one in that header
 * about LINE_LOOP and POLYGON. Only the Metal enum mapping stays here. */
static MTLPrimitiveType topo_to_metal(rsx_topology t)
{
    switch (t) {
    case RSX_TOPOLOGY_POINTS:         return MTLPrimitiveTypePoint;
    case RSX_TOPOLOGY_LINES:          return MTLPrimitiveTypeLine;
    case RSX_TOPOLOGY_LINE_STRIP:     return MTLPrimitiveTypeLineStrip;
    case RSX_TOPOLOGY_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
    default:                          return MTLPrimitiveTypeTriangle;
    }
}

/* Emit `count` vertices starting at `first`, expanding fans/quads/polygons.
 * `resolve` maps a sequence position to a guest vertex index, so the same code
 * serves both array and indexed draws. Returns vertices written. */
typedef u32 (*IndexResolver)(const rsx_state*, u32 /*seq*/, void* /*ctx*/);

static u32 emit_vertices(const rsx_state* st, u32 prim, u32 count,
                         IndexResolver resolve, void* ctx)
{
    u32 wrote = 0;
    #define PUSH(seq) do {                                                     \
        if (s_vert_count >= MTL_MAX_VERTS) return wrote;                       \
        fetch_vertex(st, resolve(st, (seq), ctx), &s_verts[s_vert_count++]);   \
        wrote++;                                                               \
    } while (0)

    if (prim == RSX_PRIMITIVE_QUADS) {
        for (u32 q = 0; q + 3 < count; q += 4) {
            PUSH(q); PUSH(q+1); PUSH(q+2);
            PUSH(q); PUSH(q+2); PUSH(q+3);
        }
    } else if (prim == RSX_PRIMITIVE_QUAD_STRIP) {
        for (u32 q = 0; q + 3 < count; q += 2) {
            PUSH(q); PUSH(q+1); PUSH(q+2);
            PUSH(q+1); PUSH(q+3); PUSH(q+2);
        }
    } else if (prim == RSX_PRIMITIVE_TRIANGLE_FAN || prim == RSX_PRIMITIVE_POLYGON) {
        for (u32 t = 1; t + 1 < count; t++) { PUSH(0); PUSH(t); PUSH(t+1); }
    } else {
        for (u32 v = 0; v < count; v++) PUSH(v);
    }
    #undef PUSH
    return wrote;
}

static u32 resolve_linear(const rsx_state* st, u32 seq, void* ctx)
{
    (void)st; return *(u32*)ctx + seq;
}

static u32 resolve_indexed(const rsx_state* st, u32 seq, void* ctx)
{
    u32 base = *(u32*)ctx;
    /* index_array_offset bits [7:4] select the type: 0 = u32, 1 = u16. */
    int u16type = ((st->index_array_offset >> 4) & 0xFu) == 1;
    u32 off = st->index_array_offset & ~0xFFu;
    u32 ea  = cellGcmResolveOffset(off) + (base + seq) * (u16type ? 2u : 4u);
    if (!vm_base) return 0;
    const u8* p = vm_base + ea;
    return u16type ? (u32)((p[0] << 8) | p[1])
                   : (u32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

/* ---- draw recording ------------------------------------------------------ */

static void record_draw(const rsx_state* st, u32 prim, u32 base, u32 count,
                        IndexResolver resolve)
{
    if (!s_ready || !s_verts || !st || count == 0) return;
    if (s_draw_count >= MTL_MAX_DRAWS) { s_dropped_draws++; return; }

    u32 first_vert = s_vert_count;
    u32 wrote = emit_vertices(st, prim, count, resolve, &base);
    if (wrote == 0) { s_dropped_draws++; return; }

    MtlDraw* d = &s_draws[s_draw_count++];
    d->base  = first_vert;
    d->count = wrote;
    /* Expanded primitives always come out as a triangle list. */
    d->topology = rsx_primitive_needs_expansion(prim) ? MTLPrimitiveTypeTriangle
                                             : topo_to_metal(rsx_primitive_topology(prim));
    d->blend_enable   = st->blend_enable;
    d->blend_sfactor  = st->blend_sfactor;
    d->blend_dfactor  = st->blend_dfactor;
    d->blend_equation = st->blend_equation;
    d->fp_addr        = st->shader_program;
    d->tex0           = (st->textures[0].control0 & 0x80000000u) != 0;
    d->surf           = st->surface_color_offset[0];
    d->sx = st->scissor_x; d->sy = st->scissor_y;
    d->sw = st->scissor_w; d->sh = st->scissor_h;

    /* RSX vertex constant slots 0..3 hold the MVP rows when no vertex program
     * has been translated. An all-zero matrix would collapse every vertex to
     * the origin, so fall back to identity. */
    int nonzero = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float v = st->vertex_constants[r][c];
            d->mvp[r * 4 + c] = v;
            if (v != 0.0f) nonzero = 1;
        }
    if (!nonzero) {
        memset(d->mvp, 0, sizeof(d->mvp));
        d->mvp[0] = d->mvp[5] = d->mvp[10] = d->mvp[15] = 1.0f;
    }
}

static void mtl_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    record_draw(s_state, primitive, first, count, resolve_linear);
}

static void mtl_draw_indexed(void* ud, u32 primitive, u32 index_offset, u32 count)
{
    (void)ud;
    record_draw(s_state, primitive, index_offset, count, resolve_indexed);
}

/* Modalita' immediata: i vertici non stanno in un vertex buffer del guest, li
 * ha appena consegnati rsx_commands.c come array contiguo di
 * RSX_MAX_VERTEX_ATTRIBS x 4 float ciascuno. L'espansione delle primitive e'
 * la stessa di emit_vertices (Metal non ha quad ne' fan); la differenza e' solo
 * da dove arrivano i numeri.
 *
 * La matrice resta l'IDENTITA': le posizioni misurate arrivano gia' in
 * coordinate normalizzate (-1..1), perche' il vertex program del titolo per
 * questi passaggi e' un pass-through. RSX_INLINE_MVP=1 applica comunque le
 * costanti 0..3, per il confronto A/B. */
static void mtl_draw_inline(void* ud, u32 primitive, const float* verts, u32 count)
{
    (void)ud;
    if (!s_ready || !s_verts || !verts || count == 0) return;
    if (s_draw_count >= MTL_MAX_DRAWS) { s_dropped_draws++; return; }

    const u32 STRIDE = RSX_MAX_VERTEX_ATTRIBS * 4;
    u32 first_vert = s_vert_count, wrote = 0;

    /* Quale attributo porta cosa. Il titolo scrive attr0 (posizione, 2F),
     * attr1 e attr2; RSX_INLINE_MAP=col,tc rimappa senza ricompilare mentre si
     * capisce quale sia il colore e quale la coordinata di texture. */
    static int s_col = -1, s_tc = -1;
    if (s_col < 0) {
        const char* e = getenv("RSX_INLINE_MAP");
        s_col = 2; s_tc = 1;
        if (e && *e) { int a = 0, b = 0;
            if (sscanf(e, "%d,%d", &a, &b) == 2) { s_col = a; s_tc = b; } }
        if (s_col >= (int)RSX_MAX_VERTEX_ATTRIBS) s_col = 2;
        if (s_tc  >= (int)RSX_MAX_VERTEX_ATTRIBS) s_tc  = 1;
    }

    #define IPUSH(seq) do {                                                    \
        if (s_vert_count >= MTL_MAX_VERTS) goto done;                          \
        const float* a = &verts[(seq) * STRIDE];                               \
        MtlVertex* o = &s_verts[s_vert_count++];                               \
        memcpy(o->pos, &a[0],           sizeof o->pos);                        \
        memcpy(o->col, &a[s_col * 4],   sizeof o->col);                        \
        memcpy(o->tc,  &a[s_tc  * 4],   sizeof o->tc);                         \
        wrote++;                                                               \
    } while (0)

    if (primitive == RSX_PRIMITIVE_QUADS) {
        for (u32 q = 0; q + 3 < count; q += 4) {
            IPUSH(q); IPUSH(q+1); IPUSH(q+2);
            IPUSH(q); IPUSH(q+2); IPUSH(q+3);
        }
    } else if (primitive == RSX_PRIMITIVE_QUAD_STRIP) {
        for (u32 q = 0; q + 3 < count; q += 2) {
            IPUSH(q); IPUSH(q+1); IPUSH(q+2);
            IPUSH(q+1); IPUSH(q+3); IPUSH(q+2);
        }
    } else if (primitive == RSX_PRIMITIVE_TRIANGLE_FAN || primitive == RSX_PRIMITIVE_POLYGON) {
        for (u32 t = 1; t + 1 < count; t++) { IPUSH(0); IPUSH(t); IPUSH(t+1); }
    } else {
        for (u32 v = 0; v < count; v++) IPUSH(v);
    }
done:
    #undef IPUSH
    if (wrote == 0) { s_dropped_draws++; return; }

    MtlDraw* d = &s_draws[s_draw_count++];
    d->base  = first_vert;
    d->count = wrote;
    d->topology = rsx_primitive_needs_expansion(primitive) ? MTLPrimitiveTypeTriangle
                : topo_to_metal(rsx_primitive_topology(primitive));
    d->blend_enable   = s_state ? s_state->blend_enable   : 0;
    d->blend_sfactor  = s_state ? s_state->blend_sfactor  : 0;
    d->blend_dfactor  = s_state ? s_state->blend_dfactor  : 0;
    d->blend_equation = s_state ? s_state->blend_equation : 0;
    d->fp_addr        = s_state ? s_state->shader_program : 0;
    d->tex0           = s_state && (s_state->textures[0].control0 & 0x80000000u) != 0;
    d->surf           = s_state ? s_state->surface_color_offset[0] : 0;
    d->sx = s_state ? s_state->scissor_x : 0; d->sy = s_state ? s_state->scissor_y : 0;
    d->sw = s_state ? s_state->scissor_w : 4096; d->sh = s_state ? s_state->scissor_h : 4096;

    memset(d->mvp, 0, sizeof d->mvp);
    d->mvp[0] = d->mvp[5] = d->mvp[10] = d->mvp[15] = 1.0f;
    { static int im = -1;
      if (im < 0) im = getenv("RSX_INLINE_MVP") ? 1 : 0;
      if (im && s_state)
          for (int r = 0; r < 4; r++)
              for (int c = 0; c < 4; c++)
                  d->mvp[r * 4 + c] = s_state->vertex_constants[r][c]; }

    { static int dbg = -1;
      if (dbg < 0) dbg = getenv("RSX_INLINE_DBG") ? 1 : 0;
      static unsigned long n = 0;
      if (dbg && (++n <= 6)) {
          fprintf(stderr, "[mtl inline] #%lu prim=%u in=%u out=%u topo=%d tex0=%d blend=%d\n",
                  n, primitive, count, wrote, (int)d->topology, d->tex0, d->blend_enable);
          for (u32 v = 0; v < count && v < 4; v++) {
              const float* a = &verts[v * STRIDE];
              fprintf(stderr, "   v%u attr0=(%.3f %.3f %.3f %.3f) attr1=(%.3f %.3f %.3f %.3f) "
                              "attr2=(%.3f %.3f %.3f %.3f)\n", v,
                      a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                      a[8], a[9], a[10], a[11]);
          }
          fflush(stderr);
      } }
}

/* ---- superfici fuori schermo (render-to-texture) --------------------------
 * Il titolo non disegna solo sul buffer di schermo: alterna passaggi su una
 * superficie fuori schermo e passaggi sul buffer visibile (misurato: 4599 draw
 * su 0x7D0000 [offscreen] e 4599 su 0x50000 [display], nello stesso
 * fotogramma). Mandando tutto sul drawable i passaggi fuori schermo
 * CANCELLAVANO l'immagine: restava visibile solo l'ultimo disegno, e lo
 * schermo era nero.
 *
 * Qui ogni offset di superficie non-display prende la sua texture, e i draw
 * vengono raggruppati per superficie in passaggi di rendering distinti. La
 * texture resta viva fra un fotogramma e l'altro: il titolo ci disegna dentro
 * una volta e la campiona dopo. -------------------------------------------*/
typedef struct { u32 offset; u32 w, h; id<MTLTexture> tex; int cleared_frame; } MtlSurface;
static MtlSurface s_surf[16];
static id<MTLTexture> s_surf_dump[16];
static u32        s_surf_count;
static unsigned long s_frame_no;

static id<MTLTexture> surface_texture(u32 offset, u32 w, u32 h, int* first_use)
{
    for (u32 i = 0; i < s_surf_count; i++) {
        if (s_surf[i].offset != offset) continue;
        if (s_surf[i].w != w || s_surf[i].h != h) { s_surf[i].tex = nil; break; }
        if (!s_surf[i].tex) break;
        *first_use = (s_surf[i].cleared_frame != (int)s_frame_no);
        s_surf[i].cleared_frame = (int)s_frame_no;
        return s_surf[i].tex;
    }
    if (s_surf_count >= 16) return nil;
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> t = [s_dev newTextureWithDescriptor:td];
    if (!t) return nil;
    u32 slot = s_surf_count;
    for (u32 i = 0; i < s_surf_count; i++) if (s_surf[i].offset == offset) { slot = i; break; }
    if (slot == s_surf_count) s_surf_count++;
    s_surf[slot].offset = offset; s_surf[slot].w = w; s_surf[slot].h = h;
    s_surf[slot].tex = t; s_surf[slot].cleared_frame = (int)s_frame_no;
    *first_use = 1;
    { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0;
      if (sd) fprintf(stderr, "[RSX metal] superficie fuori schermo 0x%X: %ux%u\n",
                      offset, w, h); }
    return t;
}

/* La texture di una superficie su cui abbiamo gia' disegnato, se il titolo la
 * ricampiona (il passaggio di composizione). La memoria guest per quella
 * superficie non contiene nulla -- la RSX vera ci avrebbe scritto, noi no --
 * quindi decodificarla darebbe nero. */
static id<MTLTexture> surface_texture_lookup(u32 offset)
{
    for (u32 i = 0; i < s_surf_count; i++)
        if (s_surf[i].offset == offset && s_surf[i].tex) return s_surf[i].tex;
    return nil;
}

/* ---- pipeline state ------------------------------------------------------ */

static MTLBlendFactor blend_factor_to_metal(u32 f)
{
    switch (f) {
    case 0x0000: return MTLBlendFactorZero;
    case 0x0001: return MTLBlendFactorOne;
    case 0x0300: return MTLBlendFactorSourceColor;
    case 0x0301: return MTLBlendFactorOneMinusSourceColor;
    case 0x0302: return MTLBlendFactorSourceAlpha;
    case 0x0303: return MTLBlendFactorOneMinusSourceAlpha;
    case 0x0304: return MTLBlendFactorDestinationAlpha;
    case 0x0305: return MTLBlendFactorOneMinusDestinationAlpha;
    case 0x0306: return MTLBlendFactorDestinationColor;
    case 0x0307: return MTLBlendFactorOneMinusDestinationColor;
    case 0x0308: return MTLBlendFactorSourceAlphaSaturated;
    default:     return MTLBlendFactorOne;
    }
}

static MTLBlendOperation blend_equation_to_metal(u32 e)
{
    switch (e) {
    case 0x8007: return MTLBlendOperationMin;
    case 0x8008: return MTLBlendOperationMax;
    case 0x800A: return MTLBlendOperationSubtract;
    case 0x800B: return MTLBlendOperationReverseSubtract;
    default:     return MTLBlendOperationAdd;
    }
}

/* Built-in shaders: transform by the RSX vertex-constant matrix and interpolate
 * the diffuse colour. The rows are dotted explicitly rather than using float4x4
 * so there is no column-major/row-major ambiguity with the guest's layout.
 * The translated-guest-shader path replaces this, it does not extend it. */
static NSString* const kBuiltinMSL = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VIn  { float4 pos [[attribute(0)]]; float4 col [[attribute(1)]]; float4 tc [[attribute(2)]]; };\n"
"struct VOut { float4 pos [[position]]; float4 col; float4 tc; };\n"
"struct VU   { float4 mvp[4]; };\n"
"vertex VOut vs_main(VIn v [[stage_in]], constant VU& u [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos = float4(dot(u.mvp[0], v.pos), dot(u.mvp[1], v.pos),\n"
"                   dot(u.mvp[2], v.pos), dot(u.mvp[3], v.pos));\n"
"    o.col = v.col; o.tc = v.tc;\n"
"    return o;\n"
"}\n"
"fragment float4 fs_main(VOut in [[stage_in]]) { return in.col; }\n"
"fragment float4 fs_tint(VOut in [[stage_in]]) { return float4(1.0, 0.2, 0.6, 1.0); }\n"
/* Come fs_tex ma con la stessa dichiarazione ad ARRAY che usa il fragment
 * program tradotto del guest. Se questa e' nera e fs_tex no, il difetto e'
 * nel legame delle texture ad array, non nel programma del titolo. */
"fragment float4 fs_tex_arr(VOut in [[stage_in]],\n"
"                           array<texture2d<float>, 16> t [[texture(0)]],\n"
"                           array<sampler, 16> s [[sampler(0)]])\n"
"{ return t[0].sample(s[0], in.tc.xy); }\n"
"fragment float4 fs_tex(VOut in [[stage_in]], texture2d<float> t0 [[texture(0)]],\n"
"                       sampler s0 [[sampler(0)]]) { return t0.sample(s0, in.tc.xy); }\n";

/* Appended to a decompiled guest fragment program. The decompiler emits the
 * PSInput struct and rsx_fp_main; this supplies the vertex stage that fills
 * PSInput from the same MtlVertex layout the built-in path uses. Interpolants
 * the fetch does not produce yet are zeroed rather than left undefined. */
static NSString* const kGuestVertexMSL = @
"struct VIn { float4 pos [[attribute(0)]]; float4 col [[attribute(1)]]; float4 tc [[attribute(2)]]; };\n"
"struct VU  { float4 mvp[4]; };\n"
"vertex PSInput vs_guest(VIn v [[stage_in]], constant VU& u [[buffer(0)]]) {\n"
"    PSInput o;\n"
"    o.position = float4(dot(u.mvp[0], v.pos), dot(u.mvp[1], v.pos),\n"
"                        dot(u.mvp[2], v.pos), dot(u.mvp[3], v.pos));\n"
"    o.col0 = v.col; o.col1 = v.col; o.fog = float4(0);\n"
"    o.tc0 = v.tc; o.tc1 = float4(0); o.tc2 = float4(0); o.tc3 = float4(0);\n"
"    o.tc4 = float4(0); o.tc5 = float4(0); o.tc6 = float4(0); o.tc7 = float4(0);\n"
"    return o;\n"
"}\n";

/* One MTLTexture per unit, rebuilt when the guest rebinds a different image.
 * The swizzle/format decode is rsx_texture_layout's, shared with D3D12, so
 * this only owns the upload. */
typedef struct {
    id<MTLTexture> tex;
    u32 offset, format, image_rect;   /* what is currently resident */
} MtlTexUnit;
static MtlTexUnit s_tex[RSX_MAX_TEXTURES];
static id<MTLSamplerState> s_sampler;
/* Il fragment program tradotto del guest dichiara le texture come ARRAY
 * (`array<texture2d<float>, 16> rsx_tex [[texture(0)]]`), e Metal vuole TUTTI
 * gli elementi dell'array legati: legando solo le unita' residenti, ogni
 * lettura dell'array tornava zero e l'immagine era nera. Misurato con uno
 * shader incorporato identico a fs_tex ma dichiarato ad array: 0 pixel non
 * neri, contro 2058 della versione scalare. Questa 1x1 riempie i buchi. */
static id<MTLTexture> s_dummy_tex;

extern u32 ppu_vm_size;   /* guest VM size the port declared; 0 = unknown */

static void mtl_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex)
{
    (void)ud;
    if (!s_ready || !vm_base || unit >= RSX_MAX_TEXTURES || !tex) return;

    const u32 width  = (tex->image_rect >> 16) & 0xFFFFu;
    const u32 height =  tex->image_rect        & 0xFFFFu;
    const u32 fmt    = (tex->format >> 8) & 0xFFu;
    if (width == 0 || height == 0) return;

    MtlTexUnit* u = &s_tex[unit];
    if (u->tex && u->offset == tex->offset && u->format == tex->format &&
        u->image_rect == tex->image_rect)
        return;                            /* already resident */

    /* Se questo offset e' una superficie su cui abbiamo disegnato fuori
     * schermo, il contenuto vero sta nella NOSTRA texture: in memoria guest non
     * c'e' niente, perche' a scriverci sarebbe stata la RSX. Senza questo il
     * passaggio di composizione campiona nero e tutto il lavoro fuori schermo
     * va perso. */
    { id<MTLTexture> st = surface_texture_lookup(tex->offset);
      if (st) {
          u->tex = st; u->offset = tex->offset; u->format = tex->format;
          u->image_rect = tex->image_rect;
          { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0;
            static int _n = 0;
            if (sd && _n++ < 8)
                fprintf(stderr, "[RSX metal] unita' %u legge la superficie "
                        "fuori schermo 0x%X\n", unit, tex->offset); }
          return;
      } }

    rsx_tex_layout tl;
    rsx_texture_layout(fmt, width, height, &tl);

    /* Bit 31 of the OFFSET selects the context DMA (1 = MAIN, 0 = LOCAL), the
     * same convention rsx_vertex_fetch follows; the FORMAT's low two bits say
     * the same thing when the offset carries no flag. Resolving the wrong pool
     * lands outside the guest allocation entirely, so the range is checked
     * before the decode reads a single byte -- guest state that does not add
     * up must not fault the host. */
    const u32 raw  = tex->offset;
    const int main_mem = (raw & 0x80000000u) ? 1 : ((tex->format & 3u) != 1u);
    const u32 ea   = cellGcmResolveLocated(!main_mem, raw & 0x7FFFFFFFu);
    if (ppu_vm_size && (ea == 0 || tl.face_bytes == 0 ||
                        ea + tl.face_bytes > ppu_vm_size)) {
        static int warned = 0;
        if (warned++ < 8)
            fprintf(stderr, "[RSX metal] texture unit %u: offset 0x%08X resolves "
                            "to 0x%08X + %u bytes, outside guest memory -- skipped\n",
                    unit, raw, ea, tl.face_bytes);
        return;
    }

    const u32 pitch = width * 4u;
    u8* staging = (u8*)malloc((size_t)pitch * height);
    if (!staging) return;
    /* Un formato compresso va DECOMPRESSO: la texture Metal qui e' RGBA8, e
     * rsx_texture_decode() per i formati a blocchi copia i blocchi cosi' come
     * sono (che e' giusto per chi carica BC nativo, come D3D12). Copiandoli in
     * una RGBA8 si otteneva rumore rosso/verde/blu -- misurato: un riquadro di
     * 128x32 pixel di sale e pepe colorato al posto dell'HUD. */
    if (tl.compressed)
        rsx_texture_decode_bc(staging, pitch, vm_base + ea, width, height, &tl);
    else
        rsx_texture_decode(staging, pitch, vm_base + ea, width, height, &tl,
                           rsx_texture_argb_is_rgba());

    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width height:height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> mt = [s_dev newTextureWithDescriptor:td];
    if (mt) {
        [mt replaceRegion:MTLRegionMake2D(0, 0, width, height)
              mipmapLevel:0 withBytes:staging bytesPerRow:pitch];
        u->tex = mt;
        u->offset = tex->offset; u->format = tex->format;
        u->image_rect = tex->image_rect;
        fprintf(stderr, "[RSX metal] texture unit %u: %ux%u fmt=0x%02X\n",
                unit, width, height, fmt);
        /* RSX_TEX_DBG=1: quanti texel non neri sono davvero atterrati. Una
         * texture decodificata male e una texture vuota dal backend si vedono
         * uguali (nero), e da li' non si riparte. */
        /* RSX_TEX_SAVE=1: la texture decodificata su file. Se la geometria si
         * dipinge con una tinta ma resta nera con lo shader vero, o e' la
         * texture o sono le coordinate; questo separa i due casi. */
        { static int ts = -1;
          if (ts < 0) ts = getenv("RSX_TEX_SAVE") ? 1 : 0;
          static int tn = 0;
          if (ts && tn < 4) {
              char tp[96]; snprintf(tp, sizeof tp, "/tmp/rsx_tex%d_%08X.ppm", tn++, tex->offset);
              FILE* tf = fopen(tp, "wb");
              if (tf) {
                  fprintf(tf, "P6\n%u %u\n255\n", width, height);
                  for (u32 q = 0; q < width * height; q++) {
                      u8 rgb[3] = { staging[q*4+0], staging[q*4+1], staging[q*4+2] };
                      fwrite(rgb, 1, 3, tf);
                  }
                  fclose(tf);
                  fprintf(stderr, "[RSX metal] texture salvata in %s\n", tp);
              } } }
        { static int td2 = -1;
          if (td2 < 0) td2 = getenv("RSX_TEX_DBG") ? 1 : 0;
          if (td2) {
              unsigned long nz = 0, na = 0;
              const size_t n = (size_t)width * height;
              for (size_t i = 0; i < n; i++) {
                  const u8* p = staging + i * 4;
                  if (p[0] | p[1] | p[2]) nz++;
                  if (p[3]) na++;
              }
              fprintf(stderr, "[RSX metal]   texel non neri: %lu/%zu, alpha>0: %lu"
                              "  primi: %02X%02X%02X%02X %02X%02X%02X%02X\n",
                      nz, n, na, staging[0], staging[1], staging[2], staging[3],
                      staging[4], staging[5], staging[6], staging[7]);
          } }
    }
    free(staging);
}

/* One compiled library per guest program. A program that fails to decompile or
 * to compile is remembered as a miss (lib == nil) so it is not retried every
 * draw; that draw falls back to the built-in shader rather than dropping. */
typedef struct { u32 fp_addr; id<MTLLibrary> lib; } GuestFpEntry;
static GuestFpEntry s_fp_cache[64];
static u32          s_fp_count;

static id<MTLLibrary> guest_fp_library(u32 fp_addr)
{
    for (u32 i = 0; i < s_fp_count; i++)
        if (s_fp_cache[i].fp_addr == fp_addr) return s_fp_cache[i].lib;
    if (s_fp_count >= (u32)(sizeof(s_fp_cache) / sizeof(s_fp_cache[0]))) return nil;

    id<MTLLibrary> lib = nil;
    const u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (vm_base && off) {
        static char msl[32768];
        const int n = rsx_fp_decompile_lang(vm_base + off, 4096, RSX_FP_CTRL_AUTO,
                                            0u, 0, RSX_FP_LANG_MSL,
                                            msl, sizeof(msl), NULL);
        if (n > 0) {
            NSString* src = [NSString stringWithFormat:@"%s%@",
                             msl, kGuestVertexMSL];
            NSError* err = nil;
            lib = [s_dev newLibraryWithSource:src options:nil error:&err];
            if (!lib)
                fprintf(stderr, "[RSX metal] guest fp 0x%08X compile failed: %s\n",
                        fp_addr, [[err localizedDescription] UTF8String]);
            else {
                fprintf(stderr, "[RSX metal] guest fp 0x%08X: %d instructions\n",
                        fp_addr, n);
                /* RSX_FP_DUMP=1: l'MSL tradotto. Quando la geometria rasterizza
                 * (provato con la tinta) ma resta nera, la risposta e' qui. */
                { static int fd = -1;
                  if (fd < 0) fd = getenv("RSX_FP_DUMP") ? 1 : 0;
                  if (fd) fprintf(stderr, "---- MSL fp 0x%08X ----\n%s\n----\n",
                                  fp_addr, msl); }
            }
        } else {
            fprintf(stderr, "[RSX metal] guest fp 0x%08X: decompile failed\n", fp_addr);
        }
    }

    s_fp_cache[s_fp_count].fp_addr = fp_addr;
    s_fp_cache[s_fp_count].lib     = lib;
    s_fp_count++;
    return lib;
}

static u32 pso_key(const MtlDraw* d)
{
    /* The guest program is part of the key: two draws with identical blend
     * state but different fragment programs are different pipelines. */
    const u32 fp = d->fp_addr ? (1u | (d->fp_addr * 2654435761u)) : 0u;
    const u32 tx = d->tex0 ? 0x40000000u : 0u;
    if (!d->blend_enable) return (fp << 1) | tx;
    return 1u | ((d->blend_sfactor & 0xFFFu) << 1)
              | ((d->blend_dfactor & 0xFFFu) << 13)
              | ((d->blend_equation & 0x7u) << 25)
              | (fp & 0x80000000u) | tx;
}

static id<MTLRenderPipelineState> pso_for(const MtlDraw* d)
{
    u32 key = pso_key(d);
    for (u32 i = 0; i < s_pso_count; i++)
        if (s_pso_cache[i].key == key) return s_pso_cache[i].pso;
    if (s_pso_count >= (u32)(sizeof(s_pso_cache) / sizeof(s_pso_cache[0]))) return nil;

    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    for (int i = 0; i < 3; i++) {
        vd.attributes[i].format      = MTLVertexFormatFloat4;
        vd.attributes[i].offset      = (NSUInteger)(i * 16);
        vd.attributes[i].bufferIndex = MTL_VB_INDEX;
    }
    vd.layouts[MTL_VB_INDEX].stride = sizeof(MtlVertex);

    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    /* The guest's own fragment program when it has one and it translated;
     * otherwise the built-in pair. */
    /* RSX_FORCE_BUILTIN=1: ignora il fragment program del guest e usa la
     * coppia incorporata. E' la prova A/B che separa "la geometria non
     * rasterizza" da "rasterizza ma il programma del titolo la dipinge di
     * nero". */
    static int s_fb = -1;
    if (s_fb < 0) s_fb = getenv("RSX_FORCE_BUILTIN") ? 1 : 0;
    id<MTLLibrary> glib = (d->fp_addr && !s_fb) ? guest_fp_library(d->fp_addr) : nil;
    if (glib) {
        pd.vertexFunction   = [glib newFunctionWithName:@"vs_guest"];
        pd.fragmentFunction = [glib newFunctionWithName:@"rsx_fp_main"];
    } else {
        pd.vertexFunction   = [s_shader_lib newFunctionWithName:@"vs_main"];
        /* RSX_DEBUG_TINT=1: tinta fissa, per vedere DOVE cade la geometria
         * quando i colori veri sono quasi neri (misurato: 0.031). */
        static int s_dt = -1;
        if (s_dt < 0) s_dt = getenv("RSX_DEBUG_TINT") ? 1 : 0;
        static int s_ta = -1;
        if (s_ta < 0) s_ta = getenv("RSX_TEX_ARRAY") ? 1 : 0;
        pd.fragmentFunction = [s_shader_lib newFunctionWithName:
                               s_dt ? @"fs_tint"
                               : (s_ta && d->tex0 && s_tex[0].tex) ? @"fs_tex_arr"
                               : ((d->tex0 && s_tex[0].tex) ? @"fs_tex" : @"fs_main")];
    }
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (d->blend_enable) {
        MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[0];
        ca.blendingEnabled             = YES;
        ca.sourceRGBBlendFactor        = blend_factor_to_metal(d->blend_sfactor);
        ca.destinationRGBBlendFactor   = blend_factor_to_metal(d->blend_dfactor);
        ca.sourceAlphaBlendFactor      = blend_factor_to_metal(d->blend_sfactor);
        ca.destinationAlphaBlendFactor = blend_factor_to_metal(d->blend_dfactor);
        ca.rgbBlendOperation           = blend_equation_to_metal(d->blend_equation);
        ca.alphaBlendOperation         = blend_equation_to_metal(d->blend_equation);
    }

    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [s_dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pso) {
        fprintf(stderr, "[RSX metal] pipeline state failed: %s\n",
                [[err localizedDescription] UTF8String]);
        return nil;
    }
    s_pso_cache[s_pso_count].key = key;
    s_pso_cache[s_pso_count].pso = pso;
    s_pso_count++;
    return pso;
}

/* ---- public API ---------------------------------------------------------- */

int rsx_metal_backend_init(u32 width, u32 height, const char* title)
{
    @autoreleasepool {
        if (width)  s_width  = width;
        if (height) s_height = height;

        const char* hl = getenv("PS3RECOMP_METAL_HEADLESS");
        s_headless = (hl && *hl && *hl != '0');

        s_dev = MTLCreateSystemDefaultDevice();
        if (!s_dev) {
            fprintf(stderr, "[RSX metal] no Metal device available\n");
            return -1;
        }
        s_queue = [s_dev newCommandQueue];
        if (!s_queue) {
            fprintf(stderr, "[RSX metal] could not create command queue\n");
            return -1;
        }

        int rc;
#if TARGET_OS_IPHONE
        s_headless = 1;
        rc = create_offscreen();
#else
        rc = s_headless ? create_offscreen() : create_window(title);
#endif
        if (rc != 0) {
            fprintf(stderr, "[RSX metal] surface creation failed\n");
            return -1;
        }

        s_verts = (MtlVertex*)malloc(sizeof(MtlVertex) * MTL_MAX_VERTS);
        if (!s_verts) {
            fprintf(stderr, "[RSX metal] vertex staging alloc failed\n");
            return -1;
        }

        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeRepeat;
        sd.tAddressMode = MTLSamplerAddressModeRepeat;
        s_sampler = [s_dev newSamplerStateWithDescriptor:sd];

        { MTLTextureDescriptor* dd = [MTLTextureDescriptor
              texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                           width:1 height:1 mipmapped:NO];
          dd.usage = MTLTextureUsageShaderRead;
          s_dummy_tex = [s_dev newTextureWithDescriptor:dd];
          const u8 zero[4] = { 0, 0, 0, 0 };
          [s_dummy_tex replaceRegion:MTLRegionMake2D(0,0,1,1)
                         mipmapLevel:0 withBytes:zero bytesPerRow:4]; }

        NSError* serr = nil;
        s_shader_lib = [s_dev newLibraryWithSource:kBuiltinMSL options:nil error:&serr];
        if (!s_shader_lib) {
            fprintf(stderr, "[RSX metal] built-in shader compile failed: %s\n",
                    [[serr localizedDescription] UTF8String]);
            return -1;
        }

        s_inflight = dispatch_semaphore_create(MTL_MAX_INFLIGHT);
        if (!s_inflight) {
            fprintf(stderr, "[RSX metal] semaphore creation failed\n");
            return -1;
        }

        rsx_set_backend(&s_backend_vtable);
        s_ready  = 1;
        s_closed = 0;
        fprintf(stderr, "[RSX metal] %s on %s (%ux%u)\n",
                s_headless ? "headless" : "windowed",
                [[s_dev name] UTF8String], s_width, s_height);
        return 0;
    }
}

void rsx_metal_backend_shutdown(void)
{
    @autoreleasepool {
        if (s_ready) rsx_set_backend(NULL);
        /* Reclaim every in-flight slot so no command buffer is still
         * referencing the device, queue or textures when they are released,
         * then hand the slots back. libdispatch traps if a semaphore is
         * deallocated while its count is below the value it was created with,
         * so draining without restoring is a crash, not a leak. */
        if (s_inflight) {
            for (int i = 0; i < MTL_MAX_INFLIGHT; i++)
                dispatch_semaphore_wait(s_inflight, DISPATCH_TIME_FOREVER);
            for (int i = 0; i < MTL_MAX_INFLIGHT; i++)
                dispatch_semaphore_signal(s_inflight);
            s_inflight = nil;
        }
#if !TARGET_OS_IPHONE
        if (s_window) { [s_window close]; s_window = nil; }
#endif
        free(s_verts); s_verts = NULL;
        s_vert_count = s_draw_count = s_pso_count = 0;
        for (u32 i = 0; i < (u32)(sizeof(s_pso_cache)/sizeof(s_pso_cache[0])); i++)
            s_pso_cache[i].pso = nil;
        s_shader_lib = nil;
        s_dummy_tex = nil;
        s_layer     = nil;
        s_offscreen = nil;
        s_queue     = nil;
        s_dev       = nil;
        s_ready     = 0;
    }
}

int rsx_metal_backend_pump_messages(void)
{
#if !TARGET_OS_IPHONE
    if (s_headless || !s_ready) return 0;
    /* Event dispatch is the main thread's job. A guest thread calling this at
     * flip time would race AppKit; the host's main loop pumps for everyone. */
    if (![NSThread isMainThread]) return 0;
    @autoreleasepool {
        for (;;) {
            NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantPast]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) break;
            [NSApp sendEvent:ev];
        }
        if (s_window && ![s_window isVisible]) s_closed = 1;
    }
#endif
    return s_closed ? -1 : 0;
}

void rsx_metal_backend_present(void)
{
    if (!s_ready) return;
    /* Block only when MTL_MAX_INFLIGHT frames are already queued. */
    if (!s_headless) dispatch_semaphore_wait(s_inflight, DISPATCH_TIME_FOREVER);
    @autoreleasepool {
        id<MTLTexture> target = nil;
        id<CAMetalDrawable> drawable = nil;

        if (s_headless) {
            target = s_offscreen;
        } else {
            drawable = [s_layer nextDrawable];
            if (!drawable) {            /* compositor is busy; skip this frame */
                dispatch_semaphore_signal(s_inflight);
                return;
            }
            target = [drawable texture];
        }
        if (!target) {
            if (!s_headless) dispatch_semaphore_signal(s_inflight);
            return;
        }

        s_frame_no++;
        id<MTLCommandBuffer> cb = [s_queue commandBuffer];

        /* Un passaggio di rendering per ogni tratto CONSECUTIVO di draw che
         * condividono la superficie: e' l'ordine in cui il titolo li ha
         * emessi, e cambiare superficie e' esattamente cambiare bersaglio.
         * Il drawable si pulisce una volta sola, all'inizio del fotogramma. */
        id<MTLBuffer> vb = nil;
        if (s_draw_count > 0 && s_vert_count > 0)
            vb = [s_dev newBufferWithBytes:s_verts
                                    length:sizeof(MtlVertex) * s_vert_count
                                   options:MTLResourceStorageModeShared];

        int drawable_cleared = 0;
        u32 i = 0;
        while (i < s_draw_count || i == 0) {
            u32 run_end = i;
            u32 surf = (i < s_draw_count) ? s_draws[i].surf : 0;
            while (run_end < s_draw_count && s_draws[run_end].surf == surf) run_end++;

            int to_display = (i >= s_draw_count) || cellGcmOffsetIsDisplay(surf);
            int first_use = 0;
            id<MTLTexture> pass_target = target;
            if (!to_display) {
                /* RSX_RTT=0 torna al comportamento precedente (tutto sul
                 * drawable), per il confronto A/B. */
                static int rtt = -1;
                if (rtt < 0) { const char* e = getenv("RSX_RTT"); rtt = (e && *e == '0') ? 0 : 1; }
                if (rtt) {
                    id<MTLTexture> st = surface_texture(surf, s_width, s_height, &first_use);
                    if (st) pass_target = st; else to_display = 1;
                } else to_display = 1;
            }

            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = pass_target;
            if (to_display)
                rp.colorAttachments[0].loadAction =
                    drawable_cleared ? MTLLoadActionLoad : MTLLoadActionClear;
            else
                rp.colorAttachments[0].loadAction =
                    first_use ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.colorAttachments[0].clearColor  = clear_color_from_rsx();
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            if (to_display) drawable_cleared = 1;

            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            if (vb) {
                [enc setVertexBuffer:vb offset:0 atIndex:MTL_VB_INDEX];
                MTLViewport vp = { 0.0, 0.0, (double)s_width, (double)s_height, 0.0, 1.0 };
                [enc setViewport:vp];
            }

            for (; i < run_end; i++) {
                MtlDraw* d = &s_draws[i];
                /* RSX_ONLY_SMALL=1: salta i quad che coprono tutto lo schermo.
                 * Il titolo ne disegna uno nero per fotogramma (di fatto un
                 * clear) e sopra ci mette l'HUD: saltandolo si vede se l'HUD
                 * dipinge davvero qualcosa o se e' nero anche lui. */
                { static int os = -1;
                  if (os < 0) os = getenv("RSX_ONLY_SMALL") ? 1 : 0;
                  if (os) {
                      float mnx = 1e30f, mxx = -1e30f, mny = 1e30f, mxy = -1e30f;
                      for (u32 v = d->base; v < d->base + d->count && v < s_vert_count; v++) {
                          const float* P = s_verts[v].pos;
                          if (P[0] < mnx) mnx = P[0];
                          if (P[0] > mxx) mxx = P[0];
                          if (P[1] < mny) mny = P[1];
                          if (P[1] > mxy) mxy = P[1];
                      }
                      if (mnx <= -0.99f && mxx >= 0.99f && mny <= -0.99f && mxy >= 0.99f)
                          continue;
                  } }
                id<MTLRenderPipelineState> pso = pso_for(d);
                if (!pso) continue;
                /* Il ritaglio: era tracciato in rsx_state e non arrivava mai a
                 * Metal. Il titolo disegna migliaia di quad "a schermo pieno"
                 * ritagliati a un rettangolo piccolo -- e' l'idioma con cui
                 * stira una texture dentro una casella. Senza ritaglio ognuno
                 * copriva tutto e cancellava quello prima: restava visibile
                 * solo l'ultimo, cioe' uno schermo di tinta unita. */
                { static int sc = -1;
                  if (sc < 0) { const char* e = getenv("RSX_SCISSOR"); sc = (e && *e=='0') ? 0 : 1; }
                  if (sc) {
                      NSUInteger x = d->sx, y = d->sy, w = d->sw, h = d->sh;
                      if (x > s_width)  x = s_width;
                      if (y > s_height) y = s_height;
                      if (w > s_width  - x) w = s_width  - x;
                      if (h > s_height - y) h = s_height - y;
                      if (w == 0 || h == 0) continue;   /* ritaglio vuoto: niente */
                      MTLScissorRect sr = { x, y, w, h };
                      [enc setScissorRect:sr];
                  } }
                [enc setRenderPipelineState:pso];
                [enc setVertexBytes:d->mvp length:sizeof(d->mvp) atIndex:0];
                /* The guest's program declares all sixteen units, so bind
                 * every resident one; the built-in sampling path only reads
                 * unit 0. Metal leaves unbound slots null, which is fine for
                 * a shader that does not sample them. */
                if (s_sampler) {
                    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) {
                        /* OGNI slot, non solo quelli residenti: vedi s_dummy_tex. */
                        [enc setFragmentTexture:(s_tex[t].tex ? s_tex[t].tex : s_dummy_tex)
                                        atIndex:t];
                        [enc setFragmentSamplerState:s_sampler atIndex:t];
                    }
                }
                [enc drawPrimitives:d->topology vertexStart:d->base vertexCount:d->count];
            }
            [enc endEncoding];
            if (i >= s_draw_count) break;
        }

        /* RSX_FRAMEDBG=1: una riga per fotogramma con quanto e' arrivato al
         * backend e DOVE finisce in clip space. Distingue le tre cause di uno
         * schermo nero -- nessun draw, draw fuori dal volume di clip, draw
         * dentro ma invisibili -- che dal nero non si distinguono. */
        { static int fd = -1;
          if (fd < 0) fd = getenv("RSX_FRAMEDBG") ? 1 : 0;
          if (fd) {
              static unsigned long fno = 0;
              unsigned long n = ++fno;
              /* Un fotogramma con molti draw si stampa SEMPRE: campionando uno
               * su 60 si perdono esattamente i fotogrammi interessanti, ed e'
               * lo stesso errore del log tappato della sessione 19. */
              if (n <= 8 || (n % 60) == 0 || s_draw_count > 8) {
                  static unsigned s_szhist[4];
                  float lo[4] = { 1e30f, 1e30f, 1e30f, 1e30f };
                  float hi[4] = { -1e30f, -1e30f, -1e30f, -1e30f };
                  unsigned inside = 0;
                  for (u32 i = 0; i < s_draw_count; i++) {
                      const MtlDraw* d = &s_draws[i];
                      for (u32 v = d->base; v < d->base + d->count && v < s_vert_count; v++) {
                          const float* P = s_verts[v].pos;
                          float o[4];
                          for (int r = 0; r < 4; r++)
                              o[r] = d->mvp[r*4+0]*P[0] + d->mvp[r*4+1]*P[1]
                                   + d->mvp[r*4+2]*P[2] + d->mvp[r*4+3]*P[3];
                          for (int r = 0; r < 4; r++) {
                              if (o[r] < lo[r]) lo[r] = o[r];
                              if (o[r] > hi[r]) hi[r] = o[r];
                          }
                          if (o[3] > 0.0f && fabsf(o[0]) <= o[3] && fabsf(o[1]) <= o[3]
                              && o[2] >= 0.0f && o[2] <= o[3]) inside++;
                      }
                  }
                  /* Quanti draw hanno davvero area sullo schermo: migliaia di
                   * quad tutti dentro il clip ma degeneri danno lo stesso nero
                   * di nessun draw, e dal nero non si distinguono. */
                  unsigned degeneri = 0;
                  for (u32 i = 0; i < s_draw_count; i++) {
                      const MtlDraw* d = &s_draws[i];
                      float mnx = 1e30f, mxx = -1e30f, mny = 1e30f, mxy = -1e30f;
                      for (u32 v = d->base; v < d->base + d->count && v < s_vert_count; v++) {
                          const float* P = s_verts[v].pos;
                          if (P[0] < mnx) mnx = P[0];
                          if (P[0] > mxx) mxx = P[0];
                          if (P[1] < mny) mny = P[1];
                          if (P[1] > mxy) mxy = P[1];
                      }
                      /* meno di mezzo pixel in una delle due direzioni */
                      if ((mxx - mnx) * s_width < 1.0f || (mxy - mny) * s_height < 1.0f)
                          degeneri++;
                      /* istogramma delle dimensioni: dice se sono migliaia di
                       * quad piccoli (testo/HUD) o lo stesso quad a schermo
                       * pieno ripetuto. */
                      { float fw = (mxx - mnx) * 0.5f, fh = (mxy - mny) * 0.5f;
                        int b = (fw > 0.9f && fh > 0.9f) ? 3 : (fw > 0.3f || fh > 0.3f) ? 2
                              : (fw > 0.05f || fh > 0.05f) ? 1 : 0;
                        s_szhist[b]++; }
                  }
                  /* Quante superfici distinte: se sono molte, il titolo sta
                   * disegnando fuori schermo (render-to-texture) e noi gli
                   * mandiamo tutto sul drawable. */
                  { u32 su[16]; u32 sc[16]; u32 ns = 0;
                    for (u32 i = 0; i < s_draw_count; i++) {
                        u32 k = 0; for (; k < ns; k++) if (su[k] == s_draws[i].surf) break;
                        if (k == ns && ns < 16) { su[ns] = s_draws[i].surf; sc[ns] = 0; ns++; }
                        if (k < 16) sc[k]++;
                    }
                    char b[400]; int bp = snprintf(b, sizeof b, "[RSX frame#%lu]   superfici:", n);
                    for (u32 k = 0; k < ns && bp < (int)sizeof b - 32; k++)
                        bp += snprintf(b + bp, sizeof b - bp, " 0x%X(%u)", su[k], sc[k]);
                    for (u32 k = 0; k < ns && bp < (int)sizeof b - 8; k++)
                        bp += snprintf(b + bp, sizeof b - bp, "%s",
                                       cellGcmOffsetIsDisplay(su[k]) ? " [display]" : " [offscreen]");
                    fprintf(stderr, "%s%s\n", b, ns >= 16 ? " ..." : ""); }
                  /* Quanti ritagli distinti: se il titolo li usa, i quad a
                   * schermo pieno non coprono affatto tutto lo schermo. */
                  { u32 nsc = 0; u32 full = 0;
                    for (u32 i2 = 0; i2 < s_draw_count; i2++) {
                        if (s_draws[i2].sw >= s_width && s_draws[i2].sh >= s_height) full++;
                        else nsc++;
                    }
                    fprintf(stderr, "[RSX frame#%lu]   ritagli: pieni=%u ristretti=%u"
                            " (primo ristretto: %ux%u+%u+%u)\n", n, full, nsc,
                            nsc ? s_draws[0].sw : 0, nsc ? s_draws[0].sh : 0,
                            nsc ? s_draws[0].sx : 0, nsc ? s_draws[0].sy : 0); }
                  fprintf(stderr, "[RSX frame#%lu]   dimensioni: minuscoli=%u piccoli=%u "
                          "medi=%u schermo-pieno=%u\n", n,
                          s_szhist[0], s_szhist[1], s_szhist[2], s_szhist[3]);
                  s_szhist[0] = s_szhist[1] = s_szhist[2] = s_szhist[3] = 0;
                  fprintf(stderr,
                          "[RSX frame#%lu] draws=%u (degeneri=%u) verts=%u dentro-clip=%u  "
                          "x[%.2f %.2f] y[%.2f %.2f] z[%.2f %.2f] w[%.2f %.2f]\n",
                          n, s_draw_count, degeneri, s_vert_count, inside,
                          lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], lo[3], hi[3]);
                  fflush(stderr);
              }
          } }

        /* RSX_DUMP_FRAME=<n>: salva su file il primo fotogramma con piu' di n
         * draw. I fotogrammi pieni durano poche decine di millisecondi e uno
         * screenshot da fuori li manca sistematicamente; questo li prende dal
         * di dentro, nell'istante giusto. */
        { static int s_df = -2; static int s_done = 0; static int s_dmax = 1;
          if (s_df == -2) { const char* e = getenv("RSX_DUMP_FRAME");
                            s_df = (e && *e) ? atoi(e) : -1;
                            /* RSX_DUMP_COUNT=<k>: salva i primi k fotogrammi
                             * che superano la soglia (rsx_frame_<i>.ppm). */
                            const char* c = getenv("RSX_DUMP_COUNT");
                            if (c && atoi(c) > 1) s_dmax = atoi(c); }
          if (s_df > 0 && s_done < s_dmax && (int)s_draw_count > s_df) {
              s_done++;
              MTLTextureDescriptor* sd = [MTLTextureDescriptor
                  texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                               width:target.width
                                              height:target.height mipmapped:NO];
              sd.usage = MTLTextureUsageShaderRead;
              sd.storageMode = MTLStorageModeShared;
              id<MTLTexture> rb = [s_dev newTextureWithDescriptor:sd];
              id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
              [bl copyFromTexture:target sourceSlice:0 sourceLevel:0
                     sourceOrigin:MTLOriginMake(0,0,0)
                       sourceSize:MTLSizeMake(target.width, target.height, 1)
                        toTexture:rb destinationSlice:0 destinationLevel:0
                destinationOrigin:MTLOriginMake(0,0,0)];
              [bl endEncoding];
              /* Anche le superfici fuori schermo: se l'immagine vera e' li',
               * il difetto e' nella composizione, non nel disegno. */
              for (u32 k = 0; k < s_surf_count; k++) {
                  if (!s_surf[k].tex) continue;
                  const NSUInteger SW = s_surf[k].tex.width, SH = s_surf[k].tex.height;
                  MTLTextureDescriptor* sd2 = [MTLTextureDescriptor
                      texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                   width:SW height:SH mipmapped:NO];
                  sd2.storageMode = MTLStorageModeShared;
                  id<MTLTexture> rb2 = [s_dev newTextureWithDescriptor:sd2];
                  if (!rb2) continue;
                  id<MTLBlitCommandEncoder> b2 = [cb blitCommandEncoder];
                  [b2 copyFromTexture:s_surf[k].tex sourceSlice:0 sourceLevel:0
                         sourceOrigin:MTLOriginMake(0,0,0)
                           sourceSize:MTLSizeMake(SW, SH, 1)
                            toTexture:rb2 destinationSlice:0 destinationLevel:0
                    destinationOrigin:MTLOriginMake(0,0,0)];
                  [b2 endEncoding];
                  s_surf_dump[k] = rb2;
              }
              [cb commit]; [cb waitUntilCompleted];
              const NSUInteger W = target.width, H = target.height;
              u8* px = (u8*)malloc(W * H * 4);
              if (px) {
                  [rb getBytes:px bytesPerRow:W*4
                    fromRegion:MTLRegionMake2D(0,0,W,H) mipmapLevel:0];
                  char path[64];
                  if (s_dmax > 1) snprintf(path, sizeof path, "/tmp/rsx_frame%d.ppm", s_done);
                  else            snprintf(path, sizeof path, "/tmp/rsx_frame.ppm");
                  if (s_dmax > 1) snprintf(path, sizeof path,
                                           "/tmp/rsx_frame_%03d.ppm", s_done);
                  FILE* f = fopen(path, "wb");
                  if (f) {
                      fprintf(f, "P6\n%lu %lu\n255\n",
                              (unsigned long)W, (unsigned long)H);
                      for (NSUInteger i = 0; i < W*H; i++) {
                          u8 rgb[3] = { px[i*4+2], px[i*4+1], px[i*4+0] };
                          fwrite(rgb, 1, 3, f);
                      }
                      fclose(f);
                      fprintf(stderr, "[RSX metal] fotogramma con %u draw salvato in %s\n",
                              s_draw_count, path);
                      fflush(stderr);
                  }
                  for (u32 k = 0; k < s_surf_count; k++) {
                      if (!s_surf_dump[k]) continue;
                      const NSUInteger SW = s_surf_dump[k].width, SH = s_surf_dump[k].height;
                      u8* sp = (u8*)malloc(SW * SH * 4);
                      if (!sp) continue;
                      [s_surf_dump[k] getBytes:sp bytesPerRow:SW*4
                                    fromRegion:MTLRegionMake2D(0,0,SW,SH) mipmapLevel:0];
                      char sp2[128];
                      snprintf(sp2, sizeof sp2, "/tmp/rsx_surf_%08X.ppm", s_surf[k].offset);
                      FILE* sf = fopen(sp2, "wb");
                      if (sf) {
                          fprintf(sf, "P6\n%lu %lu\n255\n",
                                  (unsigned long)SW, (unsigned long)SH);
                          for (NSUInteger q = 0; q < SW*SH; q++) {
                              u8 rgb[3] = { sp[q*4+2], sp[q*4+1], sp[q*4+0] };
                              fwrite(rgb, 1, 3, sf);
                          }
                          fclose(sf);
                          fprintf(stderr, "[RSX metal] superficie 0x%X salvata in %s\n",
                                  s_surf[k].offset, sp2);
                      }
                      free(sp); s_surf_dump[k] = nil;
                  }
                  free(px);
              }
              if (drawable) [drawable present];
              if (!s_headless) dispatch_semaphore_signal(s_inflight);
              s_draw_count = 0; s_vert_count = 0;
              return;
          } }

        if (drawable) [cb presentDrawable:drawable];

        if (s_headless) {
            /* The readback below must see a completed frame. */
            [cb commit];
            [cb waitUntilCompleted];
        } else {
            dispatch_semaphore_t sem = s_inflight;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> _unused) {
                (void)_unused;
                dispatch_semaphore_signal(sem);
            }];
            [cb commit];        /* no wait: the CPU goes on to the next frame */
        }

        if (s_dropped_draws) {
            fprintf(stderr, "[RSX metal] dropped %u draw(s) this frame (cap %d draws / %u verts)\n",
                    s_dropped_draws, MTL_MAX_DRAWS, MTL_MAX_VERTS);
            s_dropped_draws = 0;
        }
        s_draw_count = 0;
        s_vert_count = 0;

        if (s_headless && s_offscreen) {
            u32 px = 0;
            MTLRegion r = MTLRegionMake2D(s_width / 2, s_height / 2, 1, 1);
            [s_offscreen getBytes:&px bytesPerRow:4 fromRegion:r mipmapLevel:0];
            s_last_present_bgra = px;
        }
    }
}

u32 rsx_metal_backend_debug_color(void)     { return s_clear_argb; }
u32 rsx_metal_backend_readback_center(void) { return s_last_present_bgra; }

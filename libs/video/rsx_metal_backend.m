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
#include "rsx_vp_decompiler.h"   /* guest vertex program -> MSL (sessione 27) */
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
    u32 surf_w, surf_h;  /* dimensione della superficie (SURFACE_CLIP) al momento del draw */
    u32 tex_off[RSX_MAX_TEXTURES]; /* offset guest delle texture legate (per RSX_DRAWLIST) */
    u32 tex_fmt[RSX_MAX_TEXTURES]; /* formato (TEXTURE_FORMAT) delle texture legate */
    u32 zeta_off;                  /* SURFACE_ZETA_OFFSET al momento del draw */
    int depth_test; u32 depth_func; int depth_mask; /* stato del test di profondita' */
    u32 shader_ctrl;               /* SET_SHADER_CONTROL (0x1D60): bit 0xE = export della profondita' */
    u32 pass_pf;                   /* MTLPixelFormat del bersaglio del pass (assegnato alla codifica) */
    u32 fp_hash;                   /* hash del microcodice del FP al momento del draw */
    u32 surf2, surf3;              /* SURFACE_COLOR_C/DOFFSET */
    u32 n_mrt;                     /* bersagli di colore attivi (SET_SURFACE_COLOR_TARGET): 1..4 */
    u32 surf_fmt, color_target, surf_pitch0, surf1; /* SURFACE_FORMAT, COLOR_TARGET, pitch A, offset B */
    /* Vertex program del guest (sessione 27): indice in s_vps o -1 (percorso
     * incorporato: costanti 0..3 come MVP). vbase/vstride = dove stanno gli
     * attributi letti dal programma in s_vpd; cblock = blocco di costanti. */
    int vp;
    u32 vbase, vstride, cblock;
    /* Le texture legate al momento del draw (sessione 27): prima venivano
     * legate al present, cioe' TUTTI i draw del fotogramma usavano le ultime.
     * Puntatori non ritenuti: la cache (s_texcache) le tiene vive fino a fine
     * fotogramma. */
    __unsafe_unretained id<MTLTexture> tex[RSX_MAX_TEXTURES];
} MtlDraw;
static u32 fp_ucode_hash(u32 fp_addr);
static id<MTLLibrary> guest_fp_library(u32 fp_addr, int vp, u32 ctrl, u32 mrt, u32 hash);

/* One MTLTexture per unit, rebuilt when the guest rebinds a different image. */
typedef struct {
    id<MTLTexture> tex;
    u32 offset, format, image_rect;   /* what is currently resident */
} MtlTexUnit;
static MtlTexUnit s_tex[RSX_MAX_TEXTURES];
static id<MTLTexture> tex_with_remap(id<MTLTexture> t, u32 remap);   /* piu' sotto */

static MtlVertex* s_verts;
static u32        s_vert_count;
static MtlDraw    s_draws[MTL_MAX_DRAWS];
static u32        s_draw_count;
unsigned rsx_backend_draw_count(void) { return s_draw_count; }   /* per RSX_MTRACE (rsx_commands.c) */
static unsigned long s_frame_no;
unsigned rsx_backend_frame_no(void) { return (unsigned)s_frame_no; }   /* per GCM_PARK_DUMP_FROM (cellGcmSys.c) */
static u32 s_draw_count_peek(void) { return s_draw_count; }
static u32        s_dropped_draws;

/* ---- vertex program del guest (sessione 27) --------------------------------
 * Il titolo trasforma i vertici con i propri programmi NV40 (matrici nelle
 * costanti che vuole lui, attributi compressi, ecc.): con la sola MVP dalle
 * costanti 0..3 ogni vertice usciva dal clip (w negativo). Ora il programma
 * viene tradotto in MSL (rsx_vp_decompile_msl), gli attributi che legge
 * vengono letti sul CPU (rsx_fetch_attrib, come fa il backend D3D12) in un
 * flusso a passo variabile, e le 512 costanti + la trasformazione di
 * viewport vanno in un blocco per draw (deduplicato quando non cambiano).
 * RSX_VP=0 torna al percorso incorporato. */
#define MTL_VP_FLOATS   (48u * 1024u * 1024u)   /* 192 MB di attributi per fotogramma: il livello ne muove 1M x ~4 float4 (66 MB), oltre i 64 di prima */
#define MTL_CB_BYTES    8448u                    /* 512 float4 + scale/offset, a 256 */
#define MTL_CB_MAX      4096u                    /* blocchi per fotogramma (34 MB) */
typedef struct { u32 hash; u32 bytes; u32 mask; char* msl; int ok; } MtlVp;
static MtlVp   s_vps[4096];    /* 256 era pieno dopo ~150 fotogrammi del livello: ogni VP nuovo tornava -1 e TUTTI i draw finivano nel percorso incorporato (scena piatta, s.28) */
static u32     s_vp_count;
static float*  s_vpd;          /* attributi dei draw con VP, in float */
static u32     s_vpd_used;
static u8*     s_cbs;          /* blocchi di costanti del fotogramma */
static u32     s_cb_count;
static u32     s_cur_mask;     /* attributi da leggere per il draw in corso */
static int     s_vp_full;      /* s_vpd esaurito durante il draw in corso */
static int use_vp(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("RSX_VP"); v = (e && *e == '0') ? 0 : 1; }
    return v;
}

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
typedef struct { u32 key; int vp; id<MTLRenderPipelineState> pso; } MtlPsoEntry;
/* 512 voci erano poche per il livello (sessione 28): con la cache dei
 * programmi piena ogni draw cadeva sulla coppia incorporata e la scena era
 * un colore piatto. 8192 voci e un indice hash (sonda lineare). */
#define MTL_PSO_CACHE 8192
#define MTL_PSO_HASH  (MTL_PSO_CACHE * 4)
static MtlPsoEntry s_pso_cache[MTL_PSO_CACHE];
static u32         s_pso_hash[MTL_PSO_HASH];   /* indice+1, 0 = vuoto */
static u32         s_pso_count;
static u32 pso_slot(u32 key, int vp) { u32 h = key * 2654435761u ^ ((u32)vp * 40503u); h ^= h >> 15; return h & (MTL_PSO_HASH - 1); }
static id<MTLRenderPipelineState> pso_find(u32 key, int vp)
{
    for (u32 hs = pso_slot(key, vp), n = 0; n < MTL_PSO_HASH; n++, hs = (hs + 1) & (MTL_PSO_HASH - 1)) {
        u32 ix = s_pso_hash[hs]; if (!ix) return nil;
        if (s_pso_cache[ix - 1].key == key && s_pso_cache[ix - 1].vp == vp) return s_pso_cache[ix - 1].pso; }
    return nil;
}
static void pso_index_add(u32 ix)
{
    u32 hs = pso_slot(s_pso_cache[ix].key, s_pso_cache[ix].vp);
    while (s_pso_hash[hs]) hs = (hs + 1) & (MTL_PSO_HASH - 1);
    s_pso_hash[hs] = ix + 1;
}

/* RSX clear colour, ARGB8888, as written by NV4097_SET_COLOR_CLEAR_VALUE. */
static u32 s_clear_argb = 0xFF000000u;
static u32 s_last_present_bgra;

/* ---- rsx_backend vtable -------------------------------------------------- */

static float s_clear_depth = 1.0f;
static const rsx_state* s_state;
static void mtl_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud; (void)stencil;
    /* 0xF0 is the colour-buffer mask; depth/stencil clears carry 0x03. */
    if (flags & 0xF0u) s_clear_argb = color;
    if (flags & 0x01u) { s_clear_depth = depth;
        { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0; static int _n = 0;
          if (sd && _n++ < 40) fprintf(stderr, "[RSX metal] clear depth=%f flags=0x%X surf=%X\n", depth, flags, s_state ? s_state->surface_color_offset[0] : 0); } }
}

/* The draw callbacks are not handed the state, so it is latched here. Every
 * state setter caches it; set_vertex_attribs in particular always fires before
 * a draw. The D3D12 backend does the same via s_d3d.current_rsx_state. */
static void mtl_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    if (state) s_state = state;
    if (!state) return;
    u32 w = state->surface_clip_w, h = state->surface_clip_h;
    if (!w || !h || (w == s_width && h == s_height)) return;
    /* Il drawable segue SOLO la superficie di display: le superfici fuori
     * schermo hanno la loro dimensione (l'atlante delle ombre di Uncharted e'
     * 2432x1216; prima ridimensionava la finestra e la scena 1280x720 finiva
     * nel quadrante in basso a sinistra). */
    if (!cellGcmOffsetIsDisplay(state->surface_color_offset[0])) return;
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

/* ---- occlusion query (NV4097_GET_REPORT ZPASS_PIXEL_CNT) ----------------
 * Ogni draw ha uno slot da 8 byte nel visibility buffer del fotogramma
 * (MTLVisibilityResultModeCounting: campioni che passano il depth test).
 * CLEAR_REPORT_VALUE segna l'inizio; GET_REPORT registra {ea, primo draw,
 * ultimo draw}; a GPU finita, il completed handler somma gli slot e scrive
 * il conteggio vero a ea+8 (il job 215 lo legge il fotogramma dopo, come
 * sul ferro dove la GPU sta un fotogramma dietro). */
enum { MTL_MAX_REPORTS = 8192, MTL_VIS_RING = 3 };
#include <stdatomic.h>
static atomic_int s_gpu_pending;   /* fotogrammi con report ancora in volo sulla GPU */
int rsx_metal_backend_gpu_busy(void) { return atomic_load(&s_gpu_pending) > 0; }
/* Aspetta (al massimo max_us) che la GPU finisca i fotogrammi con report in
 * volo. Rimandare il flip al tick successivo costava un tick intero per
 * fotogramma: menu a 33 fps invece di 55 (misurato, sessione 28). */
int rsx_metal_backend_gpu_wait(unsigned max_us)
{
    for (unsigned w = 0; w < max_us && atomic_load(&s_gpu_pending) > 0; w += 100) usleep(100);
    return atomic_load(&s_gpu_pending) == 0;
}
typedef struct { u32 ea, first, last; } MtlReport;
static MtlReport      s_reports[MTL_MAX_REPORTS];
static u32            s_report_count, s_report_start;
static id<MTLBuffer>  s_vis_buf[MTL_VIS_RING];
static u32            s_vis_idx;
static u32            s_draw_count_peek(void);
static void mtl_report_clear(void* ud) { (void)ud; s_report_start = s_draw_count_peek(); }
static void mtl_report_zpass(void* ud, u32 ea)
{
    (void)ud;
    if (s_report_count >= MTL_MAX_REPORTS) return;
    u32 now = s_draw_count_peek();
    s_reports[s_report_count].ea = ea; s_reports[s_report_count].first = s_report_start; s_reports[s_report_count].last = now;
    s_report_count++;
}

static rsx_backend s_backend_vtable = {
    .userdata          = NULL,
    .report_clear      = mtl_report_clear,
    .report_zpass      = mtl_report_zpass,
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

/* Gli attributi che il vertex program legge, nell'ordine dei bit della
 * maschera, in coda a s_vpd (float4 ciascuno). */
static void fetch_vp_vertex(const rsx_state* st, u32 vi)
{
    if (!s_cur_mask) return;
    u32 n = (u32)__builtin_popcount(s_cur_mask);
    if (s_vpd_used + n * 4 > MTL_VP_FLOATS) { s_vp_full = 1;
        { static int _n = 0; if (_n++ < 4) fprintf(stderr, "[RSX metal] flusso degli attributi pieno (%u float) al draw %u: percorso incorporato\n", s_vpd_used, s_draw_count); }
        return; }
    for (int a = 0; a < 16; a++)
        if (s_cur_mask & (1u << a)) { rsx_fetch_attrib(st, a, vi, &s_vpd[s_vpd_used]); s_vpd_used += 4; }
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
        u32 _vi = resolve(st, (seq), ctx);                                     \
        fetch_vertex(st, _vi, &s_verts[s_vert_count++]);                       \
        fetch_vp_vertex(st, _vi);                                              \
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
    /* SET_INDEX_ARRAY_DMA (index_array_dma): bits [7:4] = tipo (0 = u32,
     * 1 = u16), bits [3:0] = locazione (0 = locale/VRAM, 1 = main). Prima
     * il tipo veniva letto dall'OFFSET (che e' un offset di byte e basta) e
     * la base risolta con la tabella IO anche per la VRAM: per Uncharted
     * (indici in VRAM a 0x5EC3500) ogni indice risolveva a 0 e ogni quad
     * collassava in un punto. Stessa regola del backend D3D12
     * (read_guest_index). */
    int u16type = ((st->index_array_dma >> 4) & 0xFu) == 1;
    int local   = (st->index_array_dma & 0xFu) == 0;
    u32 ea  = cellGcmResolveLocated(local, st->index_array_offset)
            + (base + seq) * (u16type ? 2u : 4u);
    if (!vm_base) return 0;
    const u8* p = vm_base + ea;
    return u16type ? (u32)((p[0] << 8) | p[1])
                   : (u32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

/* ---- draw recording ------------------------------------------------------ */

/* Il programma corrente nella cache (tradotto alla prima vista); -1 se non
 * c'e' un programma, non si traduce, o non legge attributi. */
static int vp_lookup_slow(const rsx_state* st, u32 start);
static int vp_lookup(const rsx_state* st)
{
    if (!use_vp() || !st || st->vp_ucode_bytes < 16) {
        static int _n = 0; if (_n++ < 4) fprintf(stderr, "[RSX metal] vp_lookup: niente microcodice (%u byte) al draw %u\n", st ? st->vp_ucode_bytes : 0, s_draw_count);
        return -1; }
    /* Il programma parte da SET_TRANSFORM_PROGRAM_START: la RSX ha 512 slot
     * e Uncharted ne tiene caricati diversi insieme (i quad 2D partono da
     * uno slot diverso da 0). Hash e decompilazione dal punto di partenza,
     * fino alla fine dei byte scritti (il decompilatore si ferma al bit END). */
    u32 start = st->transform_program_start * 16u;
    if (start + 16 > st->vp_ucode_bytes) {
        static int _n = 0; if (_n++ < 12)
            fprintf(stderr, "[RSX metal] vp_lookup: slot di partenza %u (byte %u) oltre il microcodice caricato (%u byte): niente VP del guest\n",
                    st->transform_program_start, start, st->vp_ucode_bytes);
        return -1; }
    /* Cache (sessione 28): l'hash FNV su tutto il microcodice ad OGNI draw era
     * il 42% del thread del walker con 6.000 draw per fotogramma. Il
     * microcodice cambia solo con SET_TRANSFORM_PROGRAM (vp_ucode_gen) o con
     * lo slot di partenza. */
    static u32 s_vl_gen, s_vl_start, s_vl_bytes; static int s_vl_res = -2;
    if (s_vl_res != -2 && st->vp_ucode_gen == s_vl_gen && start == s_vl_start && st->vp_ucode_bytes == s_vl_bytes) return s_vl_res;
    s_vl_gen = st->vp_ucode_gen; s_vl_start = start; s_vl_bytes = st->vp_ucode_bytes;
    int res = vp_lookup_slow(st, start);
    s_vl_res = res;
    { static int _n = 0; if (res < 0 && _n++ < 4) fprintf(stderr, "[RSX metal] vp_lookup: -1 per start=%u bytes=%u gen=%u al draw %u\n", start, st->vp_ucode_bytes, st->vp_ucode_gen, s_draw_count); }
    return res;
}
static int vp_lookup_slow(const rsx_state* st, u32 start)
{
    const u8* ucode = st->vp_ucode + start;
    u32 bytes = st->vp_ucode_bytes - start; if (bytes > sizeof st->vp_ucode - start) bytes = sizeof st->vp_ucode - start;
    /* Solo fino al bit END: prima l'hash copriva tutti i byte caricati dopo
     * lo slot di partenza, cioe' anche gli ALTRI programmi residenti, e ogni
     * ricarica di uno slot vicino faceva "nuovo" ogni VP (4096 voci piene in
     * 150 fotogrammi del livello, poi tutto nel percorso incorporato). */
    { u32 ni = rsx_vp_program_size_instrs(ucode, bytes); if (ni) bytes = ni * 16u; }
    u32 h = 2166136261u;
    for (u32 i = 0; i < bytes; i++) h = (h ^ ucode[i]) * 16777619u;
    h ^= bytes;
    for (u32 i = 0; i < s_vp_count; i++)
        if (s_vps[i].hash == h && s_vps[i].bytes == bytes) return s_vps[i].ok ? (int)i : -1;
    if (s_vp_count >= (u32)(sizeof s_vps / sizeof s_vps[0])) {
        static int _n = 0; if (_n++ < 4) fprintf(stderr, "[RSX metal] cache dei VP piena (%u): il guest VP non viene usato\n", s_vp_count);
        return -1; }
    MtlVp* v = &s_vps[s_vp_count++];
    v->hash = h; v->bytes = bytes; v->ok = 0; v->msl = NULL; v->mask = 0;
    static char msl[192 * 1024];
    u32 mask = 0;
    int n = rsx_vp_decompile_msl(ucode, bytes, &mask, msl, sizeof msl);
    if (n <= 0 || !(mask & 0xFFFFu)) {
        fprintf(stderr, "[RSX metal] guest vp %08X: %s (%d instr, mask %04X)\n",
                h, n <= 0 ? "decompile failed" : "no inputs", n, mask);
        return -1;
    }
    v->msl = strdup(msl); v->mask = mask & 0xFFFFu; v->ok = v->msl != NULL;
    fprintf(stderr, "[RSX metal] guest vp %08X: %d instructions, inputs %04X\n", h, n, v->mask);
    /* RSX_VP_DUMP=1: l'MSL tradotto. */
    { static int vd = -1; if (vd < 0) vd = getenv("RSX_VP_DUMP") ? 1 : 0;
      if (vd) fprintf(stderr, "---- MSL vp %08X ----\n%s\n----\n", h, msl); }
    return v->ok ? (int)(s_vp_count - 1) : -1;
}

/* Il blocco di costanti per il draw: le 512 float4 del guest + la
 * trasformazione di viewport (x/y identita', z rimappata come fa il backend
 * D3D12). Riusa l'ultimo blocco se identico. */
static u32 cb_block(const rsx_state* st)
{
    u8 blk[MTL_CB_BYTES];
    memset(blk, 0, sizeof blk);
    memcpy(blk, st->vertex_constants, sizeof st->vertex_constants);
    float* vpx = (float*)(blk + RSX_MAX_VERTEX_CONSTANTS * 16);
    vpx[0] = vpx[1] = vpx[3] = 1.0f; vpx[4] = vpx[5] = vpx[7] = 0.0f;
    if (st->viewport_scale[2] != 0.0f) { vpx[2] = st->viewport_scale[2]; vpx[6] = st->viewport_offset[2]; }
    else                                { vpx[2] = 1.0f; vpx[6] = 0.0f; }
    if (s_cb_count && memcmp(s_cbs + (s_cb_count - 1) * MTL_CB_BYTES, blk, sizeof blk) == 0)
        return s_cb_count - 1;
    if (s_cb_count >= MTL_CB_MAX) return s_cb_count - 1;   /* pieno: riusa (sbagliato ma non esplode) */
    memcpy(s_cbs + s_cb_count * MTL_CB_BYTES, blk, sizeof blk);
    return s_cb_count++;
}

/* Prepara il draw per il VP del guest: maschera, base allineata a 256 in
 * s_vpd. Da chiamare PRIMA di leggere i vertici. */
static void vp_begin_draw(const rsx_state* st, MtlDraw* d)
{
    d->vp = vp_lookup(st);
    s_cur_mask = (d->vp >= 0) ? s_vps[d->vp].mask : 0;
    s_vp_full = 0;
    s_vpd_used = (s_vpd_used + 63u) & ~63u;
    d->vbase = s_vpd_used * 4;
    d->vstride = 16u * (u32)__builtin_popcount(s_cur_mask);
    d->cblock = (d->vp >= 0) ? cb_block(st) : 0;
}
static void vp_end_draw(MtlDraw* d)
{
    if (d->vp >= 0 && s_vp_full) d->vp = -1;   /* flusso pieno: percorso incorporato */
    s_cur_mask = 0;
}

/* RSX_VP_DBG=<n>: i primi n draw con VP del guest: programma, attributi
 * dei primi due vertici e le costanti che il programma legge (c0..3,
 * c256..259, c467). Per capire se la trasformazione ha i numeri giusti.
 * RSX_VP_DBG_FP=<hex> filtra per fragment program; con PS3_TTY_BT parte
 * dall'hit. Ritorna 1 se il draw va stampato (record_draw aggiunge indici e
 * byte grezzi). */
static int vpdbg_common(const MtlDraw* d, const rsx_state* st, u32 prim, u32 wrote, const char* via)
{
    static int s_vd = -2; static int s_vn = 0; static int s_gate = -1;
    extern int g_hle_trace_armed;
    if (s_vd == -2) { const char* e = getenv("RSX_VP_DBG"); s_vd = e ? atoi(e) : 0; }
    if (s_gate < 0) s_gate = getenv("PS3_TTY_BT") ? 1 : 0;
    static long s_fpf = -2; if (s_fpf == -2) { const char* e = getenv("RSX_VP_DBG_FP"); s_fpf = e ? (long)strtoul(e, 0, 16) : -1; }
    static long s_sff = -2; if (s_sff == -2) { const char* e = getenv("RSX_VP_DBG_SURF"); s_sff = e ? (long)strtoul(e, 0, 16) : -1; }
    static long s_vpf = -2; if (s_vpf == -2) { const char* e = getenv("RSX_VP_DBG_VP"); s_vpf = e ? (long)strtoul(e, 0, 16) : -1; }
    if (!(d->vp >= 0 && s_vn < s_vd && (!s_gate || g_hle_trace_armed) && (s_fpf < 0 || (long)d->fp_addr == s_fpf) && (s_sff < 0 || (long)d->surf == s_sff)
          && (s_vpf < 0 || (long)s_vps[d->vp].hash == s_vpf))) return 0;
    s_vn++;
    fprintf(stderr, "[vpdbg] %s draw#%u vp=%08X mask=%04X prim=%u count=%u surf=%08X fp=%08X blend=%d\n",
            via, s_draw_count - 1, s_vps[d->vp].hash, s_vps[d->vp].mask, prim, wrote, d->surf, d->fp_addr, d->blend_enable);
    const float* base_f = s_vpd + d->vbase / 4; u32 na = d->vstride / 16;
    for (u32 v = 0; v < 4 && v < wrote; v++) {
        fprintf(stderr, "[vpdbg]   v%u:", v);
        u32 rank = 0;
        for (int a = 0; a < 16; a++) { if (!(s_vps[d->vp].mask & (1u << a))) continue;
            const float* f = base_f + v * na * 4 + rank * 4; rank++;
            fprintf(stderr, " a%d=(%.3f %.3f %.3f %.3f)", a, f[0], f[1], f[2], f[3]); }
        fprintf(stderr, "\n"); }
    const float* cb = (const float*)(s_cbs + d->cblock * MTL_CB_BYTES);
    static const int slots[] = { 0, 1, 2, 3, 256, 257, 258, 259, 467 };
    for (unsigned k = 0; k < sizeof slots / sizeof slots[0]; k++) { const float* c = cb + slots[k] * 4;
        fprintf(stderr, "[vpdbg]   c%d=(%.4f %.4f %.4f %.4f)%s", slots[k], c[0], c[1], c[2], c[3], (k % 3 == 2) ? "\n" : ""); }
    fprintf(stderr, "[vpdbg]   posscale=(%.3f %.3f %.3f) posoffset=(%.3f %.3f %.3f) vp_scale=(%.1f %.1f %.3f) vp_off=(%.1f %.1f %.3f)\n",
            cb[512*4], cb[512*4+1], cb[512*4+2], cb[513*4], cb[513*4+1], cb[513*4+2],
            st->viewport_scale[0], st->viewport_scale[1], st->viewport_scale[2],
            st->viewport_offset[0], st->viewport_offset[1], st->viewport_offset[2]);
    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) if (d->tex[t])
        fprintf(stderr, "[vpdbg]   tex%u: %lux%lu off=%08X fmt=%08X ctrl0=%08X ctrl1(remap)=%08X\n", t, (unsigned long)d->tex[t].width,
                (unsigned long)d->tex[t].height, st->textures[t].offset, st->textures[t].format, st->textures[t].control0, st->textures[t].control1);
    return 1;
}

static void record_draw(const rsx_state* st, u32 prim, u32 base, u32 count,
                        IndexResolver resolve)
{
    if (!s_ready || !s_verts || !st || count == 0) return;
    if (s_draw_count >= MTL_MAX_DRAWS) { s_dropped_draws++; return; }

    MtlDraw tmp; memset(&tmp, 0, sizeof tmp);
    vp_begin_draw(st, &tmp);
    u32 first_vert = s_vert_count;
    u32 wrote = emit_vertices(st, prim, count, resolve, &base);
    vp_end_draw(&tmp);
    if (wrote == 0) { s_dropped_draws++; return; }

    MtlDraw* d = &s_draws[s_draw_count++];
    *d = tmp;
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
    d->fp_hash        = fp_ucode_hash(st->shader_program);
    d->tex0           = (st->textures[0].control0 & 0x80000000u) != 0;
    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) {
        d->tex[t] = (st->textures[t].control0 & 0x80000000u) ? tex_with_remap(s_tex[t].tex, st->textures[t].control1) : nil;
        d->tex_off[t] = st->textures[t].offset; d->tex_fmt[t] = st->textures[t].format; }
    d->zeta_off       = st->surface_zeta_offset;
    d->depth_test = st->depth_test_enable; d->depth_func = st->depth_func; d->depth_mask = st->depth_mask;
    d->shader_ctrl = st->shader_control;
    d->surf           = st->surface_color_offset[0];
    d->surf_fmt = st->surface_format; d->color_target = st->color_target; d->surf_pitch0 = st->surface_color_pitch[0]; d->surf1 = st->surface_color_offset[1];
    d->surf2 = st->surface_color_offset[2]; d->surf3 = st->surface_color_offset[3];
    d->n_mrt = (st->color_target == 0x13) ? 2 : (st->color_target == 0x17) ? 3 : (st->color_target == 0x1F) ? 4 : 1;
    /* Il programma va tradotto ORA, finche' il microcodice in memoria e' quello
     * del draw: il titolo riusa il ring dei FP e alla codifica (present) ci
     * sarebbe gia' il programma del fotogramma dopo. */
    if (d->fp_addr) (void)guest_fp_library(d->fp_addr, (d->vp >= 0 && s_vps[d->vp].ok) ? d->vp : -1, d->shader_ctrl, d->n_mrt, d->fp_hash);
    d->surf_w = st->surface_clip_w ? st->surface_clip_w : s_width; d->surf_h = st->surface_clip_h ? st->surface_clip_h : s_height;
    d->sx = st->scissor_x; d->sy = st->scissor_y;
    d->sw = st->scissor_w; d->sh = st->scissor_h;
    if (vpdbg_common(d, st, prim, wrote, "array")) {
          for (int a = 0; a < 16; a++) if (s_vps[d->vp].mask & (1u << a))
              fprintf(stderr, "[vpdbg]   attr%d: fmt=%08X off=%08X\n", a, st->vertex_attribs[a].format, st->vertex_attribs[a].offset);
          fprintf(stderr, "[vpdbg]   %s base=%u idx:", resolve == resolve_indexed ? "INDEXED" : "linear", base);
          for (u32 q = 0; q < 6 && q < count; q++) fprintf(stderr, " %u", resolve(st, q, &base));
          fprintf(stderr, "  index_array_offset=%08X dma=%u\n", st->index_array_offset, st->index_array_dma);
    }

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

    MtlDraw tmp; memset(&tmp, 0, sizeof tmp);
    vp_begin_draw(s_state, &tmp);
    #define IPUSH(seq) do {                                                    \
        if (s_vert_count >= MTL_MAX_VERTS) goto done;                          \
        const float* a = &verts[(seq) * STRIDE];                               \
        MtlVertex* o = &s_verts[s_vert_count++];                               \
        memcpy(o->pos, &a[0],           sizeof o->pos);                        \
        memcpy(o->col, &a[s_col * 4],   sizeof o->col);                        \
        memcpy(o->tc,  &a[s_tc  * 4],   sizeof o->tc);                         \
        if (s_cur_mask) {                                                      \
            u32 _n = (u32)__builtin_popcount(s_cur_mask);                      \
            if (s_vpd_used + _n * 4 > MTL_VP_FLOATS) s_vp_full = 1;            \
            else for (int _a = 0; _a < 16; _a++) if (s_cur_mask & (1u << _a)) { \
                memcpy(&s_vpd[s_vpd_used], &a[_a * 4], 16); s_vpd_used += 4; } \
        }                                                                      \
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
    vp_end_draw(&tmp);
    if (wrote == 0) { s_dropped_draws++; return; }

    MtlDraw* d = &s_draws[s_draw_count++];
    *d = tmp;
    d->base  = first_vert;
    d->count = wrote;
    d->topology = rsx_primitive_needs_expansion(primitive) ? MTLPrimitiveTypeTriangle
                : topo_to_metal(rsx_primitive_topology(primitive));
    d->blend_enable   = s_state ? s_state->blend_enable   : 0;
    d->blend_sfactor  = s_state ? s_state->blend_sfactor  : 0;
    d->blend_dfactor  = s_state ? s_state->blend_dfactor  : 0;
    d->blend_equation = s_state ? s_state->blend_equation : 0;
    d->fp_addr        = s_state ? s_state->shader_program : 0;
    d->fp_hash        = d->fp_addr ? fp_ucode_hash(d->fp_addr) : 0;
    d->tex0           = s_state && (s_state->textures[0].control0 & 0x80000000u) != 0;
    d->zeta_off = s_state ? s_state->surface_zeta_offset : 0;
    d->depth_test = s_state ? s_state->depth_test_enable : 0; d->depth_func = s_state ? s_state->depth_func : 0x207; d->depth_mask = s_state ? s_state->depth_mask : 1;
    d->shader_ctrl = s_state ? s_state->shader_control : 0;
    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) { d->tex_off[t] = s_state ? s_state->textures[t].offset : 0; d->tex_fmt[t] = s_state ? s_state->textures[t].format : 0;
        d->tex[t] = (s_state && (s_state->textures[t].control0 & 0x80000000u)) ? tex_with_remap(s_tex[t].tex, s_state->textures[t].control1) : nil; }
    d->surf           = s_state ? s_state->surface_color_offset[0] : 0;
    d->surf_fmt = s_state ? s_state->surface_format : 0; d->color_target = s_state ? s_state->color_target : 0;
    d->surf2 = s_state ? s_state->surface_color_offset[2] : 0; d->surf3 = s_state ? s_state->surface_color_offset[3] : 0;
    d->n_mrt = (d->color_target == 0x13) ? 2 : (d->color_target == 0x17) ? 3 : (d->color_target == 0x1F) ? 4 : 1;
    if (d->fp_addr) (void)guest_fp_library(d->fp_addr, (d->vp >= 0 && s_vps[d->vp].ok) ? d->vp : -1, d->shader_ctrl, d->n_mrt, d->fp_hash);
    d->surf_pitch0 = s_state ? s_state->surface_color_pitch[0] : 0; d->surf1 = s_state ? s_state->surface_color_offset[1] : 0;
    d->surf_w = (s_state && s_state->surface_clip_w) ? s_state->surface_clip_w : s_width;
    d->surf_h = (s_state && s_state->surface_clip_h) ? s_state->surface_clip_h : s_height;
    d->sx = s_state ? s_state->scissor_x : 0; d->sy = s_state ? s_state->scissor_y : 0;
    d->sw = s_state ? s_state->scissor_w : 4096; d->sh = s_state ? s_state->scissor_h : 4096;
    if (s_state) vpdbg_common(d, s_state, primitive, wrote, "inline");

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
typedef struct { u32 offset; u32 w, h; MTLPixelFormat pf; id<MTLTexture> tex; int cleared_frame; unsigned write_seq; } MtlSurface;
static unsigned s_pass_seq;   /* contatore dei pass codificati (per la sincronia colore<->depth) */
/* Formato del colore della superficie (SURFACE_FORMAT bit 0-4): 0xB = FP16,
 * 0xC = FP32, il resto 8 bit. Uncharted tiene la luce e il composito in FP16
 * (HDR): con superfici a 8 bit i valori > 1 si tagliavano e l'esposizione
 * (media 1x1) portava tutto a nero. */
static MTLPixelFormat surface_pixel_format(u32 surf_fmt)
{
    static int s_hdr = -1; if (s_hdr < 0) { const char* e = getenv("RSX_HDR"); s_hdr = (e && *e == '0') ? 0 : 1; }
    u32 c = surf_fmt & 0x1Fu;
    if (s_hdr && c == 0xB) return MTLPixelFormatRGBA16Float;
    if (s_hdr && c == 0xC) return MTLPixelFormatRGBA32Float;
    return MTLPixelFormatBGRA8Unorm;
}
#define MTL_SURF_MAX 64
static MtlSurface s_surf[MTL_SURF_MAX];
static id<MTLTexture> s_surf_dump[MTL_SURF_MAX];
static u32        s_surf_count;
static int s_dump_with_drawlist;   /* 1 = il fotogramma della RSX_DRAWLIST va anche salvato (RSX_DUMP_FRAME impostata) */

static id<MTLTexture> surface_texture(u32 offset, u32 w, u32 h, MTLPixelFormat pf, int* first_use)
{
    for (u32 i = 0; i < s_surf_count; i++) {
        if (s_surf[i].offset != offset) continue;
        if (s_surf[i].w != w || s_surf[i].h != h || s_surf[i].pf != pf) { s_surf[i].tex = nil; break; }
        if (!s_surf[i].tex) break;
        *first_use = (s_surf[i].cleared_frame != (int)s_frame_no);
        s_surf[i].cleared_frame = (int)s_frame_no;
        return s_surf[i].tex;
    }
    if (s_surf_count >= MTL_SURF_MAX) return nil;
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:pf
                                     width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> t = [s_dev newTextureWithDescriptor:td];
    if (!t) return nil;
    u32 slot = s_surf_count;
    for (u32 i = 0; i < s_surf_count; i++) if (s_surf[i].offset == offset) { slot = i; break; }
    if (slot == s_surf_count) s_surf_count++;
    s_surf[slot].offset = offset; s_surf[slot].w = w; s_surf[slot].h = h; s_surf[slot].pf = pf;
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

/* ---- Buffer di profondita' -----------------------------------------------
 * Ogni SURFACE_ZETA_OFFSET ha una texture Depth32Float (attaccata al pass) e
 * una copia BGRA8 "come la vedrebbe il titolo" leggendo lo Z24S8 attraverso
 * una texture A8R8G8B8 (A = Z[23:16], R = Z[15:8], G = Z[7:0], B = stencil):
 * il post-processing di Uncharted legge il depth del passaggio principale
 * (0xF50000) cosi'. La copia si rigenera (kernel depth_to_rgba) prima di ogni
 * pass che la campiona. RSX_DEPTH=0 spegne tutto (nessun attachment). */
typedef struct { u32 offset; u32 w, h; id<MTLTexture> depth; id<MTLTexture> rgba; int cleared_frame; int dirty; unsigned synced_seq; } MtlZeta;
static id<MTLRenderPipelineState> s_rgba_to_depth;   /* colore ARGB8 codificato -> depth (v. zeta_from_color) */
static MtlZeta s_zeta[MTL_SURF_MAX];
static u32 s_zeta_count;
static id<MTLComputePipelineState> s_depth_to_rgba;
static int rsx_depth_enabled(void)
{
    static int e = -1;
    if (e < 0) { const char* v = getenv("RSX_DEPTH"); e = (v && *v == '0') ? 0 : 1; }
    return e;
}
static MtlZeta* zeta_surface(u32 offset, u32 w, u32 h, int* first_use)
{
    for (u32 i = 0; i < s_zeta_count; i++) {
        if (s_zeta[i].offset != offset) continue;
        if (s_zeta[i].w != w || s_zeta[i].h != h) { s_zeta[i].depth = nil; s_zeta[i].rgba = nil; break; }
        if (!s_zeta[i].depth) break;
        *first_use = (s_zeta[i].cleared_frame != (int)s_frame_no);
        s_zeta[i].cleared_frame = (int)s_frame_no;
        return &s_zeta[i];
    }
    u32 slot = s_zeta_count;
    for (u32 i = 0; i < s_zeta_count; i++) if (s_zeta[i].offset == offset) { slot = i; break; }
    if (slot >= MTL_SURF_MAX) return NULL;
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> dt = [s_dev newTextureWithDescriptor:td];
    MTLTextureDescriptor* cd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    cd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> ct = [s_dev newTextureWithDescriptor:cd];
    if (!dt || !ct) return NULL;
    if (slot == s_zeta_count) s_zeta_count++;
    s_zeta[slot].offset = offset; s_zeta[slot].w = w; s_zeta[slot].h = h;
    s_zeta[slot].depth = dt; s_zeta[slot].rgba = ct; s_zeta[slot].cleared_frame = (int)s_frame_no; s_zeta[slot].dirty = 1;
    *first_use = 1;
    { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0;
      if (sd) fprintf(stderr, "[RSX metal] buffer di profondita' 0x%X: %ux%u\n", offset, w, h); }
    return &s_zeta[slot];
}
static MtlZeta* zeta_lookup(u32 offset)
{
    for (u32 i = 0; i < s_zeta_count; i++)
        if (s_zeta[i].offset == offset && s_zeta[i].rgba) return &s_zeta[i];
    return NULL;
}
static MtlZeta* zeta_by_rgba(id<MTLTexture> t)
{
    for (u32 i = 0; i < s_zeta_count; i++)
        if (s_zeta[i].rgba && s_zeta[i].rgba == t) return &s_zeta[i];
    return NULL;
}
static void zeta_refresh_rgba(id<MTLCommandBuffer> cb, MtlZeta* z)
{
    if (!z->dirty || !s_depth_to_rgba) return;
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:s_depth_to_rgba];
    [ce setTexture:z->depth atIndex:0];
    [ce setTexture:z->rgba atIndex:1];
    MTLSize tg = MTLSizeMake(8, 8, 1);
    MTLSize grid = MTLSizeMake((z->w + 7) / 8, (z->h + 7) / 8, 1);
    [ce dispatchThreadgroups:grid threadsPerThreadgroup:tg];
    [ce endEncoding];
    z->dirty = 0;
}
/* Il titolo scrive il depth "risolto" in 0x2D50000 come COLORE (bytes
 * Z24S8 in A8R8G8B8) e poi usa 0x2D50000 come zeta del passaggio di
 * composizione: sul ferro e' la stessa memoria. Qui, prima di un pass con
 * quello zeta, se la superficie di colore omonima e' stata scritta dopo
 * l'ultima sincronia, si ricostruisce il depth dal colore (pass a solo depth
 * con fs_rgba_to_depth). */
static id<MTLDepthStencilState> dss_for(int test, u32 func, int mask);
static void zeta_from_color(id<MTLCommandBuffer> cb, MtlZeta* z)
{
    if (!s_rgba_to_depth) return;
    for (u32 i = 0; i < s_surf_count; i++) {
        if (s_surf[i].offset != z->offset || !s_surf[i].tex) continue;
        if (s_surf[i].write_seq <= z->synced_seq) return;
        if (s_surf[i].tex.pixelFormat != MTLPixelFormatBGRA8Unorm) return;
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.depthAttachment.texture = z->depth;
        rp.depthAttachment.loadAction = MTLLoadActionDontCare;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:s_rgba_to_depth];
        [enc setDepthStencilState:dss_for(1, 7, 1)];
        [enc setFragmentTexture:s_surf[i].tex atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        z->synced_seq = s_surf[i].write_seq; z->dirty = 1;
        { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0; static int _n = 0;
          if (sd && _n++ < 4) fprintf(stderr, "[RSX metal] depth 0x%X ricostruito dal colore\n", z->offset); }
        return;
    }
}
static MTLCompareFunction depth_func_to_metal(u32 f)
{
    switch (f & 7) {
    case 0: return MTLCompareFunctionNever;   case 1: return MTLCompareFunctionLess;
    case 2: return MTLCompareFunctionEqual;   case 3: return MTLCompareFunctionLessEqual;
    case 4: return MTLCompareFunctionGreater; case 5: return MTLCompareFunctionNotEqual;
    case 6: return MTLCompareFunctionGreaterEqual; default: return MTLCompareFunctionAlways; }
}
static id<MTLDepthStencilState> dss_for(int test, u32 func, int mask)
{
    /* RSX_DEPTH_FUNC=<n>: forza la funzione (7 = ALWAYS) per gli esperimenti */
    static int s_ff = -2; if (s_ff == -2) { const char* e = getenv("RSX_DEPTH_FUNC"); s_ff = e ? atoi(e) : -1; }
    if (s_ff >= 0) func = (u32)s_ff;
    static id<MTLDepthStencilState> cache[2][8][2];
    u32 f = test ? (func & 7) : 7; int t = test ? 1 : 0, m = (test && mask) ? 1 : (test ? 0 : 0);
    if (!test) m = 0;
    if (cache[t][f][m]) return cache[t][f][m];
    MTLDepthStencilDescriptor* dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = test ? depth_func_to_metal(func) : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = (test && mask) ? YES : NO;
    cache[t][f][m] = [s_dev newDepthStencilStateWithDescriptor:dd];
    return cache[t][f][m];
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
"struct FsqOut { float4 pos [[position]]; float2 uv; };\n"
"vertex FsqOut vs_fsq(uint vid [[vertex_id]]) { FsqOut o; float2 p = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);\n"
"    o.pos = float4(p, 0.0, 1.0); o.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); return o; }\n"
"struct DepthOnly { float depth [[depth(any)]]; };\n"
"fragment DepthOnly fs_rgba_to_depth(FsqOut in [[stage_in]], texture2d<float> c [[texture(0)]]) {\n"
"    constexpr sampler smp(coord::normalized, filter::nearest);\n"
"    float4 t = c.sample(smp, in.uv); DepthOnly o;\n"
"    o.depth = clamp(dot(float3(floor(t.a * 255.0 + 0.5), floor(t.r * 255.0 + 0.5), floor(t.g * 255.0 + 0.5)), float3(65536.0, 256.0, 1.0)) / 16777215.0, 0.0, 1.0);\n"
"    return o; }\n"
"kernel void depth_to_rgba(depth2d<float, access::read> d [[texture(0)]],\n"
"                          texture2d<float, access::write> o [[texture(1)]],\n"
"                          uint2 g [[thread_position_in_grid]]) {\n"
"    if (g.x >= o.get_width() || g.y >= o.get_height()) return;\n"
"    float z = clamp(d.read(g), 0.0f, 1.0f);\n"
"    uint zi = uint(z * 16777215.0f);\n"
"    o.write(float4(float((zi >> 8) & 255u) / 255.0f, float(zi & 255u) / 255.0f, 0.0f, float(zi >> 16) / 255.0f), g);\n"
"}\n"
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
/* MtlTexUnit / s_tex: dichiarate prima di record_draw (vedi sopra). */
/* Cache delle texture decodificate, per (offset, formato, dimensioni): il
 * titolo alterna decine di texture fra un draw e l'altro, e ridecodificarle
 * a ogni cambio di unita' costava una decodifica per draw. Evizione a fine
 * fotogramma delle voci non usate da un po'. */
typedef struct { u32 offset, format, image_rect; id<MTLTexture> tex; unsigned long last_use; } MtlTexEntry;
/* 2048 voci erano poche per il livello (sessione 28: cache piena, 5-7.000
 * ridecodifiche per 100 fotogrammi, 1,3 Gpixel): 8192 voci e un indice hash
 * al posto della scansione lineare a ogni bind. */
#define MTL_TEXCACHE 32768   /* il livello dive-boat lega >8.000 texture piccole distinte per fotogramma (RSX_TEXCACHE_DBG) */
#define MTL_TEXHASH  (MTL_TEXCACHE * 4)
static MtlTexEntry s_texcache[MTL_TEXCACHE];
static u32 s_texhash[MTL_TEXHASH];   /* indice+1 nella cache, 0 = vuoto (open addressing, sonda lineare) */
static unsigned long s_tex_decodes, s_tex_decode_px;   /* per RSX_FRAMESTAT */
static u32 s_texcache_n;

static u32 texhash_slot(u32 offset, u32 format, u32 image_rect)
{
    u32 h = offset * 2654435761u ^ (format * 40503u) ^ (image_rect * 2246822519u);
    return (h ^ (h >> 16)) & (MTL_TEXHASH - 1);
}
static id<MTLTexture> texcache_find(u32 offset, u32 format, u32 image_rect)
{
    for (u32 hs = texhash_slot(offset, format, image_rect), n = 0; n < MTL_TEXHASH; n++, hs = (hs + 1) & (MTL_TEXHASH - 1)) {
        u32 ix = s_texhash[hs];
        if (!ix) return nil;
        MtlTexEntry* e = &s_texcache[ix - 1];
        if (e->tex && e->offset == offset && e->format == format && e->image_rect == image_rect) {
            e->last_use = s_frame_no; return e->tex; }
    }
    return nil;
}
/* Ricostruisce l'indice hash dalle voci vive (dopo un'evizione). */
static void texhash_rebuild(void)
{
    memset(s_texhash, 0, sizeof s_texhash);
    for (u32 i = 0; i < s_texcache_n; i++) {
        if (!s_texcache[i].tex) continue;
        u32 hs = texhash_slot(s_texcache[i].offset, s_texcache[i].format, s_texcache[i].image_rect);
        while (s_texhash[hs]) hs = (hs + 1) & (MTL_TEXHASH - 1);
        s_texhash[hs] = i + 1;
    }
}
static void texcache_add(u32 offset, u32 format, u32 image_rect, id<MTLTexture> t)
{
    MtlTexEntry* e = NULL;
    if (s_texcache_n < MTL_TEXCACHE) e = &s_texcache[s_texcache_n++];
    else {
        for (u32 i = 0; i < s_texcache_n; i++) if (!s_texcache[i].tex) { e = &s_texcache[i]; break; }
        if (!e) {
            /* piena: la voce piu' vecchia NON usata in questo fotogramma (i draw
             * registrati la puntano senza ritenerla) */
            unsigned long best = (unsigned long)-1;
            for (u32 i = 0; i < s_texcache_n; i++)
                if (s_texcache[i].last_use < s_frame_no && s_texcache[i].last_use < best) { best = s_texcache[i].last_use; e = &s_texcache[i]; }
            if (!e) return;
            e->tex = nil; texhash_rebuild();
        }
    }
    e->offset = offset; e->format = format; e->image_rect = image_rect; e->tex = t; e->last_use = s_frame_no;
    { u32 hs = texhash_slot(offset, format, image_rect);
      while (s_texhash[hs]) hs = (hs + 1) & (MTL_TEXHASH - 1);
      s_texhash[hs] = (u32)(e - s_texcache) + 1; }
}
/* SET_TEXTURE_CONTROL1 (remap): CELL_GCM_REMAP_MODE -- bit [1:0] sorgente di
 * A, [3:2] di R, [5:4] di G, [7:6] di B (0=A 1=R 2=G 3=B), bit [15:8] il tipo
 * per uscita (0=ZERO 1=ONE 2=REMAP). Default 0xAAE4 = identita'. L'atlante
 * dei caratteri del menu (4 pagine di glifi nei 4 canali) usa 0xAA1B: senza
 * il remap il fragment program sceglieva il canale sbagliato e il testo
 * usciva con i glifi di un'altra pagina. Vista Metal con swizzle, in cache
 * per (texture, remap). */
typedef struct { __unsafe_unretained id<MTLTexture> base; u32 remap; id<MTLTexture> view; } MtlRemapEntry;
static MtlRemapEntry s_remaps[256];
static u32 s_remap_n;
static id<MTLTexture> tex_with_remap(id<MTLTexture> t, u32 remap)
{
    remap &= 0xFFFFu;
    if (!t || remap == 0xAAE4u) return t;
    for (u32 i = 0; i < s_remap_n; i++)
        if (s_remaps[i].base == t && s_remaps[i].remap == remap) return s_remaps[i].view;
    MTLTextureSwizzle sw[4];   /* uscita R,G,B,A */
    static const MTLTextureSwizzle from[4] = { MTLTextureSwizzleAlpha, MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue };
    /* posizione dei campi: A=0, R=1, G=2, B=3 (in quest'ordine nel registro) */
    for (int out = 0; out < 4; out++) {
        int field = (out == 3) ? 0 : out + 1;              /* uscita R->campo1, G->2, B->3, A->0 */
        u32 src  = (remap >> (field * 2)) & 3u;
        u32 kind = (remap >> (8 + field * 2)) & 3u;
        sw[out] = (kind == 0) ? MTLTextureSwizzleZero : (kind == 1) ? MTLTextureSwizzleOne : from[src];
    }
    MTLTextureSwizzleChannels ch = MTLTextureSwizzleChannelsMake(sw[0], sw[1], sw[2], sw[3]);
    id<MTLTexture> v = [t newTextureViewWithPixelFormat:t.pixelFormat textureType:t.textureType
                                                 levels:NSMakeRange(0, t.mipmapLevelCount)
                                                 slices:NSMakeRange(0, t.arrayLength) swizzle:ch];
    if (!v) return t;
    if (s_remap_n < 256) { s_remaps[s_remap_n].base = t; s_remaps[s_remap_n].remap = remap; s_remaps[s_remap_n].view = v; s_remap_n++; }
    { static int _n = 0; if (_n++ < 8) fprintf(stderr, "[RSX metal] texture remap %04X -> vista con swizzle\n", remap); }
    return v;
}
/* A fine fotogramma (dopo la codifica): via le voci ferme da 600 fotogrammi. */
static void texcache_sweep(void)
{
    int evicted = 0;
    for (u32 i = 0; i < s_texcache_n; i++)
        if (s_texcache[i].tex && s_frame_no > s_texcache[i].last_use + 600) {
            /* anche le viste con remap di questa texture */
            for (u32 k = 0; k < s_remap_n; k++)
                if (s_remaps[k].base == s_texcache[i].tex) { s_remaps[k] = s_remaps[s_remap_n - 1]; s_remaps[s_remap_n - 1].view = nil; s_remap_n--; k--; }
            s_texcache[i].tex = nil; evicted = 1;
        }
    if (evicted) texhash_rebuild();
    /* RSX_TEXCACHE_DBG=1: una volta, a cache piena, quanti offset distinti
     * e quante varianti (formato/dimensioni) per offset. */
    { static int s_td = -1; if (s_td < 0) s_td = getenv("RSX_TEXCACHE_DBG") ? 1 : 0;
      static int done = 0;
      if (s_td && !done && s_texcache_n >= MTL_TEXCACHE) { done = 1;
          u32 distinct = 0, multi = 0;
          for (u32 i = 0; i < s_texcache_n; i++) { int first = 1, variants = 0;
              for (u32 j = 0; j < s_texcache_n; j++) if (s_texcache[j].offset == s_texcache[i].offset) { if (j < i) { first = 0; break; } variants++; }
              if (first) { distinct++; if (variants > 1) multi++; } }
          fprintf(stderr, "[texcache] %u voci: %u offset distinti, %u con piu' varianti (formato/dimensioni)\n", s_texcache_n, distinct, multi);
          for (u32 i = 0, shown = 0; i < s_texcache_n && shown < 12; i++) { int v = 0;
              for (u32 j = 0; j < s_texcache_n; j++) if (s_texcache[j].offset == s_texcache[i].offset) v++;
              if (v > 4) { shown++; fprintf(stderr, "[texcache]   offset 0x%08X: %d varianti, es. fmt=0x%X rect=0x%08X\n", s_texcache[i].offset, v, s_texcache[i].format, s_texcache[i].image_rect); } } } }
}
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
     * va perso. Va PRIMA della cache: Uncharted campionava la superficie del
     * passaggio principale (0x7D0000, RGBA16F letta come 2560x720) e la cache
     * rispondeva con la decodifica nera fatta prima che la superficie
     * esistesse -- tutta la catena di post-processing usciva nera. */
    /* Prima la superficie di colore, poi il depth: Uncharted ricopia il depth
     * principale (0xF50000) in 0x2D50000 come colore A8R8G8B8, e poi usa
     * 0x2D50000 sia come zeta del passaggio di luce sia come texture -- la
     * texture giusta e' la copia di colore che il titolo stesso ha scritto. */
    { id<MTLTexture> st = surface_texture_lookup(tex->offset);
      if (!st) { MtlZeta* z = rsx_depth_enabled() ? zeta_lookup(tex->offset) : NULL;
      if (z) { u->tex = z->rgba; u->offset = tex->offset; u->format = tex->format; u->image_rect = tex->image_rect;
               { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0; static int _n = 0;
                 if (sd && _n++ < 8) fprintf(stderr, "[RSX metal] unita' %u legge il buffer di profondita' 0x%X\n", unit, tex->offset); }
               return; } } }
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
    /* la chiave ignora il numero di mipmap (bit 19:16 del formato): la
     * texture Metal non ha mip e lo stesso offset compariva con 5 varianti */
    { id<MTLTexture> ct = texcache_find(tex->offset, tex->format & ~0xF0000u, tex->image_rect);
      if (ct) { u->tex = ct; u->offset = tex->offset; u->format = tex->format;
                u->image_rect = tex->image_rect; return; } }

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
    s_tex_decodes++; s_tex_decode_px += (unsigned long)width * height;
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
        texcache_add(tex->offset, tex->format & ~0xF0000u, tex->image_rect, mt);
        { static int _tn = 0; if (_tn++ < 64)
          fprintf(stderr, "[RSX metal] texture unit %u: %ux%u fmt=0x%02X\n",
                  unit, width, height, fmt); }
        /* RSX_TEX_DBG=1: quanti texel non neri sono davvero atterrati. Una
         * texture decodificata male e una texture vuota dal backend si vedono
         * uguali (nero), e da li' non si riparte. */
        /* RSX_TEX_SAVE=1: la texture decodificata su file. Se la geometria si
         * dipinge con una tinta ma resta nera con lo shader vero, o e' la
         * texture o sono le coordinate; questo separa i due casi. */
        { static int ts = -1; static long ts_off = -1;
          if (ts < 0) { ts = getenv("RSX_TEX_SAVE") ? 1 : 0;
                        /* RSX_TEX_SAVE_OFF=<hex>: solo la texture a quell'offset */
                        const char* eo = getenv("RSX_TEX_SAVE_OFF"); ts_off = eo ? (long)strtoul(eo, 0, 16) : -1; }
          static int tn = 0;
          if (ts && tn < 4 && (ts_off < 0 || (long)(tex->offset & 0x7FFFFFFFu) == ts_off)) {
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
typedef struct { u32 fp_addr; u32 hash; int vp; u32 ctrl; u32 mrt; id<MTLLibrary> lib; volatile int done; } GuestFpEntry;
/* Hash del microcodice (FNV-1a sui primi rsx_fp_program_size byte): Uncharted
 * riusa gli stessi indirizzi in memoria principale (0x0012xxxx) per programmi
 * diversi, e una cache per indirizzo restituiva la traduzione di un programma
 * precedente (a schermo: il tonemap 0x00121602 tradotto da un programma di
 * una istruzione, `h0 = (0,0,0,1)` = nero). */
static u32 fp_ucode_hash(u32 fp_addr)
{
    extern u32 rsx_fp_program_size(const u8* ucode, u32 max_bytes);
    const u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (!vm_base || !off) return 0;
    const u8* p = vm_base + off;
    u32 n = rsx_fp_program_size(p, 4096); if (!n || n > 4096) n = 4096;
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h ? h : 1u;
}
#define MTL_FP_CACHE 8192   /* il livello passa i 512 programmi (sessione 28: cache piena = scena piatta) */
#define MTL_FP_HASH  (MTL_FP_CACHE * 4)
static GuestFpEntry s_fp_cache[MTL_FP_CACHE];
static u32          s_fp_hash[MTL_FP_HASH];   /* indice+1, 0 = vuoto */
static u32          s_fp_count;
static u32 fp_slot(u32 hash, int vp, u32 ctrl, u32 mrt) { u32 h = hash * 2654435761u ^ ((u32)vp * 40503u) ^ (ctrl * 2246822519u) ^ (mrt * 3266489917u); h ^= h >> 15; return h & (MTL_FP_HASH - 1); }

static id<MTLLibrary> guest_fp_library(u32 fp_addr, int vp, u32 ctrl, u32 mrt, u32 hash)
{
    extern void rsx_fp_set_mrt_count(int n);
    if (!hash) hash = fp_ucode_hash(fp_addr);
    for (u32 hs = fp_slot(hash, vp, ctrl, mrt), n = 0; n < MTL_FP_HASH; n++, hs = (hs + 1) & (MTL_FP_HASH - 1)) {
        u32 ix = s_fp_hash[hs]; if (!ix) break;
        GuestFpEntry* e = &s_fp_cache[ix - 1];
        if (e->hash == hash && e->vp == vp && e->ctrl == ctrl && e->mrt == mrt)
            return __atomic_load_n(&e->done, __ATOMIC_ACQUIRE) ? e->lib : nil; }   /* nil = in compilazione o fallito */
    /* RSX_FP_CACHE_MAX=<n>: tetto artificiale (A/B: con 512 il livello non
     * si fermava ma la scena era piatta, sessione 28) */
    static long s_fpmax = -1; if (s_fpmax < 0) { const char* e = getenv("RSX_FP_CACHE_MAX"); s_fpmax = e ? atol(e) : (long)(sizeof(s_fp_cache) / sizeof(s_fp_cache[0])); }
    if (s_fp_count >= (u32)s_fpmax) {
        static int _n = 0; if (_n++ < 4) fprintf(stderr, "[RSX metal] cache dei fragment program PIENA (%u): draw con la coppia incorporata\n", s_fp_count);
        return nil; }

    id<MTLLibrary> lib = nil; int s_fp_sync_done = 1;   /* 0 solo quando la compilazione e' stata affidata al blocco */
    const u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (vm_base && off) {
        static char msl[32768];
        /* la parola di controllo vera (0x1D60) solo per l'export della
         * profondita': per il resto restano le euristiche (RSX_FP_CTRL_AUTO) */
        rsx_fp_set_mrt_count((int)mrt);
        /* La parola di controllo vera (0x1D60) decide anche il registro di
         * uscita: 0x40 = r0 (32 bit), altrimenti h0. L'euristica sceglieva r0
         * appena qualcuno lo scriveva come temporaneo: il resolve del depth di
         * Uncharted (0x0F989E01) usa r0 per i calcoli e consegna il texel in
         * h0, e finiva a schermo il temporaneo (l'immagine "blu"). */
        const int n = rsx_fp_decompile_lang(vm_base + off, 4096, ctrl ? ctrl : RSX_FP_CTRL_AUTO,
                                            0u, 0, RSX_FP_LANG_MSL,
                                            msl, sizeof(msl), NULL);
        if (n > 0) {
            /* Con un VP del guest il vertex shader e' il suo (stessa
             * PSInput); altrimenti quello incorporato con la MVP. */
            /* fs_tint_guest: tinta fissa con il VP del guest (RSX_DEBUG_TINT),
             * per vedere DOVE cade la geometria trasformata dal titolo. */
            static NSString* const kTintGuest =
                @"fragment float4 fs_tint_guest(PSInput in [[stage_in]]) { return float4(1.0, 0.2, 0.6, 1.0); }\n"
                 "fragment float4 fs_depth_guest(PSInput in [[stage_in]]) { float z = in.position.z; return float4(z, fract(z * 16.0), fract(z * 256.0), 1.0); }\n";
            NSString* src = (vp >= 0)
                ? [NSString stringWithFormat:@"%s%s%@", msl, s_vps[vp].msl, kTintGuest]
                : [NSString stringWithFormat:@"%s%@%@", msl, kGuestVertexMSL, kTintGuest];
            NSError* err = nil;
            /* Posizione invariante e niente fast-math: il passaggio di colore
             * di Uncharted usa il test EQUAL sullo z scritto dal suo z-prepass
             * con un ALTRO vertex program; con riordini/FMA diversi fra i due
             * lo z non combaciava bit a bit e il passaggio di colore spariva. */
            MTLCompileOptions* co = [[MTLCompileOptions alloc] init];
            co.fastMathEnabled = NO;
            co.preserveInvariance = YES;
            /* RSX_FP_DUMP=1: l'MSL tradotto. Quando la geometria rasterizza
             * (provato con la tinta) ma resta nera, la risposta e' qui. */
            { static long fd = -2;   /* RSX_FP_DUMP=1: tutti; =<hex>: solo quel fp_addr */
              if (fd == -2) { const char* e = getenv("RSX_FP_DUMP"); fd = e ? (strlen(e) > 2 ? (long)strtoul(e, 0, 16) : atol(e)) : 0; }
              if (fd == 1 || (fd > 1 && (u32)fd == fp_addr)) fprintf(stderr, "---- MSL fp 0x%08X/%08X ----\n%s\n----\n", fp_addr, hash, msl); }
            /* Compilazione ASINCRONA (sessione 28): il livello ha migliaia di
             * fragment program e ogni newLibraryWithSource costa 50-100 ms sul
             * thread del walker (misurato: passate da 5 s, 85% del tempo nel
             * compilatore). La voce entra in cache subito senza libreria: i
             * draw intanto usano la coppia incorporata, e appena la
             * compilazione finisce i draw seguenti trovano il programma.
             * RSX_FP_SYNC=1 ripristina la compilazione sincrona. */
            static int s_sync = -1; if (s_sync < 0) s_sync = getenv("RSX_FP_SYNC") ? 1 : 0;
            if (s_sync) {
                lib = [s_dev newLibraryWithSource:src options:co error:&err];
                if (!lib && vp >= 0) {
                    fprintf(stderr, "[RSX metal] guest vp %08X + fp 0x%08X compile failed: %s\n",
                            s_vps[vp].hash, fp_addr, [[err localizedDescription] UTF8String]);
                    s_vps[vp].ok = 0;   /* i draw futuri con questo VP: percorso incorporato */
                } else if (!lib)
                    fprintf(stderr, "[RSX metal] guest fp 0x%08X compile failed: %s\n",
                            fp_addr, [[err localizedDescription] UTF8String]);
                else
                    fprintf(stderr, "[RSX metal] guest fp 0x%08X/%08X: %d instructions\n", fp_addr, hash, n);
            } else {
                /* coda SERIALE a bassa priorita': non blocca mai il walker
                 * (un semaforo a 6 lo teneva fermo 40 s all'ingresso nel
                 * livello, 2.500 programmi nuovi) e non ruba i core alle
                 * lane SPU; i programmi arrivano uno dopo l'altro. */
                static dispatch_queue_t s_cq;
                if (!s_cq) s_cq = dispatch_queue_create("rsx.fp.compile", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_UTILITY, 0));
                GuestFpEntry* ent = &s_fp_cache[s_fp_count]; ent->done = 0; ent->lib = nil; s_fp_sync_done = 0;
                const int nins = n; const u32 fpa = fp_addr, fph = hash; const int vpi = vp;
                dispatch_async(s_cq, ^{
                    NSError* e2 = nil;
                    id<MTLLibrary> l2 = [s_dev newLibraryWithSource:src options:co error:&e2];
                    if (!l2) {
                        fprintf(stderr, "[RSX metal] guest %sfp 0x%08X compile failed: %s\n", vpi >= 0 ? "vp + " : "", fpa, [[e2 localizedDescription] UTF8String]);
                        if (vpi >= 0) s_vps[vpi].ok = 0;
                    } else {
                        static _Atomic unsigned s_nc = 0; unsigned k = atomic_fetch_add(&s_nc, 1);
                        if (k < 64 || (k % 500) == 0) fprintf(stderr, "[RSX metal] guest fp 0x%08X/%08X: %d instructions (compilato #%u)\n", fpa, fph, nins, k + 1);
                    }
                    ent->lib = l2;
                    __atomic_store_n(&ent->done, 1, __ATOMIC_RELEASE);
                });
            }
        } else {
            fprintf(stderr, "[RSX metal] guest fp 0x%08X: decompile failed\n", fp_addr);
        }
    }

    s_fp_cache[s_fp_count].fp_addr = fp_addr;
    s_fp_cache[s_fp_count].hash    = hash;
    s_fp_cache[s_fp_count].vp      = vp;
    s_fp_cache[s_fp_count].ctrl    = ctrl;
    s_fp_cache[s_fp_count].mrt     = mrt;
    if (lib || s_fp_sync_done) { s_fp_cache[s_fp_count].lib = lib; s_fp_cache[s_fp_count].done = 1; }   /* asincrono: li scrive il blocco */
    { u32 hs = fp_slot(hash, vp, ctrl, mrt); while (s_fp_hash[hs]) hs = (hs + 1) & (MTL_FP_HASH - 1); s_fp_hash[hs] = s_fp_count + 1; }
    s_fp_count++;
    return lib;
}

static u32 pso_key(const MtlDraw* d)
{
    /* The guest program is part of the key: two draws with identical blend
     * state but different fragment programs are different pipelines. */
    /* l'export della profondita' (SHADER_CONTROL & 0xE) cambia il programma */
    const u32 fp = d->fp_addr ? (1u | (((d->fp_hash ? d->fp_hash : d->fp_addr) ^ ((d->shader_ctrl & 0xEu) << 28) ^ (d->pass_pf * 0x9E3779B1u) ^ (d->n_mrt * 0x85EBCA77u)) * 2654435761u)) : 0u;
    const u32 tx = d->tex0 ? 0x40000000u : 0u;
    if (!d->blend_enable) return (fp << 1) | tx;
    return 1u | ((d->blend_sfactor & 0xFFFu) << 1)
              | ((d->blend_dfactor & 0xFFFu) << 13)
              | ((d->blend_equation & 0x7u) << 25)
              | ((d->pass_pf == MTLPixelFormatRGBA16Float) ? 0x10000000u : (d->pass_pf == MTLPixelFormatRGBA32Float) ? 0x20000000u : 0u)
              | (fp & 0x80000000u) | tx;
}

static id<MTLRenderPipelineState> pso_for(MtlDraw* d)
{
    /* Un VP che non compila (ok=0) manda il draw sul percorso incorporato. */
    int vp = (d->vp >= 0 && s_vps[d->vp].ok) ? d->vp : -1;
    u32 key = pso_key(d);
    { id<MTLRenderPipelineState> hit = pso_find(key, vp); if (hit) return hit; }
    if (s_pso_count >= (u32)(sizeof(s_pso_cache) / sizeof(s_pso_cache[0]))) {
        static int _n = 0; if (_n++ < 4) fprintf(stderr, "[RSX metal] cache delle pipeline PIENA (%u): draw con la coppia incorporata\n", s_pso_count);
        return nil; }

    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    if (vp >= 0) {
        u32 rank = 0;
        for (int a = 0; a < 16; a++) {
            if (!(s_vps[vp].mask & (1u << a))) continue;
            vd.attributes[a].format      = MTLVertexFormatFloat4;
            vd.attributes[a].offset      = (NSUInteger)(rank * 16);
            vd.attributes[a].bufferIndex = MTL_VB_INDEX;
            rank++;
        }
        vd.layouts[MTL_VB_INDEX].stride = rank * 16;
    } else {
        for (int i = 0; i < 3; i++) {
            vd.attributes[i].format      = MTLVertexFormatFloat4;
            vd.attributes[i].offset      = (NSUInteger)(i * 16);
            vd.attributes[i].bufferIndex = MTL_VB_INDEX;
        }
        vd.layouts[MTL_VB_INDEX].stride = sizeof(MtlVertex);
    }

    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    /* The guest's own fragment program when it has one and it translated;
     * otherwise the built-in pair. */
    /* RSX_FORCE_BUILTIN=1: ignora il fragment program del guest e usa la
     * coppia incorporata. E' la prova A/B che separa "la geometria non
     * rasterizza" da "rasterizza ma il programma del titolo la dipinge di
     * nero". */
    static int s_fb = -1;
    if (s_fb < 0) s_fb = getenv("RSX_FORCE_BUILTIN") ? 1 : 0;
    id<MTLLibrary> glib = (d->fp_addr && !s_fb) ? guest_fp_library(d->fp_addr, vp, d->shader_ctrl, d->n_mrt ? d->n_mrt : 1, d->fp_hash) : nil;
    if (!glib && vp >= 0) {
        /* il VP non compila: si riprova la coppia con il VS incorporato */
        vp = -1; d->vp = -1;
        { id<MTLRenderPipelineState> hit = pso_find(key, vp); if (hit) return hit; }
        vd = [MTLVertexDescriptor vertexDescriptor];
        for (int i = 0; i < 3; i++) {
            vd.attributes[i].format      = MTLVertexFormatFloat4;
            vd.attributes[i].offset      = (NSUInteger)(i * 16);
            vd.attributes[i].bufferIndex = MTL_VB_INDEX;
        }
        vd.layouts[MTL_VB_INDEX].stride = sizeof(MtlVertex);
        glib = (d->fp_addr && !s_fb) ? guest_fp_library(d->fp_addr, -1, d->shader_ctrl, d->n_mrt ? d->n_mrt : 1, d->fp_hash) : nil;
    }
    if (glib) {
        static int s_dtg = -1;
        if (s_dtg < 0) { const char* e = getenv("RSX_DEBUG_TINT");
                         /* RSX_DEBUG_TINT=2: solo i draw con blend (l'HUD/menu
                          * sopra la scena), per non coprirli col composito. */
                         s_dtg = e ? atoi(e) : 0; if (e && !s_dtg) s_dtg = 1; }
        int tint = s_dtg == 1 || (s_dtg == 2 && d->blend_enable);
        pd.vertexFunction   = [glib newFunctionWithName:@"vs_guest"];
        /* RSX_DEBUG_TINT=3: il colore e' lo z del frammento (in.position.z) */
        pd.fragmentFunction = [glib newFunctionWithName:(s_dtg == 3) ? @"fs_depth_guest" : tint ? @"fs_tint_guest" : @"rsx_fp_main"];
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
                               : (s_ta && d->tex0 && d->tex[0]) ? @"fs_tex_arr"
                               : ((d->tex0 && d->tex[0]) ? @"fs_tex" : @"fs_main")];
    }
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = d->pass_pf ? (MTLPixelFormat)d->pass_pf : MTLPixelFormatBGRA8Unorm;
    for (u32 k = 1; k < d->n_mrt && k < 4; k++) pd.colorAttachments[k].pixelFormat = pd.colorAttachments[0].pixelFormat;
    if (rsx_depth_enabled()) pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    if (d->blend_enable) for (u32 k = 0; k < (d->n_mrt ? d->n_mrt : 1) && k < 4; k++) {
        MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[k];
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
    s_pso_cache[s_pso_count].vp  = vp;
    s_pso_cache[s_pso_count].pso = pso;
    pso_index_add(s_pso_count);
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
        s_vpd = (float*)malloc(sizeof(float) * MTL_VP_FLOATS);
        s_cbs = (u8*)malloc((size_t)MTL_CB_BYTES * MTL_CB_MAX);
        if (!s_vpd || !s_cbs) {
            fprintf(stderr, "[RSX metal] vp staging alloc failed\n");
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
        { MTLRenderPipelineDescriptor* rd = [[MTLRenderPipelineDescriptor alloc] init];
          rd.vertexFunction = [s_shader_lib newFunctionWithName:@"vs_fsq"];
          rd.fragmentFunction = [s_shader_lib newFunctionWithName:@"fs_rgba_to_depth"];
          rd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
          NSError* rerr = nil;
          s_rgba_to_depth = [s_dev newRenderPipelineStateWithDescriptor:rd error:&rerr];
          if (!s_rgba_to_depth) fprintf(stderr, "[RSX metal] rgba_to_depth: %s\n", rerr ? [[rerr localizedDescription] UTF8String] : "?"); }
        { id<MTLFunction> kf = [s_shader_lib newFunctionWithName:@"depth_to_rgba"];
          NSError* kerr = nil;
          s_depth_to_rgba = kf ? [s_dev newComputePipelineStateWithFunction:kf error:&kerr] : nil;
          if (!s_depth_to_rgba) fprintf(stderr, "[RSX metal] depth_to_rgba: %s\n", kerr ? [[kerr localizedDescription] UTF8String] : "funzione assente"); }

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

static uint8_t s_keys[256];
int rsx_metal_key_down(unsigned keycode) { return keycode < 256 ? s_keys[keycode] : 0; }

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
            /* La tastiera come pad (sessione 27): i tasti si registrano qui e
             * cellPad li legge (rsx_metal_key_down); non si inoltrano alla
             * finestra, che altrimenti suona il "bip" del tasto ignorato. */
            if (ev.type == NSEventTypeKeyDown || ev.type == NSEventTypeKeyUp) {
                unsigned short kc = ev.keyCode;
                if (kc < 256) s_keys[kc] = (ev.type == NSEventTypeKeyDown) ? 1 : 0;
                continue;
            }
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
        /* RSX_FRAMESTAT=<n>: una riga ogni n fotogrammi presentati, con il
         * numero di draw: la misura piu' economica di "il gioco va avanti". */
        { static int s_fs = -2; static unsigned long s_acc = 0, s_nz = 0;
          if (s_fs == -2) { const char* e = getenv("RSX_FRAMESTAT"); s_fs = (e && *e) ? atoi(e) : 0; }
          s_acc += s_draw_count; if (s_draw_count) s_nz++;
          if (s_fs > 0 && (s_frame_no % (unsigned long)s_fs) == 0) {
              extern unsigned long long ps3_ms_now(void);
              fprintf(stderr, "[RSX metal] fotogramma #%lu: negli ultimi %d presentati %lu draw in %lu fotogrammi non vuoti, t=%llums decodifiche=%lu (%lu Mpx) cache=%u\n",
                      s_frame_no, s_fs, s_acc, s_nz, ps3_ms_now(), s_tex_decodes, s_tex_decode_px >> 20, s_texcache_n);
              s_tex_decodes = 0; s_tex_decode_px = 0; s_acc = 0; s_nz = 0; } }
        /* RSX_DRAWLIST=<n>: l'elenco dei draw del fotogramma n (o di ogni
         * fotogramma con piu' di 5 draw quando n = 0 e' il primo tale):
         * superficie, programmi, texture, ritaglio, blend. La radiografia di
         * un fotogramma nero. */
        { static long s_dl = -2; static int s_done = 0;
          if (s_dl == -2) { const char* e = getenv("RSX_DRAWLIST"); s_dl = e ? atol(e) : -1; }
          /* RSX_DRAWLIST_MIN=<n>: il primo fotogramma con almeno n draw */
          static long s_dmin = -2; if (s_dmin == -2) { const char* e = getenv("RSX_DRAWLIST_MIN"); s_dmin = e ? atol(e) : -1; }
          static long s_daft = -2; if (s_daft == -2) { const char* e = getenv("RSX_DRAWLIST_AFTER"); s_daft = e ? atol(e) : 0; }
          if (!s_done && ((s_dl > 0 && (long)s_frame_no == s_dl) || (s_dl == 0 && s_draw_count > 5) ||
                          (s_dmin > 0 && (long)s_draw_count >= s_dmin && (long)s_frame_no >= s_daft))) {
              s_done = 1; s_dump_with_drawlist = 1;
              fprintf(stderr, "[drawlist] fotogramma #%lu: %u draw, %u vertici, %u blocchi di costanti\n",
                      s_frame_no, s_draw_count, s_vert_count, s_cb_count);
              for (u32 i = 0; i < s_draw_count; i++) {
                  const MtlDraw* d = &s_draws[i];
                  fprintf(stderr, "[drawlist] #%-3u surf=%08X%s[fmt=%X ct=%X pitch=%u B=%X Z=%X dt=%d/%X/%d sc=%X] vp=%s%08X fp=%08X/%08X n=%-5u topo=%d blend=%d(%03X,%03X) sc=%u,%u %ux%u tex:",
                          i, d->surf, cellGcmOffsetIsDisplay(d->surf) ? "*" : " ", d->surf_fmt, d->color_target, d->surf_pitch0, d->surf1, d->zeta_off, d->depth_test, d->depth_func, d->depth_mask, d->shader_ctrl,
                          d->vp >= 0 ? "" : "-", d->vp >= 0 ? s_vps[d->vp].hash : 0u, d->fp_addr, d->fp_hash, d->count, (int)d->topology,
                          d->blend_enable, d->blend_sfactor, d->blend_dfactor, d->sx, d->sy, d->sw, d->sh);
                  /* il microcodice del FP e' ancora quello del draw? (anello riusato) */
                  if (d->fp_addr) { u32 hn = fp_ucode_hash(d->fp_addr); if (hn != d->fp_hash) fprintf(stderr, " fp-ora=%08X", hn); }
                  for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) if (d->tex[t])
                      fprintf(stderr, " %u:%lux%lu@%X/f%02X", t, (unsigned long)d->tex[t].width, (unsigned long)d->tex[t].height, d->tex_off[t], (d->tex_fmt[t] >> 8) & 0xFF);
                  fprintf(stderr, "\n");
              }
          } }
        id<MTLCommandBuffer> cb = [s_queue commandBuffer];

        /* Visibility buffer del fotogramma (anello di 3: il precedente puo'
         * essere ancora in volo sulla GPU). Zero: i draw non codificati
         * (RSX_DRAW_LIMIT, pass saltati) contano 0. */
        id<MTLBuffer> vis = nil;
        if (s_report_count) {
            if (!s_vis_buf[s_vis_idx]) s_vis_buf[s_vis_idx] = [s_dev newBufferWithLength:(NSUInteger)MTL_MAX_DRAWS * 8 options:MTLResourceStorageModeShared];
            vis = s_vis_buf[s_vis_idx]; s_vis_idx = (s_vis_idx + 1) % MTL_VIS_RING;
            if (vis) memset([vis contents], 0, (size_t)MTL_MAX_DRAWS * 8);
        }

        /* Un passaggio di rendering per ogni tratto CONSECUTIVO di draw che
         * condividono la superficie: e' l'ordine in cui il titolo li ha
         * emessi, e cambiare superficie e' esattamente cambiare bersaglio.
         * Il drawable si pulisce una volta sola, all'inizio del fotogramma. */
        id<MTLBuffer> vb = nil;
        if (s_draw_count > 0 && s_vert_count > 0)
            vb = [s_dev newBufferWithBytes:s_verts
                                    length:sizeof(MtlVertex) * s_vert_count
                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> vpb = nil, cbb = nil;
        if (s_draw_count > 0 && s_vpd_used > 0)
            vpb = [s_dev newBufferWithBytes:s_vpd length:sizeof(float) * s_vpd_used
                                    options:MTLResourceStorageModeShared];
        if (s_draw_count > 0 && s_cb_count > 0)
            cbb = [s_dev newBufferWithBytes:s_cbs length:(NSUInteger)MTL_CB_BYTES * s_cb_count
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
                    /* Antialiasing della superficie (SURFACE_FORMAT bit 12-15):
                     * 3 = DIAGONAL_CENTERED_2 (memoria larga 2x), 4/5 = 4 campioni
                     * (2x2). Uncharted disegna il passaggio principale a 2x
                     * (pitch 10240 = 2560x4) e il post-processing rilegge il
                     * buffer dei campioni come texture 2560x720: la texture della
                     * superficie ha la dimensione fisica, il disegno ci si
                     * stira dentro, e la rilettura per offset combacia. */
                    u32 aa = (s_draws[i].surf_fmt >> 12) & 0xF, ax = (aa == 3 || aa >= 4) ? 2 : 1, ay = (aa >= 4) ? 2 : 1;
                    id<MTLTexture> st = surface_texture(surf, s_draws[i].surf_w * ax, s_draws[i].surf_h * ay, surface_pixel_format(s_draws[i].surf_fmt), &first_use);
                    if (st) pass_target = st; else to_display = 1;
                } else to_display = 1;
            }
            u32 pass_w = (u32)pass_target.width, pass_h = (u32)pass_target.height;
            for (u32 q = i; q < run_end; q++) s_draws[q].pass_pf = (u32)pass_target.pixelFormat;
            u32 sc_ax = (pass_w >= 2 * s_draws[i].surf_w && s_draws[i].surf_w) ? 2 : 1;
            u32 sc_ay = (pass_h >= 2 * s_draws[i].surf_h && s_draws[i].surf_h) ? 2 : 1;

            /* Le copie BGRA8 dei depth che questo pass campiona vanno rigenerate prima */
            if (rsx_depth_enabled())
                for (u32 q = i; q < run_end; q++)
                    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++)
                        if (s_draws[q].tex[t]) { MtlZeta* z = zeta_by_rgba(s_draws[q].tex[t]); if (z) zeta_refresh_rgba(cb, z); }
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = pass_target;
            if (vis) rp.visibilityResultBuffer = vis;
            /* MRT: i bersagli B/C/D del primo draw del tratto, stessa dimensione e formato */
            if (!to_display && s_draws[i].n_mrt > 1) {
                u32 offs[3] = { s_draws[i].surf1, s_draws[i].surf2, s_draws[i].surf3 };
                for (u32 k = 1; k < s_draws[i].n_mrt && k < 4; k++) {
                    int fu = 0;
                    id<MTLTexture> mt = surface_texture(offs[k - 1], pass_w, pass_h, (MTLPixelFormat)pass_target.pixelFormat, &fu);
                    if (!mt) continue;
                    rp.colorAttachments[k].texture = mt;
                    rp.colorAttachments[k].loadAction = fu ? MTLLoadActionClear : MTLLoadActionLoad;
                    rp.colorAttachments[k].clearColor = clear_color_from_rsx();
                    rp.colorAttachments[k].storeAction = MTLStoreActionStore;
                }
            }
            MtlZeta* pass_zeta = NULL;
            if (rsx_depth_enabled()) {
                int zfirst = 0;
                /* il display non ha uno zeta "vero": usa l'offset 0 come chiave */
                pass_zeta = zeta_surface(to_display ? 0xFFFFFFFFu : s_draws[i].zeta_off, pass_w, pass_h, &zfirst);
                if (pass_zeta) {
                    if (!zfirst) zeta_from_color(cb, pass_zeta);
                    rp.depthAttachment.texture = pass_zeta->depth;
                    rp.depthAttachment.loadAction = zfirst ? MTLLoadActionClear : MTLLoadActionLoad;
                    rp.depthAttachment.clearDepth = (double)s_clear_depth;
                    { static int sd = -1; if (sd < 0) sd = getenv("RSX_FRAMEDBG") ? 1 : 0; static int _n = 0;
                      if (sd && (long)s_frame_no >= 2000 && _n++ < 60) fprintf(stderr, "[RSX metal] depth 0x%X: %s (draw #%u..%u dt=%d/%X/%d vpz=%f/%f)\n",
                          pass_zeta->offset, zfirst ? "clear" : "load", i, run_end, s_draws[i].depth_test, s_draws[i].depth_func, s_draws[i].depth_mask,
                          s_state ? s_state->viewport_scale[2] : 0.f, s_state ? s_state->viewport_offset[2] : 0.f);
                      if (0) fprintf(stderr, "[RSX metal] depth 0x%X: clear a %f (draw #%u dt=%d/%X/%d vpz=%f/%f)\n",
                          pass_zeta->offset, s_clear_depth, i, s_draws[i].depth_test, s_draws[i].depth_func, s_draws[i].depth_mask,
                          s_state ? s_state->viewport_scale[2] : 0.f, s_state ? s_state->viewport_offset[2] : 0.f); }
                    rp.depthAttachment.storeAction = MTLStoreActionStore;
                    pass_zeta->dirty = 1;
                }
            }
            if (to_display)
                rp.colorAttachments[0].loadAction =
                    drawable_cleared ? MTLLoadActionLoad : MTLLoadActionClear;
            else
                rp.colorAttachments[0].loadAction =
                    first_use ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.colorAttachments[0].clearColor  = clear_color_from_rsx();
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            if (to_display) drawable_cleared = 1;

            /* la superficie di colore di questo pass viene scritta ora */
            s_pass_seq++;
            for (u32 q = 0; q < s_surf_count; q++) if (s_surf[q].tex == pass_target) s_surf[q].write_seq = s_pass_seq;
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            if (vb) {
                [enc setVertexBuffer:vb offset:0 atIndex:MTL_VB_INDEX];
                MTLViewport vp = { 0.0, 0.0, (double)pass_w, (double)pass_h, 0.0, 1.0 };
                [enc setViewport:vp];
            }

            for (; i < run_end; i++) {
                MtlDraw* d = &s_draws[i];
                /* RSX_DRAW_LIMIT=<n>: codifica solo i draw con indice < n (per
                 * fotografare le superfici a meta' fotogramma con RSX_DUMP_FRAME) */
                { static long s_lim = -2; if (s_lim == -2) { const char* e = getenv("RSX_DRAW_LIMIT"); s_lim = e ? atol(e) : -1; }
                  if (s_lim >= 0 && (long)i >= s_lim) continue; }
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
                      NSUInteger x = d->sx * sc_ax, y = d->sy * sc_ay, w = d->sw * sc_ax, h = d->sh * sc_ay;
                      if (x > pass_w)  x = pass_w;
                      if (y > pass_h) y = pass_h;
                      if (w > pass_w  - x) w = pass_w  - x;
                      if (h > pass_h - y) h = pass_h - y;
                      if (w == 0 || h == 0) continue;   /* ritaglio vuoto: niente */
                      MTLScissorRect sr = { x, y, w, h };
                      [enc setScissorRect:sr];
                  } }
                [enc setRenderPipelineState:pso];
                if (pass_zeta) [enc setDepthStencilState:dss_for(d->depth_test, d->depth_func, d->depth_mask)];
                if (d->vp >= 0 && vpb && cbb) {
                    [enc setVertexBuffer:vpb offset:d->vbase atIndex:MTL_VB_INDEX];
                    [enc setVertexBuffer:cbb offset:(NSUInteger)d->cblock * MTL_CB_BYTES atIndex:0];
                } else {
                    if (vb) [enc setVertexBuffer:vb offset:0 atIndex:MTL_VB_INDEX];
                    [enc setVertexBytes:d->mvp length:sizeof(d->mvp) atIndex:0];
                }
                /* The guest's program declares all sixteen units, so bind
                 * every resident one; the built-in sampling path only reads
                 * unit 0. Metal leaves unbound slots null, which is fine for
                 * a shader that does not sample them. */
                if (s_sampler) {
                    for (u32 t = 0; t < RSX_MAX_TEXTURES; t++) {
                        /* OGNI slot, non solo quelli residenti: vedi s_dummy_tex. */
                        [enc setFragmentTexture:(d->tex[t] ? d->tex[t] : s_dummy_tex)
                                        atIndex:t];
                        [enc setFragmentSamplerState:s_sampler atIndex:t];
                    }
                }
                if (vis) [enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:(NSUInteger)i * 8];
                [enc drawPrimitives:d->topology vertexStart:(d->vp >= 0 && vpb && cbb) ? 0 : d->base
                        vertexCount:d->count];
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
        { static int s_df = -2; static int s_done = 0; static int s_dmax = 1; static int s_skip = 0;
          if (s_df == -2) { const char* e = getenv("RSX_DUMP_FRAME");
                            s_df = (e && *e) ? atoi(e) : -1;
                            /* RSX_DUMP_COUNT=<k>: salva i primi k fotogrammi
                             * che superano la soglia (rsx_frame_<i>.ppm).
                             * RSX_DUMP_SKIP=<m>: salta i primi m sopra soglia
                             * (per arrivare al menu senza 400 file). */
                            const char* c = getenv("RSX_DUMP_COUNT");
                            if (c && atoi(c) > 1) s_dmax = atoi(c);
                            const char* k = getenv("RSX_DUMP_SKIP");
                            if (k) s_skip = atoi(k); }
          if ((s_df > 0 && s_done < s_dmax && (int)s_draw_count > s_df && s_skip-- <= 0) || (s_dump_with_drawlist == 1 && s_df != -1)) {
              s_done++; if (s_dump_with_drawlist == 1) s_dump_with_drawlist = 2;
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
                      texture2DDescriptorWithPixelFormat:s_surf[k].tex.pixelFormat
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
                      const int bpp = (s_surf_dump[k].pixelFormat == MTLPixelFormatRGBA16Float) ? 8 : (s_surf_dump[k].pixelFormat == MTLPixelFormatRGBA32Float) ? 16 : 4;
                      u8* sp = (u8*)malloc(SW * SH * bpp);
                      if (!sp) continue;
                      [s_surf_dump[k] getBytes:sp bytesPerRow:SW*bpp
                                    fromRegion:MTLRegionMake2D(0,0,SW,SH) mipmapLevel:0];
                      if (bpp != 4) {   /* FP16/FP32 -> BGRA8 (tone map 1:1 con clamp) */
                          u8* cv = (u8*)malloc(SW * SH * 4);
                          if (cv) { for (NSUInteger q = 0; q < SW*SH; q++) { float c[4];
                                  if (bpp == 8) { const uint16_t* hp = (const uint16_t*)(sp + q*8);
                                      for (int j = 0; j < 4; j++) { uint16_t hv = hp[j]; uint32_t sg = (hv >> 15) & 1, ex = (hv >> 10) & 0x1F, mn = hv & 0x3FF; float f;
                                          if (ex == 0) f = ldexpf((float)mn, -24); else if (ex == 31) f = mn ? 0.f : 65504.f; else f = ldexpf((float)(mn | 0x400), (int)ex - 25);
                                          c[j] = sg ? -f : f; } }
                                  else memcpy(c, sp + q*16, 16);
                                  for (int j = 0; j < 4; j++) { float v = c[j]; if (!(v == v)) v = 0; if (v < 0) v = 0; if (v > 1) v = 1; cv[q*4 + (j == 0 ? 2 : j == 2 ? 0 : j)] = (u8)(v * 255.f + 0.5f); } }
                              free(sp); sp = cv; } }
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
                  /* anche i depth (copia BGRA8 A=Z[23:16] R=Z[15:8] G=Z[7:0]) come
                   * scala di grigi dello z alto: /tmp/rsx_zeta_<offset>.ppm */
                  for (u32 k = 0; k < s_zeta_count; k++) {
                      if (!s_zeta[k].rgba || !s_zeta[k].dirty) continue;
                      id<MTLCommandBuffer> zb = [s_queue commandBuffer];
                      zeta_refresh_rgba(zb, &s_zeta[k]);
                      MTLTextureDescriptor* zd = [MTLTextureDescriptor
                          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                       width:s_zeta[k].w height:s_zeta[k].h mipmapped:NO];
                      zd.storageMode = MTLStorageModeShared;
                      id<MTLTexture> zt = [s_dev newTextureWithDescriptor:zd];
                      id<MTLBlitCommandEncoder> bz = [zb blitCommandEncoder];
                      [bz copyFromTexture:s_zeta[k].rgba sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                             sourceSize:MTLSizeMake(s_zeta[k].w, s_zeta[k].h, 1) toTexture:zt destinationSlice:0 destinationLevel:0
                      destinationOrigin:MTLOriginMake(0,0,0)];
                      [bz endEncoding]; [zb commit]; [zb waitUntilCompleted];
                      NSUInteger ZW = s_zeta[k].w, ZH = s_zeta[k].h;
                      u8* zp = (u8*)malloc(ZW * ZH * 4); if (!zp) continue;
                      [zt getBytes:zp bytesPerRow:ZW*4 fromRegion:MTLRegionMake2D(0,0,ZW,ZH) mipmapLevel:0];
                      { double zmin = 2, zmax = -1, zsum = 0; unsigned nz0 = 0, nz1 = 0, nn = 0;
                        for (NSUInteger q = 0; q < ZW*ZH; q += 37) { unsigned zi = ((unsigned)zp[q*4+3] << 16) | ((unsigned)zp[q*4+2] << 8) | zp[q*4+1];
                            double z = zi / 16777215.0; if (z < zmin) zmin = z; if (z > zmax) zmax = z; zsum += z; nn++; if (zi == 0) nz0++; if (zi == 0xFFFFFF) nz1++; }
                        fprintf(stderr, "[RSX metal] depth 0x%X: z min=%g max=%g media=%g  zero=%u%% uno=%u%%\n", s_zeta[k].offset, zmin, zmax, nn ? zsum / nn : 0, nn ? nz0 * 100 / nn : 0, nn ? nz1 * 100 / nn : 0); }
                      char zp2[128]; snprintf(zp2, sizeof zp2, "/tmp/rsx_zeta_%08X.ppm", s_zeta[k].offset);
                      FILE* zf = fopen(zp2, "wb");
                      if (zf) { fprintf(zf, "P6\n%lu %lu\n255\n", (unsigned long)ZW, (unsigned long)ZH);
                          for (NSUInteger q = 0; q < ZW*ZH; q++) { u8 hi = zp[q*4+3], mid = zp[q*4+2]; u8 g = (u8)((hi * 3 + mid) / 4 * 4 > 255 ? 255 : hi); u8 rgb[3] = { g, g, g }; (void)mid; fwrite(rgb, 1, 3, zf); }
                          fclose(zf); fprintf(stderr, "[RSX metal] depth 0x%X salvato in %s\n", s_zeta[k].offset, zp2); }
                      free(zp);
                  }
                  free(px);
              }
              if (drawable) [drawable present];
              if (!s_headless) dispatch_semaphore_signal(s_inflight);
              s_draw_count = 0; s_vert_count = 0; s_vpd_used = 0; s_cb_count = 0;
              s_report_count = 0; s_report_start = 0;
              return;
          } }

        /* Il present va su un SECONDO command buffer, accodato dopo il
         * rendering: cosi' il completed handler dei report (sotto) scatta
         * quando i passaggi sono finiti, non quando il drawable e' stato
         * mostrato al vsync. Con present e rendering nello stesso buffer il
         * flip del titolo (che aspetta i report, cellGcmTickFlip) restava
         * legato al vsync e il menu andava a 33 fps invece di 55. */
        id<MTLCommandBuffer> cbp = nil;
        if (drawable) { cbp = [s_queue commandBuffer]; [cbp presentDrawable:drawable]; }

        /* Occlusion: a GPU finita somma gli slot di ogni report e scrive il
         * conteggio vero (copia della lista: la prossima registrazione la
         * sovrascrive subito). */
        if (vis && s_report_count) {
            /* il flip del titolo (cellGcmTickFlip) aspetta che questo fotogramma
             * sia finito sulla GPU: i report vanno letti dal job 215 gia' veri */
            atomic_fetch_add(&s_gpu_pending, 1);
            u32 nrep = s_report_count;
            MtlReport* reps = (MtlReport*)malloc(sizeof(MtlReport) * nrep);
            memcpy(reps, s_reports, sizeof(MtlReport) * nrep);
            id<MTLBuffer> visb = vis;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> _u) {
                if (_u.error) { static int _e = 0; if (_e++ < 8) fprintf(stderr, "[RSX metal] command buffer FALLITO: %s\n", [[_u.error localizedDescription] UTF8String]); }
                extern void rsx_report_set_value(u32 ea, u32 val);
                const uint64_t* cnt = (const uint64_t*)[visb contents];
                static int s_dbg = -1; if (s_dbg < 0) s_dbg = getenv("RSX_OCCLUSION_DBG") ? 1 : 0;
                u32 vis_n = 0;
                for (u32 r = 0; r < nrep; r++) {
                    uint64_t sum = 0;
                    for (u32 k = reps[r].first; k < reps[r].last && k < MTL_MAX_DRAWS; k++) sum += cnt[k];
                    u32 v = sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)sum;
                    rsx_report_set_value(reps[r].ea, v);
                    if (v) vis_n++;
                }
                if (s_dbg) fprintf(stderr, "[occlusion] %u report, %u visibili\n", nrep, vis_n);
                free(reps);
                atomic_fetch_sub(&s_gpu_pending, 1);
            }];
        }
        s_report_count = 0; s_report_start = 0;

        if (s_headless) {
            /* The readback below must see a completed frame. */
            [cb commit];
            if (cbp) [cbp commit];
            [cb waitUntilCompleted];
        } else {
            dispatch_semaphore_t sem = s_inflight;
            [(cbp ? cbp : cb) addCompletedHandler:^(id<MTLCommandBuffer> _unused) {
                (void)_unused;
                dispatch_semaphore_signal(sem);
            }];
            [cb commit];        /* no wait: the CPU goes on to the next frame */
            if (cbp) [cbp commit];
        }

        if (s_dropped_draws) {
            fprintf(stderr, "[RSX metal] dropped %u draw(s) this frame (cap %d draws / %u verts)\n",
                    s_dropped_draws, MTL_MAX_DRAWS, MTL_MAX_VERTS);
            s_dropped_draws = 0;
        }
        s_draw_count = 0;
        s_vert_count = 0;
        s_vpd_used = 0;
        s_cb_count = 0;
        texcache_sweep();

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

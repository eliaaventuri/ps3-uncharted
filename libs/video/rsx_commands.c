/*
 * ps3recomp - RSX Command Buffer Processor
 *
 * Parses NV47xx GPU methods from the command buffer and updates RSX state.
 * Dispatches to the registered graphics backend for actual rendering.
 *
 * Command buffer format (NV47xx FIFO):
 *   Each command is a 32-bit header followed by N data words.
 *   Header format: [31:29] type | [28:18] count | [17:13] subchannel | [12:2] method | [1:0] flags
 *
 *   Type 0 (increasing): method, method+4, method+8, ... for each data word
 *   Type 2 (non-increasing): same method repeated for each data word
 *   Type 1 (jump): jump to address in data
 *   Type 3 (call/return): call/return from subroutine
 */

#include "rsx_commands.h"
#include <stdio.h>
#include <stdlib.h>   /* getenv -- an implicit decl returns int, truncating the pointer */
#include <string.h>

#include "../../runtime/memory/vm.h"    /* VM_HLE_INJECT_BASE */
u32 g_rsx_cur_dea;   /* EA della parola dati del metodo in corso (walker FIFO), per i trace */

/* ---------------------------------------------------------------------------
 * Global backend
 * -----------------------------------------------------------------------*/

static rsx_backend* s_backend = NULL;

/* Last NV406E_SET_REFERENCE value seen in the FIFO. The title writes a
 * reference then spins on the GCM control register's `ref` field until the RSX
 * reports it as reached; cellGcmSys mirrors this into the guest control
 * register after each drain so those waits (cellGcmFinish / wait-label) unblock. */
u32 g_rsx_last_reference = 0;

void rsx_set_backend(rsx_backend* backend)
{
    s_backend = backend;
}

rsx_backend* rsx_get_backend(void)
{
    return s_backend;
}

/* ---------------------------------------------------------------------------
 * State initialization
 * -----------------------------------------------------------------------*/

void rsx_state_init(rsx_state* state)
{
    memset(state, 0, sizeof(rsx_state));
    /* RSX reset value for the constant vertex attributes is (0,0,0,1); a zeroed
     * struct would leave w at 0. */
    for (int _i = 0; _i < RSX_MAX_VERTEX_ATTRIBS; _i++)
        state->vertex_data4f[_i][3] = 1.0f;

    /* Default viewport */
    state->viewport_w = 1280;
    state->viewport_h = 720;
    state->clip_min = 0.0f;
    state->clip_max = 1.0f;

    /* Default scissor */
    state->scissor_w = 4096;
    state->scissor_h = 4096;

    /* Default depth */
    state->depth_func = 1; /* LESS */
    state->depth_mask = 1;

    /* Default cull */
    state->cull_face = 1; /* BACK */
    state->front_face = 0; /* CW */

    /* Default color mask: all channels writable (A|R|G|B) */
    state->color_mask = 0x01010101;

    /* Default stencil: sensible initial values */
    state->stencil_func = 0x0207; /* ALWAYS */
    state->stencil_ref = 0;
    state->stencil_mask = 0xFF;
    state->stencil_op_fail = 0x1E00;  /* KEEP */
    state->stencil_op_zfail = 0x1E00; /* KEEP */
    state->stencil_op_zpass = 0x1E00; /* KEEP */

    /* Default alpha test */
    state->alpha_func = 0x0207; /* ALWAYS */
    state->alpha_ref = 0;

    /* Default shader control: 32-bit colour exports (r0..) -- matches every
     * title that never programs the register. */
    state->shader_control = CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS;

    /* Mark everything dirty */
    state->surface_dirty = 1;
    state->viewport_dirty = 1;
    state->blend_dirty = 1;
    state->depth_dirty = 1;
    state->stencil_dirty = 1;
    state->texture_dirty = 1;
    state->vertex_dirty = 1;
    state->color_mask_dirty = 1;
    state->alpha_dirty = 1;
    state->shader_dirty = 1;
}

/* ---------------------------------------------------------------------------
 * Method processing
 * -----------------------------------------------------------------------*/

static int process_surface_method(rsx_state* state, u32 method, u32 data)
{
    switch (method) {
    case NV4097_SET_SURFACE_FORMAT:
        state->surface_format = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_CLIP_HORIZONTAL:
        state->surface_clip_x = data & 0xFFFF;
        state->surface_clip_w = (data >> 16) & 0xFFFF;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_CLIP_VERTICAL:
        state->surface_clip_y = data & 0xFFFF;
        state->surface_clip_h = (data >> 16) & 0xFFFF;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_CONTEXT_DMA_COLOR_A:
        state->context_dma_color = data;
        return 0;
    case NV4097_SET_SURFACE_COLOR_AOFFSET:
        state->surface_color_offset[0] = data;
        { static int _s = -1; if (_s < 0) _s = getenv("SURFDBG") ? 1 : 0;
          static unsigned _seen[32]; static int _n = 0;
          if (_s) { int f = 0; for (int k = 0; k < _n; k++) if (_seen[k] == data) f = 1;
              if (!f && _n < 32) { _seen[_n++] = data;
                  fprintf(stderr, "[SURF] color offset -> 0x%08X%c", data, 10); } } }
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_BOFFSET:
        state->surface_color_offset[1] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_COFFSET:
        state->surface_color_offset[2] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_DOFFSET:
        state->surface_color_offset[3] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_ZETA_OFFSET:
        state->surface_zeta_offset = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_TARGET:
        { static int _ct=0; if (_ct++ < 24 && getenv("RTT_DUMP"))
            fprintf(stderr, "[RSXCT] color_target=0x%X offA=0x%X offB=0x%X offC=0x%X offD=0x%X%s",
                    data, state->surface_color_offset[0], state->surface_color_offset[1],
                    state->surface_color_offset[2], state->surface_color_offset[3], "\n"); }
        state->color_target = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_PITCH_A:
        state->surface_color_pitch[0] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_B:
        state->surface_color_pitch[1] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_C:
        state->surface_color_pitch[2] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_D:
        state->surface_color_pitch[3] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_Z:
        state->surface_zeta_pitch = data;
        return 0;
    default:
        return -1;
    }
}

/* ---------------------------------------------------------------------------
 * Texture method processing
 *
 * Texture registers are laid out in 16 units, each spanning 0x20 bytes:
 *   Unit 0: 0x1A00..0x1A1C
 *   Unit 1: 0x1A20..0x1A3C
 *   ...
 *   Unit N: 0x1A00 + N*0x20 .. 0x1A1C + N*0x20
 * -----------------------------------------------------------------------*/

static int process_texture_method(rsx_state* state, u32 method, u32 data)
{
    /* Compute texture unit index and register offset within the unit */
    u32 base = method - NV4097_SET_TEXTURE_OFFSET; /* 0x1A00 */
    u32 unit = base / 0x20;
    u32 reg  = base % 0x20;

    if (unit >= RSX_MAX_TEXTURES)
        return -1;

    rsx_texture_state* tex = &state->textures[unit];

    switch (reg) {
    case 0x00: /* TEXTURE_OFFSET */
        tex->offset = data;
        /* RSX_TEXOFF_TRACE=<lo>:<hi> (esadecimali): ogni SET_TEXTURE_OFFSET con
         * valore nell'intervallo, con l'indirizzo FIFO della parola (per
         * risalire con SPU_WRING a chi l'ha scritta, sessione 29). */
        { static int t = -1; static u32 lo = 0, hi = 0;
          if (t < 0) { t = 0; const char* e = getenv("RSX_TEXOFF_TRACE"); if (e && sscanf(e, "%x:%x", &lo, &hi) == 2) t = 1; }
          if (t && data >= lo && data < hi) fprintf(stderr, "[texoff] unit %u off=%08X @fifo %08X\n", unit, data, g_rsx_cur_dea); }
        break;
    case 0x04: /* TEXTURE_FORMAT */
        tex->format = data;
        break;
    case 0x08: /* TEXTURE_ADDRESS (wrap S/T/R) */
        tex->address = data;
        break;
    case 0x0C: /* TEXTURE_CONTROL0 (enable, min/max LOD, max aniso) */
        tex->control0 = data;
        break;
    case 0x10: /* TEXTURE_CONTROL1 (remap) */
        tex->control1 = data;
        break;
    case 0x14: /* TEXTURE_FILTER (bias, min/mag filter) */
        tex->filter = data;
        break;
    case 0x18: /* TEXTURE_IMAGE_RECT (width << 16 | height) */
        tex->image_rect = data;
        break;
    case 0x1C: /* TEXTURE_BORDER_COLOR */
        tex->border_color = data;
        break;
    default:
        return -1;
    }

    tex->dirty = 1;
    state->texture_dirty = 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Vertex attribute method processing
 *
 * FORMAT registers: 0x1740 + attrib*4  (16 attributes)
 * OFFSET registers: 0x1680 + attrib*4  (16 attributes)
 * -----------------------------------------------------------------------*/

static int process_vertex_attrib_method(rsx_state* state, u32 method, u32 data)
{
    if (method >= NV4097_SET_VERTEX_DATA_ARRAY_FORMAT &&
        method < NV4097_SET_VERTEX_DATA_ARRAY_FORMAT + RSX_MAX_VERTEX_ATTRIBS * 4) {
        u32 index = (method - NV4097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
        rsx_vertex_attrib* attr = &state->vertex_attribs[index];

        /*
         * Format register layout:
         *   [3:0]   type   (float, half, ubyte, short, etc.)
         *   [7:4]   size   (number of components: 1-4)
         *   [15:8]  stride (bytes between consecutive elements)
         *   [16]    enable
         */
        attr->type      = data & 0xF;
        attr->size      = (data >> 4) & 0xF;
        attr->stride    = (data >> 8) & 0xFF;
        attr->frequency = (data >> 16) & 0xFFFF;   /* instancing divisor */
        /* SIZE (the component count) is what enables the attribute: NV4097 uses
         * size == 0 to mean "this array is off". Keying off `type` instead marked
         * every unused slot enabled-with-zero-components, because PSGL writes a
         * bare type (e.g. 0x00000002 = float32, size 0, stride 0) into the slots
         * it is NOT using -- so the input layout was built from 16 attributes of
         * which most were degenerate, and the draws rasterized nothing. */
        attr->enabled   = (attr->size != 0);
        attr->format    = data;
        state->vertex_dirty = 1;
        return 0;
    }

    if (method >= NV4097_SET_VERTEX_DATA_ARRAY_OFFSET &&
        method < NV4097_SET_VERTEX_DATA_ARRAY_OFFSET + RSX_MAX_VERTEX_ATTRIBS * 4) {
        u32 index = (method - NV4097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;
        state->vertex_attribs[index].offset = data;
        state->vertex_dirty = 1;
        return 0;
    }

    return -1;
}

/* ---------------------------------------------------------------------------
 * Main method dispatch
 * -----------------------------------------------------------------------*/

/* ---- modalita' immediata --------------------------------------------------
 * Il titolo che disegna cosi' non ha un vertex buffer: ogni vertice e' una
 * raffica di scritture ai registri di attributo fra BEGIN e END, e il vertice
 * e' completo quando viene scritto l'attributo 0 (la posizione). Qui si
 * accumulano, e a END si consegnano al backend in un colpo solo.
 *
 * Lo store e' un file-static e non un campo di rsx_state di proposito:
 * rsx_state viene copiato e passato per valore in piu' punti, e mezzo megabyte
 * di vertici dentro non ci deve stare. -------------------------------------*/
#define RSX_INLINE_MAX_VERTS 4096
static float s_inline_verts[RSX_INLINE_MAX_VERTS * RSX_MAX_VERTEX_ATTRIBS * 4];
static u32   s_inline_count;
static u32   s_inline_dropped;

static void rsx_inline_latch(rsx_state* state)
{
    if (s_inline_count >= RSX_INLINE_MAX_VERTS) { s_inline_dropped++; return; }
    memcpy(&s_inline_verts[s_inline_count * RSX_MAX_VERTEX_ATTRIBS * 4],
           state->vertex_data4f, sizeof(state->vertex_data4f));
    s_inline_count++;
}

/* Ultimo valore scritto per ogni slot di report (tabella aperta, chiave = EA):
 * lo legge spu_dma.h quando una GET SPU trova lo slot ancora a -1 (v. la). */
#define RSX_REPORT_SHADOW_N 16384
static u32 s_rsh_ea[RSX_REPORT_SHADOW_N], s_rsh_val[RSX_REPORT_SHADOW_N];
static int s_rsh_on = -1;
static void rsx_report_shadow_set(u32 ea, u32 val)
{
    if (s_rsh_on < 0) { const char* e = getenv("RSX_REPORT_LAG"); s_rsh_on = (e && *e == '0') ? 0 : 1; }
    if (!s_rsh_on) return;
    u32 h = (ea >> 4) & (RSX_REPORT_SHADOW_N - 1);
    for (u32 i = 0; i < RSX_REPORT_SHADOW_N; i++, h = (h + 1) & (RSX_REPORT_SHADOW_N - 1))
        if (s_rsh_ea[h] == ea || s_rsh_ea[h] == 0) { s_rsh_ea[h] = ea; s_rsh_val[h] = val; return; }
}
/* Coda dei label ritardati (GCM_LABEL_DELAY_US): FIFO di (ea, valore, quando),
 * svuotata da rsx_label_delay_flush() -- chiamata dal ticker e all'inizio
 * di ogni passata del walker -- per le voci scadute, in ordine. */
#include <time.h>
static struct { u32 ea, val; unsigned long long due_ns; } s_ldq[4096];
static unsigned s_ldq_head, s_ldq_tail;
static unsigned long long rsx_ns_now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}
void rsx_label_delay_flush(void)
{
    extern void vm_write32(uint32_t a, uint32_t v);
    unsigned long long now = rsx_ns_now();
    while (s_ldq_head != s_ldq_tail) {
        unsigned i = s_ldq_head & 4095;
        if (s_ldq[i].due_ns > now) break;
        vm_write32(s_ldq[i].ea, s_ldq[i].val);
        s_ldq_head++;
    }
}
void rsx_label_delay_push(u32 ea, u32 val, unsigned delay_us)
{
    extern void vm_write32(uint32_t a, uint32_t v);
    if (s_ldq_tail - s_ldq_head >= 4096) { rsx_label_delay_flush(); if (s_ldq_tail - s_ldq_head >= 4096) { vm_write32(ea, val); return; } }
    unsigned i = s_ldq_tail++ & 4095;
    s_ldq[i].ea = ea; s_ldq[i].val = val; s_ldq[i].due_ns = rsx_ns_now() + (unsigned long long)delay_us * 1000ull;
}

/* Per il backend: scrive il valore vero di un report ZPASS (ea+8) e aggiorna
 * l'ombra, quando la GPU ha finito il fotogramma. */
void rsx_report_set_value(u32 ea, u32 val)
{
    extern void vm_write32(uint32_t a, uint32_t v);
    vm_write32(ea + 8, val);
    rsx_report_shadow_set(ea, val);
}
int rsx_report_shadow_get(u32 ea, u32* val)
{
    if (s_rsh_on <= 0) return 0;
    u32 h = (ea >> 4) & (RSX_REPORT_SHADOW_N - 1);
    for (u32 i = 0; i < RSX_REPORT_SHADOW_N; i++, h = (h + 1) & (RSX_REPORT_SHADOW_N - 1)) {
        if (s_rsh_ea[h] == ea) { *val = s_rsh_val[h]; return 1; }
        if (s_rsh_ea[h] == 0) return 0; }
    return 0;
}
#include <time.h>
unsigned long long rsx_us_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)ts.tv_nsec / 1000ull; }
static u32 s_report_ctx = 0xFEED0001u;   /* NV4097_SET_CONTEXT_DMA_REPORT */
/* Coalescenza dei DRAW_ARRAYS / DRAW_INDEX_ARRAY (sessione 30). Ogni parola
 * porta al massimo 256 vertici ((count-1) in 8 bit) e un draw grande arriva
 * come una sequenza di parole CONTIGUE dentro lo stesso BEGIN/END. Mandarle al
 * backend una per una faceva ripartire l'assemblaggio delle primitive a ogni
 * parola: 256 = 85*3+1, quindi dalla seconda parola in poi una lista di
 * triangoli e' sfalsata di un vertice e i triangoli sono sbagliati (il cielo
 * del livello a triangoli con i buchi: 23 parole da 256). Come RPCS3
 * (draw_clause::append): le parole contigue diventano UN draw, emesso a END
 * (o subito, fuori da BEGIN/END). RSX_DRAW_COALESCE=0 torna a una per una. */
static struct { int kind; u32 first, count; } s_dc;   /* kind: 0 niente, 1 arrays, 2 indexed */
static void draw_clause_flush(rsx_state* state)
{
    if (!s_dc.kind) return;
    if (s_backend) {
        if (s_dc.kind == 1 && s_backend->draw_arrays)
            s_backend->draw_arrays(s_backend->userdata, state->primitive_type, s_dc.first, s_dc.count);
        else if (s_dc.kind == 2 && s_backend->draw_indexed)
            s_backend->draw_indexed(s_backend->userdata, state->primitive_type, s_dc.first, s_dc.count);
    }
    s_dc.kind = 0;
}
static void draw_clause_append(rsx_state* state, int kind, u32 first, u32 count)
{
    static int s_on = -1; if (s_on < 0) { const char* e = getenv("RSX_DRAW_COALESCE"); s_on = (e && *e == '0') ? 0 : 1; }
    if (s_on && s_dc.kind == kind && s_dc.first + s_dc.count == first) { s_dc.count += count; return; }
    draw_clause_flush(state);
    s_dc.kind = kind; s_dc.first = first; s_dc.count = count;
    if (!s_on || !state->in_begin_end) draw_clause_flush(state);
}
extern u32 cellGcmResolveLocated(int local, u32 offset);
extern unsigned long long ps3_ms_now(void);
/* ---------------------------------------------------------------------------
 * Guardia dell'anello dei job SPU (s.32). Il job di geometria di Uncharted
 * (ovl 215) alloca i blocchi (vertici + pacchetto) da un anello per lane
 * (0x199980 byte) e prima di scrivere un blocco (r12, r5] legge il label
 * 0x480+16*lane (posizione consumata dalla RSX, = fine dell'ultimo blocco) e
 * aspetta SOLO se r12 < label <= r5. Con la RSX vera, piu' veloce degli SPU,
 * il label sta sempre a ridosso del produttore e quel controllo basta. Il
 * nostro walker e' piu' lento delle lane: il job fa un giro intero
 * dell'anello in ~700 righe di log mentre il label si muove di poche
 * posizioni, il label cade quasi sempre su un confine di blocco (escluso
 * dall'intervallo) e il job riscrive TUTTI i blocchi non ancora consumati
 * (prepass con vertici spazzatura, 187-435 draw su 3000). Qui si tiene la
 * posizione virtuale (giri x anello + offset) di produttore e consumatore:
 * se il blocco nuovo supererebbe il consumatore di un giro meno un margine,
 * al job si fa leggere un valore DENTRO il suo intervallo (r5, o r23 nel
 * wrap) e lui aspetta da solo; appena il walker rilascia abbastanza, torna
 * a leggere il label vero. GCM_RING_GUARD=0 spegne, _MARGIN i byte. */
#define RING_GUARD_LANES 5
#define RING_GUARD_SIZE  0x199980u
#define RING_GUARD_HIST  1024
static struct {
    u32 last_r12, last_rel, real_label; int have_p, have_c;
    u64 lap_p, lap_c, prod_v, cons_v;
    struct { u32 end; u64 lap; } hist[RING_GUARD_HIST]; u32 hist_n;
    unsigned long waits; unsigned long long wait_t0;
} s_rg[RING_GUARD_LANES];
static int ring_guard_on(void) { static int on = -1; if (on < 0) { const char* e = getenv("GCM_RING_GUARD"); on = (e && *e == '0') ? 0 : 1; } return on; }
/* il walker ha rilasciato `val` per la lane (fine di un blocco consumato) */
void rsx_ring_guard_release(unsigned lane, u32 val)
{
    if (lane >= RING_GUARD_LANES) return;
    if (!ring_guard_on()) return;
    /* il giro del consumatore = quello in cui il produttore ha allocato il
     * blocco che finisce qui (l'ultimo, se piu' d'uno) */
    u64 lap = s_rg[lane].lap_c; int found = 0;
    for (u32 k = 0; k < s_rg[lane].hist_n && k < RING_GUARD_HIST; k++) {
        u32 i = (s_rg[lane].hist_n - 1 - k) % RING_GUARD_HIST;
        if (s_rg[lane].hist[i].end == val) { lap = s_rg[lane].hist[i].lap; found = 1; break; } }
    if (!found && s_rg[lane].have_c && val + RING_GUARD_SIZE / 2 < s_rg[lane].last_rel) lap = s_rg[lane].lap_c + 1;
    s_rg[lane].lap_c = lap; s_rg[lane].cons_v = lap * RING_GUARD_SIZE + val;
    s_rg[lane].last_rel = val; s_rg[lane].real_label = val; s_rg[lane].have_c = 1;
}
/* il job sta per controllare il label per il blocco (r12, r5]: 1 = deve
 * aspettare (scrivi `*fake` nel label), 0 = scrivi il label vero */
int rsx_ring_guard_check(unsigned lane, u32 r12, u32 r5, u32 r23, u32 r26, u32* fake, u32* real)
{
    if (lane >= RING_GUARD_LANES || !ring_guard_on()) return -1;
    static long s_margin = -1; if (s_margin < 0) { const char* e = getenv("GCM_RING_GUARD_MARGIN"); s_margin = e ? strtol(e, 0, 0) : 0x40000; }
    /* un blocco che supera la fine dell'anello (r5 > r26) riparte dall'inizio e
     * finisce a r23: la RELEASE del suo pacchetto vale r23, nel giro dopo */
    const int wrap = (r5 > r26);
    const u32 end = wrap ? r23 : r5; const u64 lap_end = s_rg[lane].lap_p + (wrap ? 1u : 0u);
    if (!s_rg[lane].have_p || r12 != s_rg[lane].last_r12) {   /* blocco nuovo */
        if (s_rg[lane].have_p && r12 + RING_GUARD_SIZE / 2 < s_rg[lane].last_r12) s_rg[lane].lap_p++;   /* wrap */
        s_rg[lane].last_r12 = r12; s_rg[lane].have_p = 1;
        u32 i = s_rg[lane].hist_n % RING_GUARD_HIST; s_rg[lane].hist[i].end = end; s_rg[lane].hist[i].lap = s_rg[lane].lap_p + (wrap ? 1u : 0u); s_rg[lane].hist_n++;
        s_rg[lane].wait_t0 = 0;
    }
    s_rg[lane].prod_v = lap_end * RING_GUARD_SIZE + end;
    *real = s_rg[lane].real_label;
    if (!s_rg[lane].have_c) return 0;
    if (s_rg[lane].prod_v > s_rg[lane].cons_v + RING_GUARD_SIZE - (u64)s_margin) {
        /* valvola: un'attesa nostra che dura piu' di GCM_RING_GUARD_MAX_MS (default
         * 2000) e' un blocco circolare (il walker aspetta il pacchetto di questa
         * lane): si lascia passare e si risincronizza il consumatore */
        extern unsigned long long ps3_ms_now(void); unsigned long long now = ps3_ms_now();
        static long s_max = -1; if (s_max < 0) { const char* e = getenv("GCM_RING_GUARD_MAX_MS"); s_max = e ? atol(e) : 2000; }
        if (!s_rg[lane].wait_t0) s_rg[lane].wait_t0 = now;
        if (s_max > 0 && now - s_rg[lane].wait_t0 > (unsigned long long)s_max) {
            static int _v = 0; if (_v++ < 16) fprintf(stderr, "[ring-guard] lane %u: valvola dopo %llu ms, blocco (%08X, %08X] prod_v=%llu cons_v=%llu label=%08X\n",
                                                     lane, now - s_rg[lane].wait_t0, r12, r5, (unsigned long long)s_rg[lane].prod_v, (unsigned long long)s_rg[lane].cons_v, s_rg[lane].real_label);
            s_rg[lane].cons_v = s_rg[lane].prod_v - RING_GUARD_SIZE + (u64)s_margin + 0x10000u; s_rg[lane].lap_c = s_rg[lane].cons_v / RING_GUARD_SIZE; s_rg[lane].wait_t0 = 0;
            return 0; }
        *fake = wrap ? r23 : r5; s_rg[lane].waits++;
        { static int s_dbg = -1; if (s_dbg < 0) s_dbg = getenv("GCM_RING_GUARD_DBG") ? 1 : 0; static unsigned long _n = 0;
          if (s_dbg && ((_n++ % 2000) == 0)) fprintf(stderr, "[ring-guard] lane %u aspetta: blocco (%08X, %08X] prod_v=%llu cons_v=%llu (label vero %08X) attese=%lu\n",
                                                     lane, r12, r5, (unsigned long long)s_rg[lane].prod_v, (unsigned long long)s_rg[lane].cons_v, s_rg[lane].real_label, s_rg[lane].waits); }
        return 1; }
    return 0;
}

int rsx_process_method(rsx_state* state, u32 method, u32 data)
{
    /* YDKJ_RSXTRACE=<N>: trace the first N methods (bare "1" keeps the old 250).
     * The fixed 250 was spent entirely on boot-time setup, so the methods around
     * the first real draw -- exactly the ones worth seeing -- were never traced. */
    { static int _rt=-1; if(_rt<0){ const char* e=getenv("YDKJ_RSXTRACE");
        _rt = e ? (atoi(e) > 1 ? atoi(e) : 250) : 0; }
      if(_rt){ static int _m=0; if(_m++<_rt) fprintf(stderr,"[rsxm] method=0x%04X data=0x%08X\n", method, data); } }
    /* RSX_MTRACE=<lo>:<hi>: i metodi mentre il contatore dei draw del backend
     * (nel fotogramma corrente) e' in [lo,hi]; armato da PS3_TTY_BT, tetto
     * 4000 righe. Per vedere COSA cambia fra due draw "uguali" (superficie,
     * maschere, offset) senza leggere tutto l'anello. */
#ifdef __APPLE__   /* il contatore vive nel backend Metal */
    { static long s_lo = -2, s_hi = -1; static int s_n = 0; extern unsigned rsx_backend_draw_count(void); extern int g_hle_trace_armed;
      if (s_lo == -2) { const char* e = getenv("RSX_MTRACE"); s_lo = -1; if (e) { s_lo = strtol(e, 0, 10); const char* c = strchr(e, ':'); s_hi = c ? strtol(c + 1, 0, 10) : s_lo; } }
      if (s_lo >= 0 && g_hle_trace_armed && s_n < 4000) { long dc = (long)rsx_backend_draw_count();
          if (dc >= s_lo && dc <= s_hi) { s_n++; fprintf(stderr, "[mtrace] d%ld m=0x%04X %08X\n", dc, method, data); } } }
#endif
    /* Back-end write label / semaphore (cellGcmSetWriteBackEndLabel): the RSX
     * writes a value to a report/label the CPU polls for CPU<->RSX sync (double
     * buffering). Real hardware DOES this; without it the game's frame-fence
     * loop (func_0006E6E0 polling label 0x41 @ 0x03000410) spins forever and
     * never reaches render. NV4097_SET_SEMAPHORE_OFFSET(0x1D6C)=offset (index*0x10
     * into the GCM label window @0x03000000); NV4097_BACK_END_WRITE_SEMAPHORE_
     * RELEASE(0x1D70)=value. Also NV406E semaphore release (0x0010 offset/0x0014
     * value) for the sub-channel path. */
    /* NV4097_SET_FREQUENCY_DIVIDER_OPERATION: per-attribute modulo/divide mask
     * for the frequency divisor (instancing). Captured for read_vp_vertex. */
    if (method == 0x00001FC0) { state->frequency_divider_op = data; return 0; }

    /* GCM_SEMDBG=1: i metodi dei semafori NV4097 (0x0060 context DMA, 0x1D6C
     * offset, 0x1D70 back-end release, 0x1D74 texture-read release). */
    { static int s_sd = -1; if (s_sd < 0) s_sd = getenv("GCM_SEMDBG") ? 1 : 0;
      static int s_sn = 0;
      if (s_sd && (method == 0x0060 || method == 0x01A4 || (s_sn < 60 && (method == 0x1D6C || method == 0x1D70 || method == 0x1D74)))) { s_sn++;
          fprintf(stderr, "[semdbg] method=0x%04X data=0x%08X\n", method, data); } }
    /* GCM_DATAWATCH=lo:hi (hex): ogni metodo il cui argomento (24 bit bassi)
     * cade in [lo,hi): chi scrive in una zona di memoria (report, semafori). */
    { static long s_lo = -1, s_hi = 0; static int s_n = 0;
      if (s_lo < 0) { const char* e = getenv("GCM_DATAWATCH"); s_lo = 0; s_hi = 0;
          if (e) { s_lo = strtol(e, 0, 16); const char* c = strchr(e, ':'); s_hi = c ? strtol(c + 1, 0, 16) : s_lo + 0x100; } }
      /* GCM_DATAWATCH_M=<hex>: solo quel metodo */
      static long s_m = -2; if (s_m == -2) { const char* e = getenv("GCM_DATAWATCH_M"); s_m = e ? strtol(e, 0, 16) : -1; }
      if (s_hi && (s_m < 0 || (long)method == s_m) && (long)(data & 0xFFFFFFu) >= s_lo && (long)(data & 0xFFFFFFu) < s_hi && s_n++ < 4000)
          fprintf(stderr, "[datawatch] method=0x%04X data=0x%08X\n", method, data); }
    /* Report (NV4097_GET_REPORT 0x1800): la RSX scrive 16 byte
     * {timestamp(8), valore(4), 0} all'offset dato, nel contesto scelto da
     * NV4097_SET_CONTEXT_DMA_REPORT (0x01A8): 0xFEED0001 = memoria principale
     * (offset IO -> EA), 0xFEED0000 = locale, 0x66626660 = area report locale.
     * Uncharted (ICE) mette 8192 report in memoria principale a IO 0x14400: il
     * PPU emette `GET_REPORT slot; CLEAR_REPORT_VALUE; CALL <buffer del job>`
     * per ogni oggetto (occlusion query con ZPASS_PIXEL_CNT), il job SPU 215
     * il fotogramma dopo aspetta che il +8 dello slot non sia piu' -1 (che il
     * PPU ci scrive in SwapBuffers) e con valore 0 salta il disegno. Non c'e'
     * ancora un'occlusion query in Metal: il conteggio ZPASS e' un valore
     * fisso non nullo (RSX_ZPASS_COUNT, default 0x1000 = "visibile"). */
    if (method == 0x01A8) { s_report_ctx = data; return 0; }
    if (method == 0x17C8) { if (s_backend && s_backend->report_clear) s_backend->report_clear(s_backend->userdata); return 0; }   /* CLEAR_REPORT_VALUE */
    if (method == 0x1800) {
        extern void vm_write32(uint32_t a, uint32_t v);
        u32 type = data >> 24, off = data & 0x00FFFFFFu, ea = 0;
        if (s_report_ctx == 0xFEED0001u || s_report_ctx == 0xBAD68000u) ea = cellGcmResolveLocated(0, off);
        else if (s_report_ctx == 0xFEED0000u) ea = cellGcmResolveLocated(1, off);
        else ea = cellGcmResolveLocated(1, 0x1400000u + off);      /* 0x66626660: area report locale */
        static long s_zp = -1; if (s_zp < 0) { const char* e = getenv("RSX_ZPASS_COUNT"); s_zp = e ? strtol(e, 0, 16) : 0x1000; }
        u32 val = (type == 1) ? (u32)s_zp : 0;
        /* Occlusion vere (sessione 28): il valore vero arriva a GPU finita,
         * ma il job 215 legge il report SUBITO (timestamp nuovo = pronto) e
         * con 0 in attesa il livello si fermava ("Rendering is stuck on
         * reference value"). Sul ferro la RSX e' un fotogramma indietro e
         * il job legge il risultato del fotogramma prima: qui si scrive
         * subito l'ULTIMO valore noto di quello slot (l'ombra), che il
         * completed handler poi aggiorna. Riuso temporale, come sul ferro. */
        { static int s_oq2 = -1; if (s_oq2 < 0) { const char* e = getenv("RSX_OCCLUSION"); s_oq2 = (e && *e == '0') ? 0 : 1; }
          u32 prev; if (s_oq2 && type == 1 && ea && rsx_report_shadow_get(ea, &prev)) val = prev; }
        if (ea) { unsigned long long ts = ps3_ms_now() * 1000ull;
            vm_write32(ea, (u32)(ts >> 32)); vm_write32(ea + 4, (u32)ts); vm_write32(ea + 8, val); vm_write32(ea + 12, 0);
            rsx_report_shadow_set(ea, val);
            /* il conteggio vero arriva dal backend a fotogramma finito (RSX_OCCLUSION=0 lo spegne) */
            { static int s_oq = -1; if (s_oq < 0) { const char* e = getenv("RSX_OCCLUSION"); s_oq = (e && *e == '0') ? 0 : 1; }
              if (s_oq && type == 1 && s_backend && s_backend->report_zpass) s_backend->report_zpass(s_backend->userdata, ea); } }
        { static int _n = 0; if (_n++ < 4 || getenv("RSX_REPORT_DBG")) fprintf(stderr, "[RSX] report tipo %u offset 0x%06X -> ea 0x%08X = %u (ctx 0x%08X)\n", type, off, ea, val, s_report_ctx); }
        return 0;
    }
    { static u32 s_sem_off = 0;
      if (method == 0x1D6C) { s_sem_off = data; return 0; }
      /* 0x1D74 NV4097_TEXTURE_READ_SEMAPHORE_RELEASE: stessa scrittura del
       * back-end release ma SENZA lo scambio di byte (RPCS3 nv4097.cpp:
       * texture_read_semaphore_release scrive `arg` com'e', solo
       * back_end_write_semaphore_release scambia 0<->2). Uncharted usa SOLO
       * questa: i buffer di comandi dei job SPU finiscono con
       * SET_SEMAPHORE_OFFSET 0x480+16*lane + questa release con la posizione
       * raggiunta nell'anello dei comandi (es. 0x20800000), e il job di
       * geometria (ovl 215, `func_4B48`) aspetta che quella posizione esca
       * dall'intervallo che vuole riscrivere. Con lo scambio il label valeva
       * 0x20000080, mai nello spazio confrontato, e le 5 lane restavano a
       * interrogarlo per sempre (sessione 28, `SPU_LABEL_POLL=1`). */
      if (method == 0x1D70 || method == 0x1D74) {
        extern void vm_write32(uint32_t a, uint32_t v);
        /* The RSX back-end semaphore write swaps bytes 0<->2 of the value (hw
         * quirk); libgcm's cellGcmSetWriteBackEndLabel PRE-swaps to compensate
         * (SDK gcm_implementation_sub.h: "// swap byte 0 and 2"). Writing the
         * FIFO value verbatim left the label byte-swapped, so gcmutil's
         * cellGcmUtilFinish poll (label 255 == sLabelVal, starts at 1) compared
         * 0x00010000 != 1 and spun forever (gcm/cube sample hang after init).
         * Apply the same swap the hardware does so the pre-swap cancels out. */
        u32 val = (method == 0x1D70) ? ((data & 0xff00ff00u) | ((data >> 16) & 0xffu) | ((data & 0xffu) << 16)) : data;
        /* GCM_LABEL_DELAY_US=<n> (sessione 28): il label della texture-read
         * release si scrive n microsecondi DOPO che il walker l'ha
         * incontrato. Sul ferro la release scatta quando le letture delle
         * texture dei draw precedenti sono finite, cioe' millisecondi dopo
         * che il command processor ha letto il comando; scriverlo subito
         * fa vedere ai job SPU una posizione della RSX "nel futuro": il job
         * di geometria di Uncharted, che scrive il blocco k+1 sopra la coda
         * del blocco k, trovava il label dentro la propria regione e
         * aspettava per sempre (livello fermo con GCM_PARK_POLL_US=200). */
        { static long s_ld = -1; if (s_ld < 0) { const char* e = getenv("GCM_LABEL_DELAY_US"); s_ld = e ? atol(e) : 0; }
          if (s_ld > 0 && method == 0x1D74) { rsx_label_delay_push(VM_HLE_INJECT_BASE + (s_sem_off & 0x00FFFFFFu), val, (unsigned)s_ld); }
          else vm_write32(VM_HLE_INJECT_BASE + (s_sem_off & 0x00FFFFFFu), val);
          if (method == 0x1D74 && (s_sem_off & 0xFFFFFFu) >= 0x480u && (s_sem_off & 0xFFFFFFu) < 0x4D0u)
              rsx_ring_guard_release((((s_sem_off & 0xFFFFFFu) - 0x480u) >> 4), val); }
        { static int _l=0; if(_l++<8) fprintf(stderr,"[RSX] label write @0x%08X = 0x%08X (sync fence, raw 0x%08X)\n", VM_HLE_INJECT_BASE+(s_sem_off&0xFFFFFF), val, data); }
        { static int s_rt = -1; if (s_rt < 0) s_rt = getenv("GCM_RING_TRACE") ? 1 : 0;   /* v. spu_dma.h [ring] */
          if (s_rt && method == 0x1D74 && (s_sem_off & 0xFFFFFFu) >= 0x480u && (s_sem_off & 0xFFFFFFu) < 0x500u) {
              static long s_from = -2; if (s_from == -2) { const char* e = getenv("SPU_DMA_EAWIN_FROM"); s_from = e ? atol(e) : 0; }
              extern unsigned rsx_backend_frame_no(void);
              if ((long)rsx_backend_frame_no() >= s_from) { static int _n = 0; if (_n++ < 60000)
                  fprintf(stderr, "[ring] %llu lane %u RELEASE %08X\n", rsx_us_now() % 100000000ull, (((s_sem_off & 0xFFFFFFu) - 0x480u) >> 4) & 7, val); } } }
        /* RSX_LABEL_DBG=<hex offset>: tutte le scritture di quel label (il job
         * 215 aspetta il 0x480 = "fin dove la RSX ha consumato l'anello SPU") */
        { static long s_ld = -2; if (s_ld == -2) { const char* e = getenv("RSX_LABEL_DBG"); s_ld = e ? strtol(e, 0, 16) : -1; }
          if (s_ld >= 0 && (long)(s_sem_off & 0xFFFFFF) == s_ld) fprintf(stderr, "[label] 0x%03lX = 0x%08X (metodo 0x%04X)\n", s_ld, val, method); }
        return 0;
      } }
    if ((method >= 0x200 && method <= 0x23C) ||
        (method >= 0x280 && method <= 0x28C))
        return process_surface_method(state, method, data);

    /* Texture methods: 0x1A00..0x1A00 + 16*0x20 - 1 */
    if (method >= 0x1A00 && method < 0x1A00 + RSX_MAX_TEXTURES * 0x20)
        return process_texture_method(state, method, data);

    /* Vertex attribute FORMAT: 0x1740..0x177C */
    if (method >= 0x1740 && method < 0x1740 + RSX_MAX_VERTEX_ATTRIBS * 4)
        return process_vertex_attrib_method(state, method, data);

    /* Vertex attribute OFFSET: 0x1680..0x16BC */
    if (method >= 0x1680 && method < 0x1680 + RSX_MAX_VERTEX_ATTRIBS * 4)
        return process_vertex_attrib_method(state, method, data);

    /* Viewport */
    /* Viewport transform: window = ndc*scale + offset. The z lane is the
     * GL->[0,1] depth remap (offset.z=0.5/scale.z=0.5); without honoring it,
     * GL-convention projections (SDK gcm samples) get their near-camera
     * geometry clipped by D3D's 0<=z<=w rule (gcm/cube: missing polygons). */
    if (method >= 0x0A20 && method < 0x0A30) {
        u32 f = data; float v; memcpy(&v, &f, 4);
        state->viewport_offset[(method - 0x0A20) >> 2] = v;
        return 0;
    }
    if (method >= 0x0A30 && method < 0x0A40) {
        u32 f = data; float v; memcpy(&v, &f, 4);
        state->viewport_scale[(method - 0x0A30) >> 2] = v;
        return 0;
    }

    if (method == NV4097_SET_VIEWPORT_HORIZONTAL) {
        state->viewport_x = data & 0xFFFF;
        state->viewport_w = (data >> 16) & 0xFFFF;
        state->viewport_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_VIEWPORT_VERTICAL) {
        state->viewport_y = data & 0xFFFF;
        state->viewport_h = (data >> 16) & 0xFFFF;
        state->viewport_dirty = 1;
        return 0;
    }

    /* Color mask */
    if (method == NV4097_SET_COLOR_MASK) {
        state->color_mask = data;
        state->color_mask_dirty = 1;
        return 0;
    }

    /* Alpha test */
    if (method == NV4097_SET_ALPHA_TEST_ENABLE) {
        state->alpha_test_enable = data ? 1 : 0;
        state->alpha_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_ALPHA_FUNC) {
        state->alpha_func = data;
        state->alpha_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_ALPHA_REF) {
        state->alpha_ref = data;
        state->alpha_dirty = 1;
        return 0;
    }

    /* Clear */
    if (method == NV4097_SET_COLOR_CLEAR_VALUE) {
        state->color_clear_value = data;
        return 0;
    }
    if (method == NV4097_SET_ZSTENCIL_CLEAR_VALUE) {
        state->zstencil_clear_value = data;
        return 0;
    }
    if (method == NV4097_CLEAR_SURFACE) {
        { static int _c=0; if (_c++ < 12) fprintf(stderr, "[RSX] CLEAR_SURFACE mask=0x%X color=0x%08X\n", data, state->color_clear_value); }
        if (s_backend && s_backend->clear) {
            float depth = (float)(state->zstencil_clear_value >> 8) / (float)0xFFFFFF;
            u8 stencil = state->zstencil_clear_value & 0xFF;
            s_backend->clear(s_backend->userdata, data,
                           state->color_clear_value, depth, stencil);
        }
        return 0;
    }

    /* Blend */
    if (method == NV4097_SET_BLEND_ENABLE) {
        state->blend_enable = data ? 1 : 0;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_FUNC_SFACTOR) {
        state->blend_sfactor = data;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_FUNC_DFACTOR) {
        state->blend_dfactor = data;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_EQUATION) {
        state->blend_equation = data;
        state->blend_dirty = 1;
        return 0;
    }

    /* Depth */
    if (method == NV4097_SET_DEPTH_TEST_ENABLE) {
        state->depth_test_enable = data ? 1 : 0;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_DEPTH_FUNC) {
        state->depth_func = data;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_DEPTH_MASK) {
        state->depth_mask = data ? 1 : 0;
        state->depth_dirty = 1;
        return 0;
    }

    /* Stencil */
    if (method == NV4097_SET_STENCIL_TEST_ENABLE) {
        state->stencil_test_enable = data ? 1 : 0;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC) {
        state->stencil_func = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC_REF) {
        state->stencil_ref = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC_MASK) {
        state->stencil_mask = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_FAIL) {
        state->stencil_op_fail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_ZFAIL) {
        state->stencil_op_zfail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_ZPASS) {
        state->stencil_op_zpass = data;
        state->stencil_dirty = 1;
        return 0;
    }

    /* Culling */
    if (method == NV4097_SET_CULL_FACE_ENABLE) {
        state->cull_face_enable = data ? 1 : 0;
        return 0;
    }
    if (method == NV4097_SET_CULL_FACE) {
        state->cull_face = data;
        return 0;
    }
    if (method == NV4097_SET_FRONT_FACE) {
        state->front_face = data;
        return 0;
    }

    /* Shader programs */
    if (method == NV4097_SET_SHADER_PROGRAM) {
        /*
         * Fragment program address register:
         *   [1:0]  location (0 = local, 1 = main)
         *   [31:2] offset (4-byte aligned)
         */
        state->shader_program = data;
        state->fragment_program_addr = data & ~0x3u;
        state->shader_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_TRANSFORM_PROGRAM_LOAD) {
        /* Vertex program load slot — instruction index (each = 16 bytes).
         * Following NV4097_SET_TRANSFORM_PROGRAM words fill from here. */
        state->transform_program_load = data;
        state->vp_ucode_write = data * 16;
        state->shader_dirty = 1;
        return 0;
    }

    if (method == NV4097_SET_TRANSFORM_PROGRAM_START) {
        if (state->transform_program_start != data) state->vp_dirty = 1;
        state->transform_program_start = data;
        state->shader_dirty = 1;
        return 0;
    }

    /* NV4097_SET_TRANSFORM_PROGRAM[0..31] — a run of 32-bit vertex-program
     * microcode words appended at the current write cursor. Capture them so the
     * backend can decompile the real VP (4 words = one NV40 instruction). */
    if (method >= NV4097_SET_TRANSFORM_PROGRAM &&
        method <  NV4097_SET_TRANSFORM_PROGRAM + 32 * 4) {
        u32 w = state->vp_ucode_write;
        if (w + 4 <= sizeof(state->vp_ucode)) {
            /* data is host-endian already; store as little-endian bytes. */
            state->vp_ucode[w+0] = (u8)(data);
            state->vp_ucode[w+1] = (u8)(data >> 8);
            state->vp_ucode[w+2] = (u8)(data >> 16);
            state->vp_ucode[w+3] = (u8)(data >> 24);
            state->vp_ucode_write = w + 4;
            if (w + 4 > state->vp_ucode_bytes) state->vp_ucode_bytes = w + 4;
            state->vp_dirty = 1;
            state->vp_ucode_gen++;
        }
        return 0;
    }
    if (method == NV4097_SET_VERTEX_ATTRIB_OUTPUT_MASK) {
        state->vertex_attrib_output_mask = data;
        return 0;
    }
    /* NV4097_SET_VERTEX_DATA4F_M (0x1C00): the constant/"current" value for each
     * vertex attribute, 16 attributes x 4 floats. The hardware feeds this to
     * every vertex when the attribute's ARRAY is disabled -- it is not zero.
     *
     * Rubber Ducky leaves attribute 3 (diffuse colour) disabled and sets it here
     * instead; its duck shader computes (lighting * col0) * texture + spec, so a
     * zero col0 multiplies the texture away and the ducks rendered as a dim grey
     * specular term only -- present in the framebuffer, invisible on screen.
     * SET_VERTEX_DATA2F/4UB/2S/4S are the other encodings of the same register
     * file; add them if a title needs them. */
    if (method >= 0x1C00u && method < 0x1C00u + RSX_MAX_VERTEX_ATTRIBS * 16u) {
        u32 idx  = (method - 0x1C00u) >> 4;         /* attribute */
        u32 lane = ((method - 0x1C00u) >> 2) & 3;   /* x/y/z/w   */
        float f; memcpy(&f, &data, 4);
        state->vertex_data4f[idx][lane] = f;
        { static int _d = -1; if (_d < 0) _d = getenv("VDATA_DBG") ? 1 : 0;
          static int _n = 0;
          if (_d && _n < 400) { _n++;
            fprintf(stderr, "[VDATA4F] attr=%u lane=%u = %.4f%c", idx, lane, f, 10); } }
        /* Fra BEGIN e END questi non sono costanti: sono i vertici stessi. */
        if (state->in_begin_end && idx == 0 && lane == 3) rsx_inline_latch(state);
        return 0;
    }

    /* NV4097_SET_VERTEX_DATA2F_M (0x1880): stessa banca di registri, due float
     * per attributo (z=0, w=1 come sull'hardware). Uncharted disegna l'HUD e i
     * quad a pieno schermo cosi': misurato, fra BEGIN e END arrivano SOLO
     * 0x1880-0x188C (attributi 0 e 1, 2F) e 0x1C20-0x1C2C (attributo 2, 4F),
     * mai un DRAW_ARRAYS. Senza questo ramo il backend non riceveva un solo
     * draw e lo schermo restava nero. */
    if (method >= 0x1880u && method < 0x1880u + RSX_MAX_VERTEX_ATTRIBS * 8u) {
        u32 idx  = (method - 0x1880u) >> 3;         /* attribute */
        u32 lane = ((method - 0x1880u) >> 2) & 1;   /* x/y       */
        float f; memcpy(&f, &data, 4);
        state->vertex_data4f[idx][lane] = f;
        if (lane == 1) { state->vertex_data4f[idx][2] = 0.0f;
                         state->vertex_data4f[idx][3] = 1.0f; }
        /* L'attributo 0 e' la posizione e il titolo lo scrive PER ULTIMO
         * (misurato: attr2, attr1, poi attr0) -- e' la sua ultima lane a
         * chiudere il vertice, come sull'hardware NV. */
        if (state->in_begin_end && idx == 0 && lane == 1) rsx_inline_latch(state);
        return 0;
    }

    if (method == NV4097_SET_TRANSFORM_CONSTANT_LOAD) {
        { static int _n=0; if (getenv("LOAD_DBG") && _n++ < 200)
            fprintf(stderr, "[LOAD] transform_constant_load = %u\n", data); }
        state->transform_constant_load = data;
        return 0;
    }

    /* NV4097_SET_TRANSFORM_CONSTANT[0..63] — up to 64 dwords (16 vec4s) per
     * command. Each register slot writes to a lane of one vertex constant
     * vec4: vec_index = LOAD + (reg_offset/4), lane = reg_offset%4.
     * The data arrives as a host-endian u32; reinterpret the bits as float
     * because the game's intent is "these 32 bits are a float". The hardware
     * does NOT auto-advance LOAD between commands — games re-issue
     * SET_TRANSFORM_CONSTANT_LOAD before each block. */
    if (method >= NV4097_SET_TRANSFORM_CONSTANT &&
        method <  NV4097_SET_TRANSFORM_CONSTANT + 64 * 4) {
        u32 reg_offset = (method - NV4097_SET_TRANSFORM_CONSTANT) / 4;
        u32 slot = state->transform_constant_load + (reg_offset >> 2);
        u32 lane = reg_offset & 3;
        if (slot < RSX_MAX_VERTEX_CONSTANTS) {
            float f;
            memcpy(&f, &data, 4);
            { static int _en=-1; if(_en<0){const char*e=getenv("TCONST_DBG");_en=e?1:0;}
              static int _n=0;
              static int _max=-1; if(_max<0){const char*m=getenv("TCONST_MAX");_max=m?atoi(m):64;}
              int _hit = _en && (getenv("TCONST_ALL") ? (_n<_max) : (slot>=12 && slot<=30 && _n<400));
              if(_hit){ _n++; fprintf(stderr,"[TCONST] load=%u slot=%u lane=%u = %.4f\n", state->transform_constant_load, slot, lane, f); } }
            state->vertex_constants[slot][lane] = f;
            { static int _sq=0; if (getenv("SEQ_DBG") && slot==256 && lane==0 && _sq++ < 500)
                fprintf(stderr, "[SEQ] upload c256.x=%.4f (load=%u)\n", f, state->transform_constant_load); }
            if (!state->vertex_constants_dirty) {
                state->vertex_constants_lo = slot;
                state->vertex_constants_hi = slot;
                state->vertex_constants_dirty = 1;
            } else {
                if (slot < state->vertex_constants_lo) state->vertex_constants_lo = slot;
                if (slot > state->vertex_constants_hi) state->vertex_constants_hi = slot;
            }
        }
        return 0;
    }

    /* Draw */
    if (method == NV4097_SET_BEGIN_END) {
        draw_clause_flush(state);   /* le parole accumulate nel BEGIN/END che si chiude */
        if (data != 0) {
            state->primitive_type = data;
            state->in_begin_end = 1;
            state->begin_epoch++;
            s_inline_count = 0;   /* i vertici immediati sono per-primitiva */

            /* Flush dirty state to backend before drawing */
            if (s_backend) {
                if (state->surface_dirty && s_backend->set_render_target)
                    s_backend->set_render_target(s_backend->userdata, state);
                if (state->viewport_dirty && s_backend->set_viewport)
                    s_backend->set_viewport(s_backend->userdata, state);
                if (state->blend_dirty && s_backend->set_blend)
                    s_backend->set_blend(s_backend->userdata, state);
                if ((state->depth_dirty || state->stencil_dirty) && s_backend->set_depth_stencil)
                    s_backend->set_depth_stencil(s_backend->userdata, state);
                if (state->color_mask_dirty && s_backend->set_color_mask)
                    s_backend->set_color_mask(s_backend->userdata, state);
                if (state->alpha_dirty && s_backend->set_alpha_test)
                    s_backend->set_alpha_test(s_backend->userdata, state);
                if (state->shader_dirty && s_backend->set_shader)
                    s_backend->set_shader(s_backend->userdata, state);
                if (state->vertex_dirty && s_backend->set_vertex_attribs)
                    s_backend->set_vertex_attribs(s_backend->userdata, state);

                /* Bind dirty textures */
                if (state->texture_dirty && s_backend->bind_texture) {
                    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++) {
                        if (state->textures[i].dirty) {
                            s_backend->bind_texture(s_backend->userdata, i, &state->textures[i]);
                            state->textures[i].dirty = 0;
                        }
                    }
                }

                state->surface_dirty = 0;
                state->viewport_dirty = 0;
                state->blend_dirty = 0;
                state->depth_dirty = 0;
                state->stencil_dirty = 0;
                state->color_mask_dirty = 0;
                state->alpha_dirty = 0;
                state->shader_dirty = 0;
                state->vertex_dirty = 0;
                state->texture_dirty = 0;
            }
        } else {
            state->in_begin_end = 0;
            if (s_inline_count) {
                { static int _d = -1; if (_d < 0) _d = getenv("RSX_INLINE_DBG") ? 1 : 0;
                  static unsigned long _n = 0;
                  if (_d && (++_n <= 16 || (_n % 500) == 0))
                      fprintf(stderr, "[RSX inline] #%lu prim=%u verts=%u%s\n",
                              _n, state->primitive_type, s_inline_count,
                              (s_backend && s_backend->draw_inline) ? "" : "  (backend senza draw_inline)"); }
                if (s_backend && s_backend->draw_inline)
                    s_backend->draw_inline(s_backend->userdata, state->primitive_type,
                                           s_inline_verts, s_inline_count);
                if (s_inline_dropped) {
                    static int _w = 0;
                    if (_w++ < 8)
                        fprintf(stderr, "[RSX inline] %u vertici scartati (tetto %d)\n",
                                s_inline_dropped, RSX_INLINE_MAX_VERTS);
                    s_inline_dropped = 0;
                }
                s_inline_count = 0;
            }
        }
        return 0;
    }

    if (method == NV4097_DRAW_ARRAYS) {
        u32 first = data & 0xFFFFFF;
        u32 count = ((data >> 24) & 0xFF) + 1;
        { static int _d=0; if (_d++ < 32) fprintf(stderr, "[RSX] DRAW_ARRAYS prim=%u first=%u count=%u\n", state->primitive_type, first, count); }
        { static int _sq=0; if (getenv("SEQ_DBG") && _sq++ < 500)
            fprintf(stderr, "[SEQ] DRAW surf0=0x%X c256.x=%.4f c257.y=%.4f\n",
                    state->surface_color_offset[0], state->vertex_constants[256][0],
                    state->vertex_constants[257][1]); }
        /* MVPDBG: dump every non-zero vertex-constant slot the FIRST time a
         * G-buffer draw (surf0=0xCC0000) is dispatched -- the true MVP the GPU
         * will use, read live (no snapshot/parity indirection). */
        if (getenv("MVPDBG") && state->surface_color_offset[0] == 0xCC0000) {
            static int _once = 0;
            if (!_once) { _once = 1;
                for (int s = 0; s < RSX_MAX_VERTEX_CONSTANTS; s++) {
                    float* v = state->vertex_constants[s];
                    if (v[0]||v[1]||v[2]||v[3])
                        fprintf(stderr, "[MVP] c[%3d] = %10.4f %10.4f %10.4f %10.4f\n",
                                s, v[0], v[1], v[2], v[3]);
                }
            }
        }
        draw_clause_append(state, 1, first, count);
        return 0;
    }

    if (method == NV4097_SET_SHADER_CONTROL) {
        { static int _sc=0; if (_sc++ < 12 && getenv("RTT_DUMP"))
            fprintf(stderr, "[RSXSC] shader_control=0x%X\n", data); }
        /* RSX_SC_TRACE=1 (s.32): le prime 64 coppie distinte (programma, controllo) */
        { static int s_t = -1; if (s_t < 0) s_t = getenv("RSX_SC_TRACE") ? 1 : 0;
          if (s_t) { static u32 seen_p[64], seen_c[64]; static int n = 0; int k;
              for (k = 0; k < n; k++) if (seen_p[k] == state->shader_program && seen_c[k] == data) break;
              if (k == n && n < 64) { seen_p[n] = state->shader_program; seen_c[n] = data; n++;
                  fprintf(stderr, "[RSXSC] fp=%08X shader_control=%08X (prima %08X)\n", state->shader_program, data, state->shader_control); } } }
        state->shader_control = data;
        state->shader_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_INDEX_ARRAY_ADDRESS) {
        state->index_array_offset = data;
        return 0;
    }
    if (method == NV4097_SET_INDEX_ARRAY_DMA) {
        state->index_array_dma = data;
        return 0;
    }
    if (method == NV4097_DRAW_INDEX_ARRAY) {
        /* [23:0] first index, [31:24] count-1 (same packing as DRAW_ARRAYS). */
        u32 first = data & 0xFFFFFF;
        u32 count = ((data >> 24) & 0xFF) + 1;
        { static int _d=0; if (_d++ < 8) fprintf(stderr, "[RSX] DRAW_INDEX_ARRAY prim=%u first=%u count=%u idxoff=0x%X dma=0x%X\n", state->primitive_type, first, count, state->index_array_offset, state->index_array_dma); }
        draw_clause_append(state, 2, first, count);
        return 0;
    }

    /* Scissor */
    if (method == NV4097_SET_SCISSOR_HORIZONTAL) {
        state->scissor_x = data & 0xFFFF;
        state->scissor_w = (data >> 16) & 0xFFFF;
        return 0;
    }
    if (method == NV4097_SET_SCISSOR_VERTICAL) {
        state->scissor_y = data & 0xFFFF;
        state->scissor_h = (data >> 16) & 0xFFFF;
        return 0;
    }
    if (method == NV4097_SET_SHADER_WINDOW) {
        state->shader_window = data;
        return 0;
    }

    /* NV406E_SET_REFERENCE (0x0050): the title writes a reference value it then
     * spins on (cellGcmFinish / cellGcmSetWaitLabel). Record the latest so the
     * GCM control register can report completion back to the guest and unblock
     * the spin. Class 0 (software) method, no subchannel state needed here. */
    if (method == 0x0050) {
        g_rsx_last_reference = data;
        return 0;
    }

    /* Unrecognized method — log in debug builds */
#ifndef NDEBUG
    static int s_unknown_count = 0;
    if (s_unknown_count < 50) {
        printf("[RSX] unknown method 0x%04X = 0x%08X\n", method, data);
        s_unknown_count++;
    }
#endif

    return -1;
}

/* ---------------------------------------------------------------------------
 * Command buffer parsing
 * -----------------------------------------------------------------------*/

int rsx_process_command_buffer(rsx_state* state, const u32* buf, u32 size)
{
    int methods_processed = 0;
    u32 pos = 0;
    u32 count = size / 4; /* size in dwords */

    { static int _c=0; if (count && _c++ < 16) fprintf(stderr, "[RSX] process_cmd_buffer words=%u first_hdr=0x%08X\n", count, buf[0]); }
    while (pos < count) {
        u32 header = buf[pos++];
        u32 type = (header >> 29) & 0x7;

        if (type == 0 || type == 2) {
            /* Increasing or non-increasing method */
            u32 method = (header >> 2) & 0x7FF;
            method <<= 2; /* method addresses are dword-aligned */
            u32 num_data = (header >> 18) & 0x7FF;
            int increasing = (type == 0);

            for (u32 i = 0; i < num_data && pos < count; i++) {
                u32 data = buf[pos++];
                u32 m = increasing ? (method + i * 4) : method;
                rsx_process_method(state, m, data);
                methods_processed++;
            }
        } else if (type == 1) {
            /* Jump — change command buffer read position */
            /* In recomp context, this is handled by the caller */
            break;
        } else {
            /* Unknown type, skip */
            break;
        }
    }

    return methods_processed;
}

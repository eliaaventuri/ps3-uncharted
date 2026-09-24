/* incluso (-include) davanti al modulo liftato: l'arco all'indietro chiede al
 * banco se uscire al trampolino. In linea (nessuna chiamata: una chiamata su
 * ARM64 distrugge i registri vettoriali e falserebbe il confronto). */
extern volatile int g_h_one, g_h_zero;
extern unsigned long long g_h_ticks, g_h_budget;
extern unsigned g_h_every, g_h_count;
static inline volatile int* harness_lock_ptr(void) {
    if (++g_h_ticks >= g_h_budget) return &g_h_one;
    if (g_h_every && --g_h_count == 0) { g_h_count = g_h_every; return &g_h_one; }
    return &g_h_zero;
}
#define g_yz_lockstep_on (*harness_lock_ptr())

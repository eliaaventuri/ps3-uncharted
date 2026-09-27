/* Ordine dei byte della local store SPU (sessione 47): vedi "Ordine dei byte
 * della local store" in spu_context.h. Header a parte perche' serve anche a
 * chi scrive nella LS senza includere spu_context.h (libs/spurs/spurs_pm.c).
 * SPU_LSB(a) = indice fisico del byte SPU a. */
#ifndef SPU_LS_ORDER_H
#define SPU_LS_ORDER_H
#ifndef SPU_LS_XOR
#if defined(__aarch64__) && defined(__ARM_NEON)
#define SPU_LS_XOR 3
#else
#define SPU_LS_XOR 0
#endif
#endif
#define SPU_LSB(a) ((a) ^ SPU_LS_XOR)
#endif /* SPU_LS_ORDER_H */

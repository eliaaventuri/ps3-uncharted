# TASK SPECIFICATION: SPU-FLAG-04 — chi chiama il DMA di completamento

## 1. OBIETTIVO
Trovare, in **un solo file**, la funzione che chiama
`jobmod_008cc900_8acfddb1519c_spu_func_00006120` e riportare testualmente il
codice che imposta i registri `r7`, `r8`, `r9` prima della chiamata. Sola
lettura: non modificare nulla, non compilare, non lanciare niente.

## 2. CONTESTO
Il file, unico da leggere (5.760 righe):

`/Users/elia/Documents/Coding/uncharted-data/spu_jobs/lifted/jobmod_008cc900_8acfddb1519c/spu_recomp.c`

A riga 2593 c'e':

```c
void jobmod_008cc900_8acfddb1519c_spu_func_00006120(spu_context* ctx) {
        ctx->gpr[35] = spu_ori(ctx->gpr[7], 0);
        spu_wrch(ctx, MFC_LSA, ctx->gpr[35]);
        ctx->gpr[34] = spu_ori(ctx->gpr[9], 0);
        spu_wrch(ctx, MFC_EAL, ctx->gpr[34]);
        ...
}
```

cioe' e' un "manda 16 byte dalla local store all'indirizzo `r9` in memoria
principale". Quando `r9` vale `0x20000000` questo e' il segnale di "lavoro
finito" che il PPU aspetta. Serve sapere **chi** lo chiama e **da dove viene
r9**.

Nel file le funzioni si chiamano `jobmod_008cc900_8acfddb1519c_spu_func_XXXXXXXX`
e i salti sono scritti come
`{ ctx->pc = 0xNNNN; g_spu_trampoline_fn = <funzione>; return; }`.

## 3. VINCOLI
* **Nessuna modifica a nessun file.** Niente compilazione, niente esecuzione.
* Usa `grep -n` e poi `sed -n 'A,Bp'`: **non leggere il file intero.**
* Non aprire altri file.

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md`, sezione "EVIDENZE RACCOLTE":

1. Ogni riga del file che nomina `spu_func_00006120` (comando:
   `grep -n 'spu_func_00006120' <file>`), col numero di riga.
2. Per ciascun **chiamante** trovato (escludendo la definizione a riga 2593 e la
   tabella a riga ~5563): le **30 righe precedenti** alla chiamata, copiate
   testualmente.
3. In quelle righe, indica quali assegnano `ctx->gpr[7]`, `ctx->gpr[8]`,
   `ctx->gpr[9]` — con la riga esatta.
4. Se il chiamante e' raggiunto da un salto condizionale, copia testualmente la
   condizione (`if (...)`) che lo precede.

Se una cosa non c'e' nel file, scrivi `NON TROVATO` e perche', senza indovinare.

## 5. STOP CONDITIONS
* Qualsiasi modifica a un file: fermarsi.
* Se `spu_func_00006120` compare solo nella definizione e nella tabella (nessun
  chiamante), riportalo come risultato — e' un'informazione, non un fallimento.

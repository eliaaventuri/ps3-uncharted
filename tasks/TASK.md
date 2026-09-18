# TASK SPECIFICATION: S29-RICOSTRUZIONE — che cosa è successo nella sessione 29

## 1. OBIETTIVO
Ricostruire, SOLO LEGGENDO, il lavoro della "sessione 29" (14 settembre 2026)
che non è stato documentato in `docs/STATO.md`, e scrivere in `tasks/RESULT.md`
una bozza (in italiano, max 90 righe) della sezione
`## Sessione 29 — ...` da incollare in STATO.md, nello stesso stile delle
sezioni esistenti (fatti, misure con numeri, cosa ha fallito, variabili
d'ambiente nuove). Sola lettura: non modificare nulla, non compilare, non
lanciare niente.

## 2. CONTESTO
Il repo è `/Users/elia/Documents/Coding/ps3-uncharted`. L'ultimo commit è del
13/09 sera (sessione 28, vedi `docs/STATO.md` righe 422-780 per lo stile e per
il punto di partenza: "Il prossimo passo (sessione 28)"). Il 14/09 sono stati
modificati, senza commit e senza aggiornare STATO.md, questi file:

- `libs/video/rsx_metal_backend.m` (commenti con "sessione 29" alle righe
  ~1054, ~1176, ~1330, ~1574; cerca anche `tex_fingerprint`, `RSX_TEX_FP`,
  `RSX_TEX_TRACE`, `RSX_FP_PROBE`, `RSX_EQUAL_LEQUAL`, `RSX_DEBUG_TINT`,
  `fs_depth_guest`, `s_fpc`, `MTL_FPC_BYTES`, `fp_ucode_hash`, `fpc_block`)
- `libs/video/cellGcmSys.c` (cerca `NV0039`, `nv0039_copy`, `GCM_NV0039`,
  `func_004D19B8`)
- `libs/video/rsx_commands.c` (cerca `RSX_TEXOFF_TRACE`, `[texoff]`)
- `libs/video/rsx_vertex_fetch.c` (cerca `case 6:` CMP, "sessione 29")
- `libs/input/cellPad.c` (riga ~513, "s.29": mappa dei tasti nel livello)
- `libs/video/rsx_fp_decompiler.c` / `.h` (`rsx_fp_structural_hash`,
  `rsx_fp_collect_constants`: le costanti del FP fuori dall'hash della cache,
  passate come buffer — è il passo 0 di "Il prossimo passo (sessione 28)")

Le due immagini `docs/img_s29_prima_del_nv0039.png` e
`docs/img_s29_dive_boat_display.png` (quest'ultima mostra il livello dive-boat
a schermo con l'HUD "Ambushed", "Press ○ to take cover": pavimento e casse
texturizzati, cielo con nuvole; restano triangoli neri in alto a sinistra e
superfici verdi piatte a destra).

I log delle corse stanno in `/Users/elia/Documents/Coding/uncharted-data/`:
`run_s29_c1.log` ... `run_s29_c25.log` (5-11 MB l'uno, NON leggerli per
intero). Per ognuno estrai con `grep` solo:
- le righe `[RSX metal]` che contengono `framestat` o `fps` o `draw` (numeri
  di draw per fotogramma e fps: es. `grep -m5 -i "framestat\|fps" run_s29_cN.log`),
- le prime 3 righe `[RSX metal] cache dei fragment program PIENA`,
  `arena delle costanti FP piena`, `dropped`, `Rendering is stuck`,
  `halting the SPU`, `ppu_guest_trap`, `assert` (se ci sono),
- l'ultima riga con `f#` o `frame` per sapere fino a che fotogramma è
  arrivata la corsa,
- e le variabili d'ambiente della corsa, se il log le stampa nelle prime
  40 righe (`head -40`), oppure da `run.sh` (le variabili con
  `RSX_`, `GCM_`, `SPU_`, `SPURS_` esportate).
Confronta la prima corsa (c1) con l'ultima (c25) e riporta la progressione
(cosa è cambiato tra le corse, con i numeri).

## 3. VINCOLI
* File autorizzati alla modifica: SOLO `tasks/RESULT.md`.
* Non modificare nessun file del repo, non compilare, non eseguire il port.
* Non leggere i log per intero: solo `grep`/`head`/`tail` mirati.

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md` contiene:
1. STATO FINALE `SUCCESS` (o `AMBIGUOUS` se qualcosa non si ricostruisce).
2. La bozza della sezione `## Sessione 29 — <titolo>` per STATO.md: per ogni
   modifica trovata nel codice, 2-5 righe: il sintomo (cosa si vedeva),
   la causa, il fix, la variabile d'ambiente per spegnerlo/tracciarlo.
3. Una tabella o elenco `corsa -> fps, draw/fotogramma, ultimo fotogramma,
   eventi anomali` per le 25 corse (una riga l'una).
4. Un paragrafo "Fronte aperto" con quello che l'ultima corsa lascia
   irrisolto (triangoli neri, superfici verdi, errori ricorrenti nel log).
5. L'elenco delle variabili d'ambiente NUOVE (non presenti in
   `docs/DIAGNOSTICS.md` né nella sezione "Strumenti nuovi (sessione 28)" di
   STATO.md).

## 5. STOP CONDITIONS
* Se un file elencato non esiste, segnalalo e continua con gli altri.

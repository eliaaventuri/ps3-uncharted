# TASK SPECIFICATION: S35-SWAP-POLL - Cosa aspetta SwapBuffers su 0x2000A910

## 1. OBIETTIVO
Leggere il codice PPU liftato (C++ generato, NON modificarlo) di tre funzioni di
Uncharted e spiegare in modo sintetico la condizione di attesa del PPU sui job SPU
alla fine del fotogramma: quale struttura legge, quali campi confronta, quando esce.

## 2. CONTESTO & IPOTESI
* Il port (`~/Documents/Coding/uncharted-data/recomp/ppu_recomp_*.cpp`) e' C++
  generato dal lifter: `ctx->gpr[n]` sono i registri PPC, `vm_read32/16(ea)` legge
  la memoria guest big-endian, `func_XXXXXXXX` sono funzioni del gioco (indirizzo).
* Il thread principale, nel livello, gira il 26% del tempo in `vm_read32` su
  0x2000A910 chiamando `func_007428C0` e `func_0074073C` da `func_0014F528`
  (SwapBuffers). Dopo la morte del personaggio il gioco si ferma per sempre in
  quell'attesa: serve sapere ESATTAMENTE cosa aspetta.
* Ipotesi: `func_0074073C(r3=obj, r4=n)` legge `obj+4 -> p` (intestazione di una
  job list WwsJob) e scorre `p+0x14+2*i` (u16 per SPU, "ticket"/"min job index"),
  riducendoli con `func_00742838` (min o max?), poi confronta con `p+0x12`.

## 3. VINCOLI & REGOLE DI INGAGGIAMENTO
* SOLA LETTURA. Nessun file va modificato. Nessuna compilazione.
* Funzioni da leggere (usa `grep -n "^void func_XXXXXXXX" recomp/*.cpp` per trovarle,
  poi `sed -n` sull'intervallo fino alla `void func_` successiva):
  - `func_0074073C` (recomp/ppu_recomp_005.cpp riga ~286960, 72 righe)
  - `func_007428C0` (recomp/ppu_recomp_005.cpp riga ~289298, 50 righe)
  - `func_00742838` (chiamata da 0074073C)
  - `func_0014F528` (recomp/ppu_recomp_000.cpp riga ~472977, 544 righe): SOLO la
    parte attorno alle chiamate a `func_007428C0`/`func_0074073C` (il ciclo di
    attesa: da quali valori dipende, cosa fa se l'attesa non finisce, se c'e' un
    timeout o un `sys_timer_usleep`/`cellGcm*` nel ciclo).
* Non leggere altro del repo: bastano queste funzioni.

## 4. CRITERI DI SUCCESSO
Scrivere `tasks/RESULT.md` (max 60 righe) con:
1. Layout della struttura letta da `func_0074073C`: offset e significato dei
   campi (`+0x4`, `+0x12`, `+0x14+2*i`, ...), cosa ritorna la funzione.
2. `func_007428C0`: cosa legge e cosa ritorna.
3. Il ciclo di attesa in `func_0014F528`: pseudocodice in 10-15 righe
   (condizione di uscita, chi dovrebbe cambiare i valori letti — PPU o SPU).
4. Se in `func_0014F528` ci sono chiamate a import HLE (nomi `cellGcm*`,
   `cellSpurs*`, `sys_*`) nel ciclo, elencarle nell'ordine.
STATO FINALE: SUCCESS se i 4 punti sono coperti, altrimenti AMBIGUOUS con cio' che manca.

## 5. STOP CONDITIONS
* Le funzioni non si trovano nei file indicati: fermarsi e riportarlo.
